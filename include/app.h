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
#include <utility>

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
#define TIMER_LINK_PEEK 9
#define TIMER_UPDATE_CHECK 10

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

// Posted by the update-check worker with the latest release tag
// (lParam = std::string* "X.Y.Z", ownership transfers to the handler;
// empty string = the check failed and is retried another day)
#define WM_APP_UPDATE_CHECK (WM_APP + 5)

// Pandoc worker finished; wParam = success (pandoc.cpp)
#define WM_APP_PANDOC_DONE (WM_APP + 6)

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
    // Plain file launches join the existing window as a new tab (Win11
    // Notepad model); off restores one window per document
    bool openInTabs = true;
    // Last session's open tabs, restored on the next plain launch
    std::vector<std::string> sessionTabs;
    int sessionActive = 0;
    // Reading positions: most-recent-first, capped (#77). zoom = 0 means
    // "no per-document zoom": the document keeps the current zoom.
    struct ReadingPosition { std::string path; float scrollY; float zoom = 0.0f; };
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
    // Update check: portable builds ask GitHub for the latest release at
    // most once a day (Store installs update through the Store and never
    // check). dismissedUpdate remembers a version the user closed away.
    bool checkUpdates = true;
    std::string lastUpdateCheck;   // YYYY-MM-DD of the last completed check
    std::string dismissedUpdate;   // "X.Y.Z" the user dismissed
    // Recently opened files for the start page: most-recent-first,
    // capped at 10. when = FILETIME ticks of the last open.
    struct RecentFile { unsigned long long when = 0; std::string path; };
    std::vector<RecentFile> recentFiles;
    // Editor markdown assists (list continuation, Tab indent, Ctrl+B/I)
    // master switch
    bool editorAssists = true;
    // User-chosen pandoc executable ("" = auto-detect)
    std::string pandocPath;
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
        unsigned long long lastUsed = 0;
    };
    std::unordered_map<size_t, EditorCachedLayout> editorLineLayoutCache;
    unsigned long long editorLayoutUseClock = 0;
    static constexpr size_t EDITOR_LAYOUT_CACHE_MAX = 256;
    void clearEditorLineLayoutCache() {
        for (auto& [start, entry] : editorLineLayoutCache) {
            if (entry.layout) entry.layout->Release();
        }
        editorLineLayoutCache.clear();
        editorLayoutUseClock = 0;
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
        size_t bytes = 0;      // estimated decoded bitmap memory
        unsigned long long lastUsed = 0;
    };
    std::unordered_map<std::string, ImageEntry> imageCache;
    size_t imageCacheBytes = 0;
    unsigned long long imageCacheUseClock = 0;
    static constexpr size_t IMAGE_CACHE_BUDGET = 64ull * 1024ull * 1024ull;
    static constexpr size_t IMAGE_CACHE_MAX_ENTRIES = 256;

    void touchImageCacheEntry(ImageEntry& entry) {
        entry.lastUsed = ++imageCacheUseClock;
    }

    void storeImageCacheEntry(const std::string& key, ImageEntry entry) {
        entry.bytes = entry.bitmap && entry.width > 0 && entry.height > 0
            ? (size_t)entry.width * (size_t)entry.height * 4
            : 0;
        touchImageCacheEntry(entry);

        auto it = imageCache.find(key);
        if (it != imageCache.end()) {
            if (it->second.bitmap) it->second.bitmap->Release();
            imageCacheBytes -= it->second.bytes;
            it->second = std::move(entry);
            imageCacheBytes += it->second.bytes;
        } else {
            auto result = imageCache.emplace(key, std::move(entry));
            imageCacheBytes += result.first->second.bytes;
        }

        // Keep the just-stored key alive: layout may immediately use the
        // returned entry even when a single oversized image exceeds the cap.
        while (imageCache.size() > IMAGE_CACHE_MAX_ENTRIES ||
               imageCacheBytes > IMAGE_CACHE_BUDGET) {
            auto victim = imageCache.end();
            for (auto candidate = imageCache.begin(); candidate != imageCache.end(); ++candidate) {
                if (candidate->first == key || candidate->second.pending) continue;
                if (victim == imageCache.end() ||
                    candidate->second.lastUsed < victim->second.lastUsed) {
                    victim = candidate;
                }
            }
            if (victim == imageCache.end()) break;
            if (victim->second.bitmap) victim->second.bitmap->Release();
            imageCacheBytes -= victim->second.bytes;
            imageCache.erase(victim);
        }
    }

    // Layout bitmaps (document coordinates)
    struct LayoutBitmap {
        ID2D1Bitmap* bitmap = nullptr;
        D2D1_RECT_F destRect{};
    };
    std::vector<LayoutBitmap> layoutBitmaps;

    // Image lightbox: clicking an inline image views it full size over a
    // dimmed backdrop; wheel zooms, drag pans, Esc or a click closes
    bool showLightbox = false;
    ID2D1Bitmap* lightboxBitmap = nullptr;  // AddRef'd while open
    float lightboxZoom = 1.0f;              // multiplier over fit-to-view
    float lightboxPanX = 0.0f;
    float lightboxPanY = 0.0f;
    bool lightboxDragging = false;
    int lightboxDragStartX = 0;
    int lightboxDragStartY = 0;
    float lightboxDragPanX = 0.0f;
    float lightboxDragPanY = 0.0f;
    bool lightboxDragMoved = false;

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

    // Stats overlay: UI chrome, so it must not follow the document zoom
    IDWriteTextFormat* statsFormat = nullptr;

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

    // --- Tabbed interface (Win11 Notepad-style tabs in the title bar) ---
    // A tab keeps only what the document itself cannot restore: its path
    // and, when the user switches away mid-edit, the parked editor buffer.
    // View-mode scroll positions ride the existing reading-position memory.
    struct DocTab {
        int id = 0;                // stable identity across reorders/closes
        std::string path;          // empty = untitled quick note buffer
        std::wstring title;        // display name (file name / Untitled)
        bool editMode = false;     // switched away while editing
        bool editorDirty = false;
        bool fileMissing = false;  // deleted/renamed on disk (red-grey dot)
        FILETIME lastWrite{};      // for external-change conflicts (dirty tabs)
        std::wstring editorText;   // parked edit buffer (editMode only)
        float editorScrollY = 0.0f;
        size_t editorCursor = 0;
        bool wordWrap = true;
    };
    std::vector<DocTab> tabs;
    int activeTab = 0;
    bool showTabSwitcher = false;      // chevron dropdown (open-files list)
    int tabSwitcherHover = -1;
    int hoveredTab = -1;               // strip hover for close-button reveal
    int captionButtonHover = 0;        // 0 none, 1 min, 2 max, 3 close
    int captionButtonPressed = 0;
    bool tabNewTabIntent = false;      // Ctrl+T: next browser pick -> new tab

    // Caption button hit rects (client coords), refreshed by renderTabStrip
    D2D1_RECT_F tabStripRects[3] = {};  // min, max, close
    struct TabHit {
        D2D1_RECT_F rect{};
        int index = -1;    // tab index; -2 = plus, -3 = chevron
        D2D1_RECT_F closeRect{};
        bool hasClose = false;
    };
    std::vector<TabHit> tabHits;       // refreshed by renderTabStrip
    // Pin button in the title bar: keeps this window above every other
    // (per-window, not persisted)
    bool alwaysOnTop = false;

    // Update-available chip (bottom right): non-blocking, once per
    // session, absent entirely for Store installs
    bool updateCheckEnabled = true;
    bool updateAvailable = false;
    bool updateDismissed = false;   // this session
    std::string updateVersion;      // "X.Y.Z" offered by the chip
    std::string updateLastCheck;    // mirrors Settings on save
    std::string updateDismissedVersion;
    D2D1_RECT_F updateChipRect{};   // screen coords, zero while hidden
    D2D1_RECT_F updateCloseRect{};
    // Draft recovery: crash leftovers found at startup, offered via a chip
    std::vector<std::wstring> recoveredDrafts;
    D2D1_RECT_F draftChipRect{};    // screen coords, zero while hidden
    D2D1_RECT_F draftCloseRect{};
    std::vector<std::pair<D2D1_RECT_F, int>> tabSwitcherHits;
    // Closing a dirty tab routes through the unsaved-changes dialog; the
    // close completes from confirmExitAction once the user decides
    int pendingTabClose = -1;
    // Window close with dirty buffers: resolve them one dialog at a time,
    // then let the close proceed (Keep editing cancels it)
    bool pendingWindowClose = false;
    // Tab dragging: press arms a potential drag, moving past the threshold
    // starts reordering, leaving the strip vertically detaches the tab
    // into its own window
    int tabDragIndex = -1;       // pressed/dragged tab slot
    bool tabDragging = false;    // threshold crossed
    float tabDragOffsetX = 0.0f; // grab point within the tab
    int tabDragStartX = 0;
    int tabDragStartY = 0;
    // Pulled clear of the strip: the tab floats as a ghost card until
    // release decides (drop into another window / new window / snap back)
    bool tabDragDetached = false;
    HWND tabGhostWnd = nullptr;
    // A native move/size loop is in progress; the starting rect tells a
    // pure move (a possible strip drop) apart from a resize on exit
    bool windowMoveTracking = false;
    RECT windowMoveStartRect = {};
    // Right-click tab context menu (NPP-style close operations)
    bool showTabMenu = false;
    int tabMenuIndex = -1;          // tab the menu targets
    int tabMenuHover = -1;
    float tabMenuX = 0.0f;
    float tabMenuY = 0.0f;
    float tabMenuAnimation = 0.0f;
    // Bulk close (others / left / right) survives the per-tab
    // unsaved-changes dialogs by re-finding the kept tab by id
    int tabBulkCloseMode = 0;       // 0 none, 1 others, 2 left, 3 right
    int tabBulkKeepId = 0;
    int tabIdCounter = 0;           // DocTab.id source
    // Drag-out satellites keep the tab row even with a single tab
    bool forceTabStrip = false;
    // Quick-note empty state: "Open a file" button in the preview pane of
    // an untitled, still-empty buffer (rect refreshed by its renderer)
    D2D1_RECT_F quickNoteButtonRect = {};
    bool quickNoteButtonHover = false;
    // Editor pane scrollbar dragging (#121)
    bool editorScrollbarDragging = false;
    float editorScrollbarDragStartY = 0.0f;
    float editorScrollbarDragStartScroll = 0.0f;
    // Only the primary window persists the tab session; satellites
    // (--cascade/--new/drag-outs) neither restore nor overwrite it
    bool sessionOwner = true;
    // 16px window icon drawn in the strip (device bitmap: recreated with
    // the render target)
    ID2D1Bitmap* titleIconBitmap = nullptr;

    // Start page (design t7): the launcher that replaced the sample
    // document. It is the universal empty state: any tab with no
    // document and no edit buffer shows it, and Ctrl+T opens one fresh.
    // An embedded Learn document showing in place of the launcher;
    // cleared when anything else takes the view over
    bool startPageEmbeddedOpen = false;
    // Tracks appearance transitions so recents reload from settings.ini
    // each time the launcher comes back into view
    bool startPageShowing = false;
    struct RecentDoc { std::string path; unsigned long long when = 0; };
    std::vector<RecentDoc> startPageRecents;
    int startPageHover = 0;  // hit id under the mouse, 0 = none
    std::vector<std::pair<D2D1_RECT_F, int>> startPageHits;  // rebuilt each paint
    ID2D1Bitmap* startPageIconBitmap = nullptr;  // hero icon (device bitmap)

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
    // Session copy of the [Keys] overrides (the custom profile); the
    // shortcut editor edits these, WM_DESTROY writes them back
    std::vector<std::pair<std::string, std::string>> keyOverrides;
    // Shortcut editor overlay: click an action row, press its new key
    bool showShortcutEditor = false;
    int shortcutEditorRow = -1;         // row armed for key capture, -1 none
    std::vector<std::pair<D2D1_RECT_F, int>> shortcutHits;  // rebuilt each paint

    // Resolved single-key bindings, indexed like KEY_ACTIONS (#77).
    // Filled by applyKeymap from settings; slots beyond the action count
    // stay zero.
    unsigned keymap[16] = {};

    // Reading position restore (#77): applied once enough of the document is
    // laid out for the target to be reachable (chunked layout grows
    // contentHeight, so applying immediately would clamp to a partial height)
    float pendingScrollRestore = -1.0f;

    // Mouse back/forward navigation: link jumps push where they came from;
    // XBUTTON1/Alt+Left walks back, XBUTTON2/Alt+Right forward, each entry
    // restoring its document and scroll position
    struct NavEntry {
        std::string path;
        float scrollY = 0.0f;
    };
    std::vector<NavEntry> navBack;
    std::vector<NavEntry> navForward;

    // Review annotations (#126 experiment): notes stored as HTML comments
    // in the source markdown; rendered as tinted text plus a marker rail
    // beside the scrollbar. The file is the only model — every change is
    // written to disk and re-derived on the watcher reload.
    struct Annotation {
        std::string quote;        // whitespace-collapsed anchor (may elide " ... ")
        std::string note;         // raw markdown note
        size_t commentStart = 0;  // byte range of the comment in sourceText
        size_t commentEnd = 0;
        int commentLine = 0;      // 1-based source line of the comment
        int lineFrom = 0;         // reported selection lines (block above comment)
        int lineTo = 0;
        size_t docStart = (size_t)-1;  // anchor in docText; (size_t)-1 = unresolved
        size_t docEnd = (size_t)-1;
        bool anchorTried = false;
    };
    std::vector<Annotation> annotations;
    std::string sourceText;       // raw markdown of currentFile (viewer)
    int hoveredAnnotation = -1;
    bool annotEditorOpen = false;

    // Link peek: dwelling on a local .md link previews the target, fully
    // rendered at the live theme beside the link
    std::string linkPeekUrl;    // hoveredLink value the dwell timer armed for
    std::wstring linkPeekTitle;
    bool linkPeekActive = false;
    ID2D1Bitmap* linkPeekBitmap = nullptr;  // owned while active
    D2D1_RECT_F linkPeekAnchorDoc{};        // hovered link rect (doc coords)
    D2D1_RECT_F linkPeekPanel{};            // placed panel (screen coords)
    int annotEditorIndex = -1;    // -1 = creating a new annotation
    std::wstring annotEditorText;
    size_t annotEditorCaret = 0;
    size_t annotPendingInsert = (size_t)-1;  // source offset for the new comment line
    std::string annotPendingQuote;
    int annotPendingLineFrom = 0;
    int annotPendingLineTo = 0;
    // Rebuilt each frame by renderAnnotations (screen coords, hit-testing)
    struct AnnotationMark {
        D2D1_RECT_F square{};
        int index = -1;
        bool onText = false;  // level with visible text vs parked at an edge
    };
    std::vector<AnnotationMark> annotationMarks;
    D2D1_RECT_F annotCopyBtnRect{};  // zero-sized while the rail is hidden

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
    // Typed while the panel is open: case-insensitive substring filter
    std::wstring tocFilter;

    // Mouse
    bool mouseDown = false;
    int mouseX = 0;
    int mouseY = 0;

    // Vertical scrollbar
    bool scrollbarHovered = false;
    bool scrollbarDragging = false;
    bool verticalScrollbarVisible = false;
    float scrollbarContentHeight = 0.0f;
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
        std::wstring codeText;    // The code content (diagram source for diagrams)
        bool isDiagram = false;   // adds the copy-as-image button
        unsigned fitKey = 0;      // per-layout ordinal for the fit toggle
        bool fitCandidate = false;
        bool fitActive = false;
    };
    std::vector<CodeBlockInfo> codeBlocks;
    int hoveredCodeBlock = -1;

    // Tables - tracked for the copy-as-TSV button
    struct TableInfo {
        D2D1_RECT_F bounds{};  // document coordinates
        std::wstring tsv;      // cells joined by tabs, rows by newlines
        unsigned fitKey = 0;   // per-layout ordinal for the fit toggle
        bool fitCandidate = false;  // wider than the column at natural size
        bool fitActive = false;
    };
    std::vector<TableInfo> tableRects;
    int hoveredTable = -1;

    // Fit-to-width overrides for oversized tables/diagrams, keyed by their
    // per-layout ordinal (cleared when the document changes)
    std::vector<unsigned> fitBlocks;
    unsigned layoutTableSeq = 0;
    unsigned layoutDiagramSeq = 0;

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
    // Plain file launches join this window as tabs (settings toggle)
    bool openInTabs = true;
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
        // Arbitrary geometry built eagerly at layout time (pie slices,
        // arrowheads, crow's feet); `geometry` is in local space with the
        // origin at rect top-left, rect doubles as the culling bounds
        Path,
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

    // Plain-text .md reference existence per resolved path (#127); cleared
    // with the layout so reload and file changes re-check the disk
    std::unordered_map<std::string, bool> fileRefCache;

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
    const char* copiedNotificationKey = "toast.copied";
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
    bool confirmExitPending = false;  // Unsaved-changes dialog is showing
    // Dialog open time: keys arriving within the grace window are ignored
    // so ESC-smashing lands on a visible, stable dialog
    std::chrono::steady_clock::time_point confirmExitOpenedAt;
    std::vector<std::pair<D2D1_RECT_F, int>> confirmExitHits;  // rebuilt each paint

    // Create-missing-reference dialog: a clicked ghost ref offers to
    // create its target and start typing
    bool createRefPending = false;
    std::string createRefPath;  // resolved absolute target
    std::vector<std::pair<D2D1_RECT_F, int>> createRefHits;  // rebuilt each paint
    // WM_CHARs arriving in this window are dropped so the keystroke that
    // resolved the dialog cannot type into the editor it just opened
    // (one keydown can yield more than one WM_CHAR)
    std::chrono::steady_clock::time_point swallowCharsUntil{};

    // Editor notification
    bool showEditModeNotification = false;
    float editModeNotificationAlpha = 0;
    std::chrono::steady_clock::time_point editModeNotificationStart;
    std::wstring editorNotificationMsg;

    // Pandoc bridge (pandoc.cpp): resolved executable, the user's
    // settings override, and the single in-flight conversion guard
    std::wstring pandocExe;
    std::wstring pandocUserPath;
    bool pandocChecked = false;
    bool pandocRunning = false;

    // Unified editor (design t11): one raw buffer, live render beside it;
    // the left tool rail slides in with edit mode carrying the controls
    bool editorAssists = true;
    float editRailAnim = 0.0f;       // rail slide-in 0..1
    int editRailHover = 0;           // hit id under the mouse, 0 = none
    std::vector<std::pair<D2D1_RECT_F, int>> editRailHits;  // rebuilt each paint

    // Raw editor insert menu (design t9): right-click drops markdown at
    // the caret; Table and Diagram open flyout submenus. The rail's
    // table/diagram buttons open the same submenus standalone.
    bool editCtxOpen = false;
    bool editCtxRailOnly = false;  // submenu only, anchored at the rail
    float editCtxX = 0.0f;
    float editCtxY = 0.0f;
    int editCtxHover = 0;
    int editCtxSub = 0;       // 0 none, 1 table grid, 2 diagram templates
    int editCtxSubHover = 0;
    int editCtxGridC = 0;     // hovered table size
    int editCtxGridR = 0;
    std::vector<std::pair<D2D1_RECT_F, int>> editCtxHits;     // rebuilt each paint
    std::vector<std::pair<D2D1_RECT_F, int>> editCtxSubHits;

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

    // Editor find & replace (#121): Ctrl+H adds a replace row to search
    bool searchReplaceMode = false;
    std::wstring replaceText;
    bool replaceFieldActive = false;    // keyboard goes to the replace row
    // Hit rects refreshed by renderSearchOverlay: 1 query field,
    // 2 replace field, 3 Replace button, 4 Replace-all button
    std::vector<std::pair<D2D1_RECT_F, int>> searchReplaceHits;

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
    // Slim in-editor line-number gutter (design t11)
    IDWriteTextFormat* editorGutterFormat = nullptr;

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
        tableRects.clear();
        textRects.clear();
        lineBuckets.clear();
        docText.clear();
        docTextLower.clear();
        headings.clear();
        headingSlugCounts.clear();
        fileRefCache.clear();
        for (auto& a : annotations) {
            a.docStart = a.docEnd = (size_t)-1;
            a.anchorTried = false;  // re-anchor against the fresh docText
        }
    }

    void releaseOverlayFormats() {
        if (searchTextFormat) { searchTextFormat->Release(); searchTextFormat = nullptr; }
        if (themeTitleFormat) { themeTitleFormat->Release(); themeTitleFormat = nullptr; }
        if (themeHeaderFormat) { themeHeaderFormat->Release(); themeHeaderFormat = nullptr; }
        if (folderBrowserFormat) { folderBrowserFormat->Release(); folderBrowserFormat = nullptr; }
        if (tocFormat) { tocFormat->Release(); tocFormat = nullptr; }
        if (tocFormatBold) { tocFormatBold->Release(); tocFormatBold = nullptr; }
        if (statsFormat) { statsFormat->Release(); statsFormat = nullptr; }
        if (supSubFormat) { supSubFormat->Release(); supSubFormat = nullptr; }
        if (editorTextFormat) { editorTextFormat->Release(); editorTextFormat = nullptr; }
        if (editorGutterFormat) { editorGutterFormat->Release(); editorGutterFormat = nullptr; }
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
        imageCacheBytes = 0;
        imageCacheUseClock = 0;
        if (titleIconBitmap) {
            titleIconBitmap->Release();
            titleIconBitmap = nullptr;
        }
        if (startPageIconBitmap) {
            startPageIconBitmap->Release();
            startPageIconBitmap = nullptr;
        }
    }

    void shutdown() {
        if (lightboxBitmap) {
            lightboxBitmap->Release();
            lightboxBitmap = nullptr;
        }
        if (linkPeekBitmap) {
            linkPeekBitmap->Release();
            linkPeekBitmap = nullptr;
        }
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
// Desk gap (design 10a): the sliver of desk between the source column
// and the floating render sheet; it doubles as the split drag handle.
// Its center sits at width * editorSplitRatio.
inline float editSeamWidth(const App& app) {
    return dpi(app, 16.0f);
}

inline float editorPaneWidth(const App& app) {
    return app.editorShowPreview
        ? app.width * app.editorSplitRatio - editSeamWidth(app) * 0.5f
        : static_cast<float>(app.width);
}

// Left tool rail (design t8/t11): slides in with edit mode, carries the
// formatting controls
inline float editRailWidth(const App& app) {
    if (!app.editMode) return 0.0f;
    return dpi(app, 48.0f) * app.editRailAnim;
}

// Slim in-editor line-number column right of the rail (design t11)
inline float editorGutterWidth(const App& app) {
    return dpi(app, 34.0f);
}

inline bool editorWrapOn(const App& app) {
    return app.editorWordWrap;
}

// Folder browser panel width — shared by input hit-testing, the panel
// renderer, and the viewport shift
// Title-bar strip height: the custom caption hosting the tab strip. Zero
// in zen mode (borderless fullscreen hides all chrome).
inline float chromeTopHeight(const App& app) {
    if (app.zenMode) return 0.0f;
    return dpi(app, 40.0f);
}

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
    if (!app.editorShowPreview) {
        return static_cast<float>(app.width);
    }
    return app.width * app.editorSplitRatio + editSeamWidth(app) * 0.5f;
}

inline bool editorPreviewVisible(const App& app) {
    return app.editMode && app.editorShowPreview;
}

// Floating render sheet (design 10a): the page lies on the editor's
// desk and rises past the tab strip to the window's top edge — the
// caption buttons float over it as an island. Shadow is the only
// separator.
inline D2D1_RECT_F editSheetRect(const App& app) {
    return D2D1::RectF(documentViewportX(app), dpi(app, 10.0f),
                       (float)app.width - dpi(app, 16.0f),
                       (float)app.height - dpi(app, 14.0f));
}

inline D2D1_COLOR_F editSurfaceMix(D2D1_COLOR_F c, float to, float t) {
    c.r += (to - c.r) * t;
    c.g += (to - c.g) * t;
    c.b += (to - c.b) * t;
    c.a = 1.0f;
    return c;
}

// The desk both panes sit on: lifted a shade off the window in the dark,
// dimmed a touch in the light so the sheet reads as paper on top
inline D2D1_COLOR_F editDeskColor(const App& app) {
    return app.theme.isDark
               ? editSurfaceMix(app.theme.background, 1.0f, 0.03f)
               : editSurfaceMix(app.theme.background, 0.0f, 0.045f);
}

inline D2D1_COLOR_F editSheetColor(const App& app) {
    return app.theme.isDark
               ? editSurfaceMix(app.theme.background, 1.0f, 0.065f)
               : editSurfaceMix(app.theme.background, 1.0f, 0.35f);
}

inline float documentViewportWidth(const App& app) {
    float width;
    if (app.editMode) {
        width = static_cast<float>(app.width) - documentViewportX(app);
        // The floating sheet is inset from the window's right edge
        if (editorPreviewVisible(app)) width -= dpi(app, 16.0f);
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
