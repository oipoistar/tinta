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
#define TIMER_IMAGE_REFLOW 6
#define TIMER_FOLDER_SEARCH 7
#define TIMER_SELECT_SCROLL 8

// Posted to continue an incomplete document layout in time-budgeted chunks
#define WM_APP_LAYOUT_CHUNK (WM_APP + 1)

// Posted by an image download worker thread when a remote image finished
// loading (lParam = AsyncImageResult*, ownership transfers to the handler)
#define WM_APP_IMAGE_READY (WM_APP + 2)

// Posted by the GPU warm-up thread once the D3D driver is initialized: the
// software render target used for the instant first paint is then swapped
// for a hardware one (cheap now that the driver is warm)
#define WM_APP_GPU_READY (WM_APP + 3)

// Posted by the folder-search worker with its scan results
// (lParam = FolderScanMsg*, ownership transfers to the handler)
#define WM_APP_FOLDER_SEARCH (WM_APP + 4)

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

// Dynamic theme registry (#82): the built-ins plus user themes loaded from
// %APPDATA%\Tinta\themes.ini. Custom themes own their strings and live at
// stable addresses for the app lifetime, so D2DTheme's const wchar_t*
// members stay valid. Indices: [0, THEME_COUNT) built-in, then customs.
struct CustomTheme {
    std::wstring name, fontFamily, codeFontFamily;
    D2DTheme theme;
};
int themeCount();
const D2DTheme& themeAt(int index);  // out-of-range clamps to theme 0
void loadCustomThemes();
// Adds or replaces (by name) a user theme and rewrites themes.ini.
// Returns the theme's registry index, or -1 on failure.
int saveCustomTheme(const D2DTheme& t, const std::wstring& name,
                    const std::wstring& fontFamily,
                    const std::wstring& codeFontFamily);

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
    // Auto theme: follow the Windows light/dark preference with a preferred
    // theme for each mode
    bool followSystemTheme = false;
    int lightThemeIndex = 0;   // Paper
    int darkThemeIndex = 5;    // Midnight
    // Search results from sibling markdown files in the search overlay
    bool folderSearchEnabled = true;
    // B opens the folder browser with the path box focused (#81)
    bool browserFocusPath = false;
    // Reading positions: most-recent-first, capped (#77)
    struct ReadingPosition { std::string path; float scrollY; };
    std::vector<ReadingPosition> readingPositions;
    // [Keys] overrides from settings.ini: action name -> key name (#77)
    std::vector<std::pair<std::string, std::string>> keyOverrides;
    // Shortcut profile: "windows" (default), "vim", or "custom" ([Keys])
    std::string keyProfile = "windows";
    // Reading column as a percentage of the window width (#82): 100 = full.
    // Windowed and fullscreen (zen) modes keep separate preferences.
    int readingWidthPct = 100;
    int zenWidthPct = 60;
    // Table of contents panel side: false = right (default), true = left
    bool tocOnLeft = false;
    // UI language id ("auto" follows the Windows display language; else a
    // registry id like "en"/"zh"/"de" — see i18n.h). Persisted as a string
    // because languages.ini languages have no stable numeric index.
    // Credit: multilingual groundwork by wxh-777 (PR #84).
    std::string language = "auto";
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
    // The first render target is software: it creates in ~20 ms while a
    // hardware one pays ~200 ms of D3D device + GPU driver initialization.
    // The warm-up thread flips this and the target is recreated on
    // WM_APP_GPU_READY, after the first frame is already on screen.
    bool useHardwareRT = false;

    // Cached dashed stroke for mermaid edges (factory object: created once,
    // survives render-target recreation)
    ID2D1StrokeStyle* dashedStrokeStyle = nullptr;

    // Editor paint-path line layouts, keyed by line start offset. Cleared on
    // any text/format/width/wrap change; between changes the 500 ms cursor
    // blink and idle repaints stop reshaping every visible line.
    struct EditorCachedLayout {
        IDWriteTextLayout* layout = nullptr;
        size_t len = 0;
    };
    std::unordered_map<size_t, EditorCachedLayout> editorLineLayoutCache;
    void clearEditorLineLayoutCache() {
        for (auto& [start, entry] : editorLineLayoutCache) {
            if (entry.layout) entry.layout->Release();
        }
        editorLineLayoutCache.clear();
    }
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
        bool pending = false;  // remote image download in flight
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
    // Nested inline spans: **bold *italic***, **`code`**, *`code`*
    IDWriteTextFormat* boldItalicFormat = nullptr;
    IDWriteTextFormat* codeBoldFormat = nullptr;
    IDWriteTextFormat* codeItalicFormat = nullptr;
    IDWriteTextFormat* codeBoldItalicFormat = nullptr;
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
    // Zen mode: borderless fullscreen with a centered reading column (F11)
    bool zenMode = false;
    WINDOWPLACEMENT zenRestorePlacement{};
    bool followSystemTheme = false;
    int lightThemeIndex = 0;
    int darkThemeIndex = 5;
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

    // Path header editing + new file/folder creation (#52)
    bool folderBrowserEditingPath = false;   // Path header is an edit box
    int folderBrowserNaming = 0;             // 0 = off, 1 = naming a new file, 2 = a new folder
    std::wstring folderBrowserInput;         // Single-line buffer shared by both inputs
    bool folderBrowserInputSelectAll = false; // Whole input selected: next keystroke replaces it
    bool folderInputJustOpened = false;      // Swallow the WM_CHAR of the key that opened it
    bool folderBrowserInputError = false;    // Last commit failed (bad path/name): red border

    // Right-click context menu overlay
    bool showContextMenu = false;
    // The mouse-up of a menu-item click must not reach the overlay handlers:
    // an action that opens the TOC or theme chooser would otherwise be
    // closed instantly by its own click's release landing "outside the panel"
    bool swallowNextMouseUp = false;
    float contextMenuX = 0.0f;        // Top-left, clamped into the window
    float contextMenuY = 0.0f;
    int hoveredContextMenuItem = -1;
    float contextMenuAnimation = 0.0f;

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

    // Settings overlay (Ctrl+,): section rail + rows of toggles/chips.
    // Hit rects are stored during render (print-preview pattern); each
    // entry pairs a rect with a SettingsAction id for the mouse-up test.
    bool showSettings = false;
    float settingsAnimation = 0.0f;
    int settingsSection = 0;  // 0 General, 1 Appearance, 2 Editor
    std::vector<std::pair<D2D1_RECT_F, int>> settingsHits;
    int readingWidthPct = 100;  // mirrors Settings (#82); 100 = full width
    int zenWidthPct = 60;
    int settingsDragSlider = 0;         // SET_SLIDER_* while dragging, else 0
    D2D1_RECT_F settingsSliderTrack[2]{};  // 0 window, 1 fullscreen (set in render)

    // Theme editor ("+ New" in settings): a working copy of a theme edited
    // through hex fields and a system-font list, previewed live in an ink
    // specimen. Saving goes through saveCustomTheme into themes.ini.
    bool showThemeEditor = false;
    D2DTheme themeEditorTheme{};        // working colors (string ptrs unused)
    std::wstring themeEditorName;
    std::wstring themeEditorFont;       // main font family
    std::wstring themeEditorHex[6];     // bg, text, heading, link, accent, code bg
    int themeEditorField = -1;          // focused field: 0..5 hex, 6 = name
    int themeEditorBase = 0;            // registry index the colors started from
    float themeEditorFontScroll = 0.0f;
    std::vector<std::wstring> systemFontFamilies;  // enumerated on first open
    std::vector<std::pair<D2D1_RECT_F, int>> themeEditorHits;
    D2D1_RECT_F themeEditorFontListRect{};  // for wheel routing (set in render)

    // UI language: languageSetting is the chosen registry index (-1 =
    // follow system), currentLanguageIndex is what tr() reads every call
    int languageSetting = -1;
    int currentLanguageIndex = 0;
    bool settingsLangOpen = false;      // language dropdown popup expanded
    D2D1_RECT_F settingsLangBox{};      // dropdown value box (set in render)

    // Shortcut profile state (mirrors Settings.keyProfile; the dropdown in
    // Settings switches it live)
    std::string keyProfile = "windows";
    bool settingsKeysOpen = false;      // profile dropdown popup expanded
    D2D1_RECT_F settingsKeysBox{};      // dropdown value box (set in render)

    // Resolved single-key bindings, indexed like KEY_ACTIONS (#77).
    // Filled by applyKeymap from settings; slots beyond the action count
    // stay zero.
    unsigned keymap[16] = {};

    // Reading position restore (#77): applied once enough of the document is
    // laid out for the target to be reachable (chunked layout grows
    // contentHeight, so applying immediately would clamp to a partial height)
    float pendingScrollRestore = -1.0f;

    // Print preview overlay. While it is open the document is held in print
    // layout — width, theme, zoom and scroll are hijacked by enterPrintLayout —
    // and the real screen state lives in printSaved. Screen dimensions and UI
    // scale must therefore be read from printSaved, not app.width/contentScale.
    struct PrintSavedView {
        int width = 0, height = 0;
        float contentScale = 1.0f, zoomFactor = 1.0f, appliedZoomFactor = 1.0f;
        float scrollX = 0, scrollY = 0, targetScrollX = 0, targetScrollY = 0;
        D2DTheme theme{};
        // Edit mode is suspended during print layout: the layout width comes
        // from documentViewportWidth, which in edit mode is the preview
        // pane — printing from the editor wrapped at half the page (#81)
        bool editMode = false;
    };
    bool showPrintPreview = false;
    PrintSavedView printSaved;
    std::vector<float> printPreviewBounds;    // page boundaries in doc Y (pages + 1)
    int printPreviewPage = 0;
    float printPreviewPageW = 794.0f;         // page size in DIPs (A4 fallback)
    float printPreviewPageH = 1123.0f;
    float printPreviewFit = 1.0f;             // page DIPs -> screen pixels
    std::vector<uint8_t> printPreviewPixels;  // current page, premultiplied BGRA
    unsigned printPreviewPxW = 0, printPreviewPxH = 0;
    int printPreviewPaper = -1;               // index into PRINT_PAPERS (-1: detect)
    bool printPreviewLandscape = false;
    D2D1_RECT_F printPreviewPrintBtn{};       // hit rects (set during render)
    D2D1_RECT_F printPreviewCancelBtn{};
    D2D1_RECT_F printPreviewPaperBtn[4]{};
    D2D1_RECT_F printPreviewOrientBtn[2]{};   // 0 portrait, 1 landscape

    // Blocks wider than the printable area (mermaid diagrams, wide tables)
    // are shrunk uniformly to fit the margins when printing; each band is a
    // vertical slice of the document drawn at `scale` about its own top-left
    struct PrintShrinkBand { float top, bottom, scale; };
    std::vector<PrintShrinkBand> printShrinkBands;

    // Table of contents overlay
    bool showToc = false;
    bool tocOnLeft = false;     // panel side (persisted)
    float tocAnimation = 0.0f;  // 0 to 1 slide-in from the chosen side
    struct HeadingInfo {
        std::wstring text;
        int level;       // 1-6
        float y;         // document Y coordinate
        std::string id;  // GitHub-style slug for anchor links
    };
    std::vector<HeadingInfo> headings;
    std::unordered_map<std::string, int> headingSlugCounts;
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
        // Index into layoutTextRuns of the run whose layout covers this
        // rect's doc range (SIZE_MAX = none; hit-testing interpolates).
        // Both vectors rebuild together on relayout, so the index is stable.
        size_t runIndex = (size_t)-1;
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

    // Folder-wide search: sibling .md files matching the current query,
    // filled by a worker thread and shown beside the search bar
    bool folderSearchEnabled = true;
    // B opens the folder browser with the path box focused (#81)
    bool browserFocusPath = false;
    int folderSearchGeneration = 0;
    struct FolderMatch {
        std::wstring snippet;
        size_t matchStart = 0;
        size_t matchLen = 0;
    };
    struct FolderFileResult {
        std::wstring fileName;
        std::wstring fullPath;
        std::vector<FolderMatch> matches;  // first few only
        int totalMatches = 0;
    };
    std::vector<FolderFileResult> folderResults;
    struct FolderResultHit {
        D2D1_RECT_F rect{};
        int fileIndex = -1;
    };
    std::vector<FolderResultHit> folderResultHits;  // rebuilt each paint

    // Text selection (#83): a pair of character offsets into docText.
    // Highlights and copies derive from the same range, and offsets survive
    // relayout (zoom, resize) where pixel rects would not.
    bool selecting = false;
    size_t selAnchor = 0;  // where the drag started
    size_t selFocus = 0;   // where the mouse is now
    bool hasSelection = false;
    std::wstring selectedText;
    // Dragging past the viewport edge scrolls (px per TIMER_SELECT_SCROLL
    // tick, sign = direction)
    float selAutoScrollVel = 0.0f;
    // Shift+click extends: finalize as a selection even without a drag
    bool selShiftExtend = false;

    // Multi-click selection (double/triple click)
    std::chrono::steady_clock::time_point lastClickTime;
    int clickCount = 0;
    int lastClickX = 0, lastClickY = 0;
    enum class SelectionMode { Normal, Word, Line } selectionMode = SelectionMode::Normal;
    // The word/line originally clicked; drag extension unions the current
    // word/line with this range
    size_t selAnchorRangeStart = 0, selAnchorRangeEnd = 0;

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
        // Cached polygon outline in LOCAL space (origin = rect top-left),
        // built lazily on first draw. Local space keeps it valid across the
        // exterior-lane x-shift that moves rects after layout.
        ID2D1PathGeometry* geometry = nullptr;
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
    // Clickable task checkboxes (document coordinates)
    struct TaskRect {
        D2D1_RECT_F rect{};
        size_t markOffset = SIZE_MAX;  // source offset of the [x]/[ ] mark char
        bool checked = false;
    };
    std::vector<TaskRect> taskRects;

    // Viewport width at the last layout: a change (folder browser toggle,
    // preview split drag) triggers a relayout in the render loop
    float lastViewportWidth = -1.0f;
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
    float editorScrollX = 0.0f;  // horizontal scroll, non-wrap mode only (#77)

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
        for (auto& shape : layoutShapes) {
            if (shape.geometry) {
                shape.geometry->Release();
            }
        }
        layoutTextRuns.clear();
        layoutRects.clear();
        layoutLines.clear();
        layoutShapes.clear();
        layoutConnectors.clear();
        layoutBitmaps.clear();
        taskRects.clear();
        linkRects.clear();
        codeBlocks.clear();
        textRects.clear();
        lineBuckets.clear();
        docText.clear();
        docTextLower.clear();
        headings.clear();
        headingSlugCounts.clear();
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
        if (dashedStrokeStyle) { dashedStrokeStyle->Release(); dashedStrokeStyle = nullptr; }
        if (deviceContext) { deviceContext->Release(); deviceContext = nullptr; }
        if (renderTarget) { renderTarget->Release(); renderTarget = nullptr; }
        if (fontFallback) { fontFallback->Release(); fontFallback = nullptr; }
        if (textAnalyzer) { textAnalyzer->Release(); textAnalyzer = nullptr; }
        if (textFormat) { textFormat->Release(); textFormat = nullptr; }
        if (headingFormat) { headingFormat->Release(); headingFormat = nullptr; }
        if (codeFormat) { codeFormat->Release(); codeFormat = nullptr; }
        if (boldFormat) { boldFormat->Release(); boldFormat = nullptr; }
        if (italicFormat) { italicFormat->Release(); italicFormat = nullptr; }
        if (boldItalicFormat) { boldItalicFormat->Release(); boldItalicFormat = nullptr; }
        if (codeBoldFormat) { codeBoldFormat->Release(); codeBoldFormat = nullptr; }
        if (codeItalicFormat) { codeItalicFormat->Release(); codeItalicFormat = nullptr; }
        if (codeBoldItalicFormat) { codeBoldItalicFormat->Release(); codeBoldItalicFormat = nullptr; }
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

// Folder browser panel width — shared by input hit-testing, the panel
// renderer, and the viewport shift
inline float folderBrowserPanelWidth(const App& app) {
    float cap = dpi(app, 300.0f);
    float floor_ = dpi(app, 250.0f);
    float w = app.width * 0.2f;
    if (w < floor_) w = floor_;
    if (w > cap) w = cap;
    return w;
}

// TOC panel width — shared by input hit-testing, the panel renderer, and
// the viewport shift
inline float tocPanelWidth(const App& app) {
    float cap = dpi(app, 280.0f);
    float floor_ = dpi(app, 220.0f);
    float w = app.width * 0.2f;
    if (w < floor_) w = floor_;
    if (w > cap) w = cap;
    return w;
}

inline float documentViewportX(const App& app) {
    if (!app.editMode) {
        // Side panels push the content aside instead of covering it; the
        // shift follows the panel's slide-in animation. A right-docked TOC
        // leaves x at 0 and only narrows the viewport.
        if (app.showFolderBrowser) {
            return folderBrowserPanelWidth(app) * app.folderBrowserAnimation;
        }
        if (app.showToc && app.tocOnLeft) {
            return tocPanelWidth(app) * app.tocAnimation;
        }
        return 0.0f;
    }
    // Preview hidden: zero-width viewport at the right edge — document
    // rendering flows through unchanged and clips to nothing
    if (!app.editorShowPreview) return static_cast<float>(app.width);
    return app.width * app.editorSplitRatio + 3.0f;
}

inline float documentViewportWidth(const App& app) {
    float width;
    if (app.editMode) {
        width = static_cast<float>(app.width) - documentViewportX(app);
    } else {
        width = static_cast<float>(app.width);
        // Snap to the panel's final width (not the animated position) so
        // the reading column relayouts once per toggle, not per frame
        if (app.showFolderBrowser) width -= folderBrowserPanelWidth(app);
        else if (app.showToc) width -= tocPanelWidth(app);
    }
    return width > 0.0f ? width : 0.0f;
}

// Cursor blink runs on a timer instead of per-frame InvalidateRect so the
// app is fully idle between blinks. Call after editMode/search state changes.
inline void updateBlinkTimer(App& app) {
    if (!app.hwnd) return;
    if (app.editMode || (app.showSearch && app.searchActive) ||
        (app.showFolderBrowser &&
         (app.folderBrowserEditingPath || app.folderBrowserNaming != 0))) {
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
