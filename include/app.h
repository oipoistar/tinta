#ifndef TINTA_APP_H
#define TINTA_APP_H

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dwrite_2.h>
#include <wincodec.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>

#include "markdown.h"

using namespace qmd;

// Timing helpers
using Clock = std::chrono::high_resolution_clock;
inline int64_t usElapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
}

// Timer IDs (TIMER_FILE_WATCH=1 lives in file_utils.h, TIMER_EDITOR_REPARSE=2 in editor.cpp)
#define TIMER_CURSOR_BLINK 3
#define TIMER_NOTIFICATION 4
#define TIMER_ZOOM_APPLY 5

// Posted to continue an incomplete document layout in time-budgeted chunks
#define WM_APP_LAYOUT_CHUNK (WM_APP + 1)

// Startup metrics
struct StartupMetrics {
    int64_t windowInitUs = 0;
    int64_t d2dInitUs = 0;
    int64_t dwriteInitUs = 0;
    int64_t renderTargetUs = 0;
    int64_t fileLoadUs = 0;
    int64_t showWindowUs = 0;
    int64_t consoleInitUs = 0;
    int64_t totalStartupUs = 0;
};

// Syntax highlighting token types
enum class SyntaxTokenType { Plain, Keyword, String, Comment, Number, Function, TypeName, Operator, ControlFlow };

// Theme colors
struct D2DTheme {
    const wchar_t* name;
    const wchar_t* fontFamily;       // Main font
    const wchar_t* codeFontFamily;   // Monospace font
    bool isDark;
    D2D1_COLOR_F background;
    D2D1_COLOR_F text;
    D2D1_COLOR_F heading;
    D2D1_COLOR_F link;
    D2D1_COLOR_F code;
    D2D1_COLOR_F codeBackground;
    D2D1_COLOR_F blockquoteBorder;
    D2D1_COLOR_F accent;             // For UI elements
    // Syntax highlighting colors
    D2D1_COLOR_F syntaxKeyword;
    D2D1_COLOR_F syntaxString;
    D2D1_COLOR_F syntaxComment;
    D2D1_COLOR_F syntaxNumber;
    D2D1_COLOR_F syntaxFunction;
    D2D1_COLOR_F syntaxType;
    D2D1_COLOR_F syntaxControlFlow;
};

// Helper to create color from hex
inline D2D1_COLOR_F hexColor(uint32_t hex, float alpha = 1.0f) {
    return D2D1::ColorF(
        ((hex >> 16) & 0xFF) / 255.0f,
        ((hex >> 8) & 0xFF) / 255.0f,
        (hex & 0xFF) / 255.0f,
        alpha
    );
}

// Forward declare App for dpi() helper
struct App;

// DPI scaling helper for UI chrome elements.
// Scales by contentScale only (not zoomFactor) so UI chrome tracks monitor DPI.
inline float dpi(const App& app, float value);

// Themes array (defined in themes.cpp)
extern const D2DTheme THEMES[];
extern const int THEME_COUNT;

// Persistent settings
struct Settings {
    int themeIndex = 5;          // Default to Midnight
    float zoomFactor = 1.0f;
    int windowX = CW_USEDEFAULT;
    int windowY = CW_USEDEFAULT;
    int windowWidth = 1024;
    int windowHeight = 768;
    bool windowMaximized = false;
    bool hasAskedFileAssociation = false;
    bool editorShowPreview = true;
    bool editorWordWrap = false;
};

// Application state
struct App {
    // Win32
    HWND hwnd = nullptr;
    int width = 1024;
    int height = 768;
    bool running = true;

    // Direct2D
    ID2D1Factory* d2dFactory = nullptr;
    ID2D1HwndRenderTarget* renderTarget = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    ID2D1DeviceContext* deviceContext = nullptr;  // For color emoji rendering

    // WIC (Windows Imaging Component) for image loading
    IWICImagingFactory* wicFactory = nullptr;

    // Image cache
    struct ImageEntry {
        ID2D1Bitmap* bitmap = nullptr;
        int width = 0;
        int height = 0;
        bool failed = false;
    };
    std::unordered_map<std::string, ImageEntry> imageCache;

    // Layout bitmaps (document coordinates)
    struct LayoutBitmap {
        ID2D1Bitmap* bitmap = nullptr;
        D2D1_RECT_F destRect{};
    };
    std::vector<LayoutBitmap> layoutBitmaps;

    // DirectWrite
    IDWriteFactory* dwriteFactory = nullptr;
    IDWriteFontFallback* fontFallback = nullptr;  // For emoji font fallback
    IDWriteTextAnalyzer* textAnalyzer = nullptr;  // UAX#14 line-break analysis
    IDWriteTextFormat* textFormat = nullptr;
    IDWriteTextFormat* headingFormat = nullptr;
    IDWriteTextFormat* codeFormat = nullptr;
    IDWriteTextFormat* boldFormat = nullptr;
    IDWriteTextFormat* italicFormat = nullptr;
    IDWriteTextFormat* headingFormats[6] = {};

    // Overlay text formats (cached)
    IDWriteTextFormat* searchTextFormat = nullptr;
    IDWriteTextFormat* themeTitleFormat = nullptr;
    IDWriteTextFormat* themeHeaderFormat = nullptr;

    struct ThemePreviewFormats {
        IDWriteTextFormat* name = nullptr;
        IDWriteTextFormat* preview = nullptr;
        IDWriteTextFormat* code = nullptr;
    };
    std::vector<ThemePreviewFormats> themePreviewFormats;

    // OpenType typography
    IDWriteTypography* bodyTypography = nullptr;
    IDWriteTypography* codeTypography = nullptr;

    // Folder browser text format
    IDWriteTextFormat* folderBrowserFormat = nullptr;

    // TOC text formats
    IDWriteTextFormat* tocFormat = nullptr;
    IDWriteTextFormat* tocFormatBold = nullptr;

    // Markdown
    MarkdownParser parser;
    ElementPtr root;
    std::string currentFile;
    bool focusMermaidOnNextLayout = false;
    size_t parseTimeUs = 0;
    float contentHeight = 0;
    float contentWidth = 0;

    // State
    float scrollY = 0;
    float scrollX = 0;
    float targetScrollY = 0;
    float targetScrollX = 0;
    float contentScale = 1.0f;  // DPI scale
    float zoomFactor = 1.0f;    // User zoom (Ctrl+scroll)
    float appliedZoomFactor = 1.0f;  // zoomFactor last baked into text formats
    bool zoomApplyPending = false;   // TIMER_ZOOM_APPLY armed to coalesce zoom ticks
    bool darkMode = true;
    bool showStats = false;
    int currentThemeIndex = 5;  // Default to "Midnight" (first dark theme)
    D2DTheme theme = THEMES[5];

    // Theme chooser overlay
    bool showThemeChooser = false;
    int hoveredThemeIndex = -1;
    float themeChooserAnimation = 0.0f;  // 0 to 1 for fade in

    // Folder browser overlay
    bool showFolderBrowser = false;
    float folderBrowserAnimation = 0.0f;  // 0 to 1 for slide-in from left
    std::wstring folderBrowserPath;       // Current directory being browsed
    struct FolderItem {
        std::wstring name;
        bool isDirectory;
    };
    std::vector<FolderItem> folderItems;
    int hoveredFolderIndex = -1;
    float folderBrowserScroll = 0.0f;     // Scroll offset for folder list

    // Help overlay
    bool showHelp = false;
    float helpAnimation = 0.0f;
    float helpScroll = 0.0f;
    float helpContentHeight = 0.0f;   // Total content height (set during render)
    float helpVisibleHeight = 0.0f;   // Visible area height (set during render)
    float helpScrollbarTop = 0.0f;    // Scrollbar track top Y (set during render)
    bool helpScrollbarDragging = false;
    float helpScrollbarDragStartY = 0;
    float helpScrollbarDragStartScroll = 0;

    // Table of contents overlay
    bool showToc = false;
    float tocAnimation = 0.0f;  // 0 to 1 for slide-in from right
    struct HeadingInfo {
        std::wstring text;
        int level;       // 1-6
        float y;         // document Y coordinate
    };
    std::vector<HeadingInfo> headings;
    int hoveredTocIndex = -1;
    float tocScroll = 0.0f;

    // Mouse
    bool mouseDown = false;
    int mouseX = 0;
    int mouseY = 0;

    // Vertical scrollbar
    bool scrollbarHovered = false;
    bool scrollbarDragging = false;
    float scrollbarDragStartY = 0;
    float scrollbarDragStartScroll = 0;

    // Horizontal scrollbar
    bool hScrollbarHovered = false;
    bool hScrollbarDragging = false;
    float hScrollbarDragStartX = 0;
    float hScrollbarDragStartScroll = 0;

    // Links - tracked during render for click detection
    struct LinkRect {
        D2D1_RECT_F bounds;
        std::string url;
    };
    std::vector<LinkRect> linkRects;
    std::string hoveredLink;

    // Code block info - tracked for copy button
    struct CodeBlockInfo {
        D2D1_RECT_F bounds;       // Full background rect in document coordinates
        std::wstring codeText;    // The code content
    };
    std::vector<CodeBlockInfo> codeBlocks;
    int hoveredCodeBlock = -1;

    // Text bounds - tracked for cursor changes and selection (document coordinates)
    struct TextRect {
        D2D1_RECT_F rect;
        size_t docStart = 0;   // Start position in docText
        size_t docLength = 0;  // Length in docText
    };
    std::vector<TextRect> textRects;

    // Line buckets for fast hit-testing/selection
    struct LineBucket {
        float top = 0;
        float bottom = 0;
        float minX = 0;
        float maxX = 0;
        std::vector<size_t> textRectIndices;
    };
    std::vector<LineBucket> lineBuckets;

    // Search match info
    struct SearchMatch {
        size_t textRectIndex;       // Index into textRects
        size_t startPos;            // Character offset in text
        size_t length;              // Match length
        D2D1_RECT_F highlightRect;  // Computed highlight bounds
    };
    std::vector<SearchMatch> searchMatches;
    bool overText = false;

    // Text selection
    bool selecting = false;
    int selStartX = 0, selStartY = 0;
    int selEndX = 0, selEndY = 0;
    bool hasSelection = false;
    std::wstring selectedText;

    // Multi-click selection (double/triple click)
    std::chrono::steady_clock::time_point lastClickTime;
    int clickCount = 0;
    int lastClickX = 0, lastClickY = 0;
    enum class SelectionMode { Normal, Word, Line } selectionMode = SelectionMode::Normal;
    // Anchor bounds for word/line selection (the original word/line that was clicked)
    float anchorLeft = 0, anchorRight = 0, anchorTop = 0, anchorBottom = 0;

    // Document text built during render (used for search/mapping)
    std::wstring docText;
    std::wstring docTextLower;

    // Cached space widths for common formats
    float spaceWidthText = 0.0f;
    float spaceWidthBold = 0.0f;
    float spaceWidthItalic = 0.0f;
    float spaceWidthCode = 0.0f;

    // Layout cache (document coordinates)
    struct LayoutTextRun {
        IDWriteTextLayout* layout = nullptr;
        D2D1_POINT_2F pos{};
        D2D1_RECT_F bounds{};
        D2D1_COLOR_F color{};
        size_t docStart = 0;
        size_t docLength = 0;
        bool selectable = false;
    };
    struct LayoutRect {
        D2D1_RECT_F rect{};
        D2D1_COLOR_F color{};
    };
    struct LayoutLine {
        D2D1_POINT_2F p1{};
        D2D1_POINT_2F p2{};
        D2D1_COLOR_F color{};
        float stroke = 1.0f;
    };
    enum class LayoutShapeType {
        Rectangle,
        RoundedRectangle,
        Diamond,
        Stadium,
        Ellipse,
        Hexagon,
    };
    struct LayoutShape {
        LayoutShapeType type = LayoutShapeType::Rectangle;
        D2D1_RECT_F rect{};
        D2D1_COLOR_F fill{};
        D2D1_COLOR_F stroke{};
        float strokeWidth = 1.0f;
        float radius = 0.0f;
    };
    struct LayoutConnector {
        std::vector<D2D1_POINT_2F> points;
        D2D1_RECT_F bounds{};
        D2D1_COLOR_F color{};
        float stroke = 1.0f;
        float arrowSize = 8.0f;
        bool directed = true;
        bool dashed = false;
    };
    std::vector<LayoutTextRun> layoutTextRuns;
    std::vector<LayoutRect> layoutRects;
    std::vector<LayoutLine> layoutLines;
    std::vector<LayoutShape> layoutShapes;
    std::vector<LayoutConnector> layoutConnectors;
    bool layoutDirty = true;

    // Incremental layout: the first paint lays out ~2 viewports, the rest
    // continues in WM_APP_LAYOUT_CHUNK time slices (see render.cpp)
    bool layoutComplete = true;
    size_t layoutNextBlock = 0;   // next top-level block to lay out
    float layoutCursorY = 0.0f;   // y where the next block starts
    float layoutIndent = 0.0f;
    float layoutMaxWidth = 0.0f;
    size_t layoutTimeUs = 0;      // total layout time for the current cycle

    // Scroll sync anchors: source byte offset → rendered Y position
    struct ScrollAnchor {
        size_t sourceOffset;
        float renderedY;
    };
    std::vector<ScrollAnchor> scrollAnchors;
    std::vector<size_t> editorLineByteOffsets;  // UTF-8 byte offset per editor line

    size_t searchMatchCursor = 0;

    // Copied notification (fades out over 2 seconds)
    bool showCopiedNotification = false;
    float copiedNotificationAlpha = 0.0f;
    std::chrono::steady_clock::time_point copiedNotificationStart;

    // Cursor blink state, toggled by TIMER_CURSOR_BLINK (editor + search cursor)
    bool cursorBlinkOn = true;

    // Search overlay
    bool showSearch = false;
    float searchAnimation = 0.0f;
    std::wstring searchQuery;
    int searchCurrentIndex = 0;
    bool searchActive = false;
    bool searchJustOpened = false;  // Skip WM_CHAR after opening with F key

    // File watching (auto-reload)
    FILETIME lastFileWriteTime = {};
    bool fileWatchEnabled = true;

    // Edit mode
    bool editMode = false;
    float editorSplitRatio = 0.5f;
    bool draggingSeparator = false;
    float separatorDragStartX = 0;
    float separatorDragStartRatio = 0;

    // Double-ESC detection
    std::chrono::steady_clock::time_point lastEscTime;
    bool escPressedOnce = false;
    bool confirmExitPending = false;  // Waiting for Y/N to confirm unsaved exit

    // Editor notification
    bool showEditModeNotification = false;
    float editModeNotificationAlpha = 0;
    std::chrono::steady_clock::time_point editModeNotificationStart;
    std::wstring editorNotificationMsg;

    // Editor document
    std::wstring editorText;
    bool editorDirty = false;
    std::vector<size_t> editorLineStarts;

    // Editor view options (persisted)
    bool editorShowPreview = true;
    bool editorWordWrap = false;

    // Soft-wrap metrics: cumulative visual rows before each logical line
    // (editorRowStarts.size() == lines + 1). Only maintained while
    // editorWordWrap is on; rebuilt when text or wrap width changes.
    std::vector<size_t> editorRowStarts;
    size_t editorTotalRows = 0;
    float editorRowMetricsWidth = -1.0f;  // wrap width the metrics were built for

    // Editor cursor & selection
    size_t editorCursorPos = 0;
    int editorDesiredCol = -1;
    float editorDesiredX = -1.0f;  // desired caret x for Up/Down in wrap mode
    bool editorSelecting = false;
    size_t editorSelStart = 0;
    size_t editorSelEnd = 0;
    bool editorHasSelection = false;

    // Editor scroll
    float editorScrollY = 0;
    float editorContentHeight = 0;

    // Editor search
    struct EditorSearchMatch {
        size_t startPos;
        size_t length;
    };
    std::vector<EditorSearchMatch> editorSearchMatches;
    int editorSearchCurrentIndex = 0;

    // Undo/redo
    struct EditAction {
        enum Type { Insert, Delete };
        Type type;
        size_t position;
        std::wstring text;
        size_t cursorBefore, cursorAfter;
    };
    std::vector<EditAction> undoStack;
    std::vector<EditAction> redoStack;

    // Editor text format (monospace)
    IDWriteTextFormat* supSubFormat = nullptr;   // small size for ^sup^/~sub~
    IDWriteTextFormat* editorTextFormat = nullptr;
    float editorCharWidth = 0.0f; // Measured monospace char width

    // Metrics
    StartupMetrics metrics;
    size_t drawCalls = 0;

    ~App() { shutdown(); }

    void clearLayoutCache() {
        for (auto& run : layoutTextRuns) {
            if (run.layout) {
                run.layout->Release();
            }
        }
        layoutTextRuns.clear();
        layoutRects.clear();
        layoutLines.clear();
        layoutShapes.clear();
        layoutConnectors.clear();
        layoutBitmaps.clear();
        linkRects.clear();
        codeBlocks.clear();
        textRects.clear();
        lineBuckets.clear();
        docText.clear();
        docTextLower.clear();
        headings.clear();
    }

    void releaseOverlayFormats() {
        if (searchTextFormat) { searchTextFormat->Release(); searchTextFormat = nullptr; }
        if (themeTitleFormat) { themeTitleFormat->Release(); themeTitleFormat = nullptr; }
        if (themeHeaderFormat) { themeHeaderFormat->Release(); themeHeaderFormat = nullptr; }
        if (folderBrowserFormat) { folderBrowserFormat->Release(); folderBrowserFormat = nullptr; }
        if (tocFormat) { tocFormat->Release(); tocFormat = nullptr; }
        if (tocFormatBold) { tocFormatBold->Release(); tocFormatBold = nullptr; }
        if (supSubFormat) { supSubFormat->Release(); supSubFormat = nullptr; }
        if (editorTextFormat) { editorTextFormat->Release(); editorTextFormat = nullptr; }
        for (auto& fmt : themePreviewFormats) {
            if (fmt.name) { fmt.name->Release(); fmt.name = nullptr; }
            if (fmt.preview) { fmt.preview->Release(); fmt.preview = nullptr; }
            if (fmt.code) { fmt.code->Release(); fmt.code = nullptr; }
        }
        themePreviewFormats.clear();
    }

    void releaseImageCache() {
        for (auto& [key, entry] : imageCache) {
            if (entry.bitmap) { entry.bitmap->Release(); entry.bitmap = nullptr; }
        }
        imageCache.clear();
    }

    void shutdown() {
        clearLayoutCache();
        releaseOverlayFormats();
        releaseImageCache();
        if (wicFactory) { wicFactory->Release(); wicFactory = nullptr; }
        if (brush) { brush->Release(); brush = nullptr; }
        if (deviceContext) { deviceContext->Release(); deviceContext = nullptr; }
        if (renderTarget) { renderTarget->Release(); renderTarget = nullptr; }
        if (fontFallback) { fontFallback->Release(); fontFallback = nullptr; }
        if (textAnalyzer) { textAnalyzer->Release(); textAnalyzer = nullptr; }
        if (textFormat) { textFormat->Release(); textFormat = nullptr; }
        if (headingFormat) { headingFormat->Release(); headingFormat = nullptr; }
        if (codeFormat) { codeFormat->Release(); codeFormat = nullptr; }
        if (boldFormat) { boldFormat->Release(); boldFormat = nullptr; }
        if (italicFormat) { italicFormat->Release(); italicFormat = nullptr; }
        for (auto& fmt : headingFormats) {
            if (fmt) { fmt->Release(); fmt = nullptr; }
        }
        if (bodyTypography) { bodyTypography->Release(); bodyTypography = nullptr; }
        if (codeTypography) { codeTypography->Release(); codeTypography = nullptr; }
        if (dwriteFactory) { dwriteFactory->Release(); dwriteFactory = nullptr; }
        if (d2dFactory) { d2dFactory->Release(); d2dFactory = nullptr; }
    }
};

inline float dpi(const App& app, float value) {
    return value * app.contentScale;
}

// Width of the editor pane in edit mode (full window when preview is hidden)
inline float editorPaneWidth(const App& app) {
    return app.editorShowPreview
        ? app.width * app.editorSplitRatio - 3.0f
        : static_cast<float>(app.width);
}

inline float documentViewportX(const App& app) {
    if (!app.editMode) return 0.0f;
    // Preview hidden: zero-width viewport at the right edge — document
    // rendering flows through unchanged and clips to nothing
    if (!app.editorShowPreview) return static_cast<float>(app.width);
    return app.width * app.editorSplitRatio + 3.0f;
}

inline float documentViewportWidth(const App& app) {
    float width = app.editMode
        ? static_cast<float>(app.width) - documentViewportX(app)
        : static_cast<float>(app.width);
    return width > 0.0f ? width : 0.0f;
}

// Cursor blink runs on a timer instead of per-frame InvalidateRect so the
// app is fully idle between blinks. Call after editMode/search state changes.
inline void updateBlinkTimer(App& app) {
    if (!app.hwnd) return;
    if (app.editMode || (app.showSearch && app.searchActive)) {
        app.cursorBlinkOn = true;
        SetTimer(app.hwnd, TIMER_CURSOR_BLINK, 500, nullptr);
    } else {
        KillTimer(app.hwnd, TIMER_CURSOR_BLINK);
        app.cursorBlinkOn = true;
    }
}

// Restart the blink phase so the cursor stays visible while typing
inline void resetCursorBlink(App& app) {
    app.cursorBlinkOn = true;
    if (app.hwnd) SetTimer(app.hwnd, TIMER_CURSOR_BLINK, 500, nullptr);
}

// Notification fades repaint on this timer; the WM_TIMER handler kills it
// once no notification is active.
inline void startNotificationTimer(App& app) {
    if (app.hwnd) SetTimer(app.hwnd, TIMER_NOTIFICATION, 33, nullptr);
}

#endif // TINTA_APP_H
