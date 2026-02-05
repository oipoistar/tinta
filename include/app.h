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
enum class SyntaxTokenType { Plain, Keyword, String, Comment, Number, Function, TypeName, Operator };

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
    std::vector<LayoutTextRun> layoutTextRuns;
    std::vector<LayoutRect> layoutRects;
    std::vector<LayoutLine> layoutLines;
    bool layoutDirty = true;

    // Search match layout mapping (document Y for each match)
    std::vector<float> searchMatchYs;
    size_t searchMatchCursor = 0;

    // Copied notification (fades out over 2 seconds)
    bool showCopiedNotification = false;
    float copiedNotificationAlpha = 0.0f;
    std::chrono::steady_clock::time_point copiedNotificationStart;

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
        layoutBitmaps.clear();
        linkRects.clear();
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

#endif // TINTA_APP_H
