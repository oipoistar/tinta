// Sheets in the window: the canvas a .smd (or an Obsidian .canvas) shows
// in its tab instead of a page. Drawn with Direct2D like the rest of
// Tinta; a note card's content is the note laid out by the document
// layout into a cached bitmap, redrawn only when the note, its size, the
// theme or the zoom bucket changes.
#pragma once

#include "app.h"
#include "sheet.h"

#include <d2d1.h>
#include <wrl/client.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// A text box inside the sheet: a card's note, a label, an address, the
// find field. Multi-line for notes; single-line ones commit on Enter.
struct SheetTextEdit {
    std::wstring text;
    size_t caret = 0, anchor = 0;
    bool multiline = true;
    float preferredX = -1.0f;  // for Up/Down, in layout units
    struct Snapshot { std::wstring text; size_t caret = 0, anchor = 0; };
    std::vector<Snapshot> undo, redo;
    unsigned long long lastTypeMs = 0;  // typing within a pause is one undo step
    float scrollY = 0.0f;               // layout units the text is scrolled by
    bool changed = false;               // since the last write
    bool selectingWithMouse = false;
    size_t clickCount = 0;
    unsigned long long lastClickMs = 0;

    bool hasSelection() const { return caret != anchor; }
    size_t selStart() const { return caret < anchor ? caret : anchor; }
    size_t selEnd() const { return caret < anchor ? anchor : caret; }
};

// What the sheet knows about a node beyond its JSON: its note, the
// figures its card shows and its content drawn for the current zoom
struct SheetCard {
    enum class Kind { Note, Image, Other, Missing, Blocked };
    Kind kind = Kind::Other;
    std::wstring path;                   // File nodes: the file on disk
    unsigned long long stamp = 0;        // its write time and size, as seen
    std::string source;                  // a note's text (capped)
    std::string title;                   // what the card is called
    int tasksDone = 0, tasksTotal = 0, words = 0;
    // What its text links to: a note's full path, or "*" and a note name
    // for a [[wiki]] link, lowercased; matched to cards by ghostLinks
    std::vector<std::wstring> linkKeys;

    // The content drawn for one zoom: a slice of the laid-out note
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    float bmScale = 0;                   // pixels per sheet unit
    float bmWidth = 0;                   // the width it was laid out at
    float bmTop = 0, bmHeight = 0;       // the slice of the content it holds
    unsigned bmGeneration = 0;           // render target it belongs to
    unsigned long long bmKey = 0;        // note, theme and fill it shows
    float contentHeight = 0;             // the whole laid-out note
    float scroll = 0;                    // scrolled inside the card
    bool layoutFailed = false;
    // Text drawn every frame (title, figures, name), kept while its words,
    // face and box stay the same: panning makes no layouts
    struct TextCache {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        std::wstring text;
        IDWriteTextFormat* format = nullptr;
        float width = 0, height = 0;
    } texts[3];
};

struct SheetDoc {
    std::string path;          // the sheet file (UTF-8)
    std::wstring file;
    std::wstring dir;          // the folder the sheet file is in
    std::wstring base;         // file nodes are relative to this folder
    std::wstring cardDir;      // new notes go here
    bool canvasFile = false;   // an Obsidian .canvas: JSON only, paths from the vault
    bool readOnly = false;     // could not be parsed: shown, never written
    sheet::Sheet model;
    sheet::SmdInfo info;
    std::string loadedSource;  // the text the model was read from or saved as
    bool dirty = false;        // a save is pending
    std::string loadError;

    // Camera: the sheet point at the viewport's centre, and the zoom
    float camX = 0, camY = 0, zoom = 1.0f;
    bool placed = false;
    float savedCamX = 1e30f, savedCamY = 1e30f, savedZoom = 0;  // the view as last written
    struct Flight {
        bool active = false;
        float fromX = 0, fromY = 0, fromZoom = 1, toX = 0, toY = 0, toZoom = 1;
        unsigned long long startMs = 0;
        float durationMs = 0;
    } flight;
    // Cards gliding to new places after a tidy or an undo
    struct Glide {
        bool active = false;
        unsigned long long startMs = 0;
        std::unordered_map<std::string, sheet::Box> from;
    } glide;
    unsigned long long zoomChangedMs = 0;  // card content waits for the zoom to settle

    // Selection and hover
    std::vector<std::string> selected;  // node ids
    std::string selectedEdge;
    std::string hoverNode, hoverEdge, hoverGhostFrom, hoverGhostTo;
    int hoverUi = 0;                    // toolbar or chip under the pointer

    // A press and what it is doing
    enum class Gesture { None, Pending, Pan, Marquee, Move, Resize, Connect, Minimap, TrayDrag, EditSelect };
    Gesture gesture = Gesture::None;
    bool pressed = false;               // the press under way began on the sheet
    int pressX = 0, pressY = 0;         // client pixels
    float pressSX = 0, pressSY = 0;     // sheet units
    float lastSX = 0, lastSY = 0;
    float panCamX = 0, panCamY = 0;
    bool spaceHeld = false;
    bool middlePan = false;
    std::string pressNode;
    bool pressWasSelected = false;
    bool pressShift = false, pressCtrl = false, pressAlt = false;
    std::vector<std::pair<std::string, sheet::Box>> moveStart;
    int resizeEdges = 0;                // 1 left, 2 top, 4 right, 8 bottom
    sheet::Box resizeStart;
    sheet::Side connectSide = sheet::Side::None;
    std::string connectTarget;
    sheet::Side connectTargetSide = sheet::Side::None;
    std::vector<float> guidesX, guidesY;
    std::wstring trayDragFile;
    bool gestureMoved = false;
    unsigned long long lastClickMs = 0;
    int lastClickX = -100, lastClickY = -100;

    // Editing in place
    enum class Edit { None, Card, EdgeLabel, GroupLabel, Url, LaneKey, Find };
    Edit editing = Edit::None;
    std::string editId;
    SheetTextEdit edit;
    std::string editOriginal;            // what the file or field held when editing began
    unsigned long long editSavedMs = 0;

    // Undo: whole-model snapshots with the file changes made in the step
    struct FileChange {
        enum Kind { Created, Edited, Renamed, Deleted } kind = Edited;
        std::wstring path, otherPath;    // Renamed: path is the new name, otherPath the old
        std::string before, after;
    };
    struct Step {
        sheet::Sheet model;
        std::vector<std::string> selected;
        std::vector<FileChange> files;
    };
    std::vector<Step> undo, redo;
    std::vector<FileChange> pendingFiles;  // file changes for the step being made

    // Features
    bool showMinimap = true, showGhosts = true;
    bool spotlight = false;
    std::string spotlightId;
    bool presenting = false;
    std::vector<std::string> slides;    // node ids: cards and groups
    int slide = 0;
    float presentReturnX = 0, presentReturnY = 0, presentReturnZoom = 1;
    bool finding = false;
    std::vector<std::string> found;
    int foundIndex = -1;
    bool trayOpen = false;
    float trayScroll = 0;
    std::vector<std::wstring> unplaced;  // notes in the folder not on the sheet
    unsigned long long lastPollMs = 0;
    std::unordered_map<std::string, SheetCard> cards;
    // Note cards this sheet made this session. Kept with the sheet, not its
    // cards: a card's runtime state goes when a reload misses its node.
    std::unordered_set<std::string> madeHere;
    // Cards by their note's lowercased path and name, kept while the sheet's files stay the same
    size_t linkIndexKey = 0;
    std::unordered_map<std::wstring, std::string> cardByPath, cardByName;

    // Context menu
    struct MenuItem {
        int action = 0;
        std::wstring label, keys;
        bool enabled = true;
        bool separator = false;
        bool swatches = false;           // a row of colour swatches
    };
    bool menuOpen = false;
    float menuX = 0, menuY = 0;
    std::vector<MenuItem> menu;
    int menuHover = -1;
    float menuSheetX = 0, menuSheetY = 0;  // where the menu was opened, on the sheet
    std::vector<std::pair<D2D1_RECT_F, int>> menuHits;  // item index, or 1000 + swatch

    // Drawn each frame for hit testing (client pixels)
    std::vector<std::pair<D2D1_RECT_F, int>> uiHits;
    D2D1_RECT_F minimapRect{};
    float minimapScale = 1, minimapOriginX = 0, minimapOriginY = 0;
    D2D1_RECT_F trayRect{};
    std::vector<std::pair<D2D1_RECT_F, std::wstring>> trayRows;
    D2D1_RECT_F findRect{};
    std::wstring tip;                   // the toolbar button's name under the pointer
};

// Sheet files
bool isSheetPath(const std::string& path);
bool isSheetPath(const std::wstring& path);
// The active tab shows a sheet (its source is not being edited)
bool sheetActive(const App& app);
SheetDoc* activeSheet(App& app);

// Frame
void sheetPrepareFrame(App& app);  // before BeginDraw: card content for this frame
void renderSheet(App& app);

// Input; each returns true when the sheet took it
bool sheetMouseDown(App& app, HWND hwnd, int x, int y);
bool sheetPressed(App& app);  // a press that began on the sheet is under way
bool sheetMouseMove(App& app, HWND hwnd, int x, int y);
bool sheetMouseUp(App& app, HWND hwnd, int x, int y);
bool sheetMiddleDown(App& app, HWND hwnd, int x, int y);
bool sheetMiddleUp(App& app, HWND hwnd, int x, int y);
bool sheetMouseWheel(App& app, HWND hwnd, float delta, bool horizontal, int x, int y);
bool sheetContextMenu(App& app, HWND hwnd, int x, int y);
// handled: whether the sheet took the key; the return value is what
// handleKeyDown returns (true keeps the key's character from being typed)
bool sheetKeyDown(App& app, HWND hwnd, WPARAM key, bool& handled);
bool sheetKeyUp(App& app, HWND hwnd, WPARAM key);
bool sheetCharInput(App& app, HWND hwnd, wchar_t ch);
bool sheetDropFiles(App& app, HWND hwnd, const std::vector<std::wstring>& files, int x, int y);
void sheetCaptureLost(App& app, HWND hwnd);
void sheetTimer(App& app, HWND hwnd);
void sheetImagesArrived(App& app);   // a remote or PlantUML image landed: notes redraw
void sheetLeave(App& app);           // the tab is switching away: commit and save
void sheetSetCursor(App& app, int x, int y);

// The New sheet flow: a Save dialog names the sheet; its notes go in a
// folder of the same name beside it (an existing folder's notes are
// placed on the new sheet). Opens the sheet in this tab or a new one.
bool sheetCreateInteractive(App& app, HWND hwnd);
// Makes the sheet at `path` and opens it; an empty tab gives it its place
bool sheetCreateAndOpen(App& app, HWND hwnd, const std::wstring& path);
// Creates the sheet file and folder; false when the file could not be written
bool sheetCreateAt(App& app, const std::wstring& sheetPath, std::string* error = nullptr);

// --- Shared inside the sheet sources -------------------------------------

namespace sheetui {
constexpr unsigned SHEET_TIMER_ID = 23;
constexpr float kGrid = 20.0f;

float scale(const App& app, const SheetDoc& doc);      // pixels per sheet unit
D2D1_RECT_F viewRect(const App& app);                  // the canvas, in client pixels
D2D1_POINT_2F toScreen(const App& app, const SheetDoc& doc, float x, float y);
D2D1_POINT_2F toSheet(const App& app, const SheetDoc& doc, float x, float y);
D2D1_RECT_F screenRect(const App& app, const SheetDoc& doc, const sheet::Box& box);
sheet::Box drawBox(const SheetDoc& doc, const sheet::Node& node);  // with the glide applied
unsigned long long nowMs();

SheetCard& card(App& app, SheetDoc& doc, const sheet::Node& node);
void refreshCard(App& app, SheetDoc& doc, const sheet::Node& node, bool force = false);
std::string cardTitle(App& app, SheetDoc& doc, const sheet::Node& node);
D2D1_COLOR_F presetColor(const App& app, const std::string& color, bool* has = nullptr);
bool isSelected(const SheetDoc& doc, const std::string& id);
std::vector<int> selectedIndexes(const SheetDoc& doc);
void markChanged(App& app, SheetDoc& doc);
void beginStep(SheetDoc& doc);                 // snapshot for undo before a change
void commitStep(App& app, SheetDoc& doc);      // the change is made: save soon
void undoStep(App& app, SheetDoc& doc, bool redo);
void saveNow(App& app, SheetDoc& doc);
void flyTo(SheetDoc& doc, float x, float y, float zoom, float ms = 420.0f);
void fitBox(App& app, SheetDoc& doc, const sheet::Box& box, bool animate, float maxZoom = 1.0f);
void ensureTimer(App& app);
void hint(App& app, const wchar_t* text);
// Notes: create a note card at a place (text optional); returns its id
std::string newNoteCard(App& app, SheetDoc& doc, float x, float y, const std::string& text = {},
                        float width = 360, float height = 240);
std::string writeNoteFile(App& app, SheetDoc& doc, const std::wstring& stem, const std::string& text,
                          std::wstring* created = nullptr);
std::string relativePath(const SheetDoc& doc, const std::wstring& absolute);
std::wstring absolutePath(const SheetDoc& doc, const std::string& relative);
bool readFileUtf8(const std::wstring& path, std::string& out, size_t cap = 0);
bool writeFileUtf8(const std::wstring& path, const std::string& text);
void startEdit(App& app, SheetDoc& doc, SheetDoc::Edit what, const std::string& id);
// Writes what the text box holds; ends editing unless `stay`
void commitEdit(App& app, SheetDoc& doc, bool stay = false);
void cancelEdit(App& app, SheetDoc& doc);
void applyLanes(App& app, SheetDoc& doc, const std::vector<std::string>& ids);
void pollFolder(App& app, SheetDoc& doc, bool force);
void runAction(App& app, HWND hwnd, SheetDoc& doc, int action);
void openMenu(App& app, SheetDoc& doc, float x, float y);
void closeMenu(SheetDoc& doc);
void updateFind(App& app, SheetDoc& doc);
void startPresenting(App& app, SheetDoc& doc);
void showSlide(App& app, SheetDoc& doc, int index);
void stopPresenting(App& app, SheetDoc& doc);
void toggleSpotlight(App& app, SheetDoc& doc);
std::vector<std::pair<std::string, std::string>> ghostLinks(App& app, SheetDoc& doc);
// Whether a ghost is drawn: all of them on a sheet with a few, else the
// ones of the cards under the pointer or selected
bool ghostShown(const SheetDoc& doc, size_t count, const std::pair<std::string, std::string>& ghost);
bool editRect(App& app, SheetDoc& doc, D2D1_RECT_F& rect, float& textScale);  // where the edit box draws
void renderCardBitmaps(App& app, SheetDoc& doc, unsigned long long budgetMs);
// Tool and menu actions
enum Action {
    ACT_NONE = 0,
    ACT_NEW_NOTE = 1, ACT_ADD_FILES, ACT_ADD_LINK, ACT_ADD_GROUP, ACT_TIDY, ACT_PRESENT,
    ACT_COMPILE, ACT_GHOSTS, ACT_MINIMAP, ACT_ZOOM_IN, ACT_ZOOM_OUT, ACT_ZOOM_RESET, ACT_ZOOM_FIT,
    ACT_EDIT, ACT_OPEN_TAB, ACT_GROUP_SELECTION, ACT_UNGROUP, ACT_DUPLICATE, ACT_REMOVE,
    ACT_FRONT, ACT_BACK, ACT_COPY_MERMAID, ACT_PASTE, ACT_SELECT_ALL, ACT_EDGE_LABEL,
    ACT_EDGE_FORWARD, ACT_EDGE_BACKWARD, ACT_EDGE_BOTH, ACT_EDGE_NONE, ACT_GROUP_RENAME,
    ACT_LANE, ACT_SORT_LANES, ACT_TO_FILE, ACT_SPOTLIGHT, ACT_FIND, ACT_TRAY, ACT_PLACE_ALL,
    ACT_ZOOM_SELECTION, ACT_SHOW_FOLDER, ACT_UNDO, ACT_REDO, ACT_TIDY_DOWN,
    ACT_PRESENT_PREV, ACT_PRESENT_NEXT, ACT_PRESENT_EXIT, ACT_FIND_CLOSE, ACT_FIT_HEIGHT,
    ACT_ALIGN_LEFT, ACT_ALIGN_CENTER, ACT_ALIGN_RIGHT, ACT_ALIGN_TOP, ACT_ALIGN_MIDDLE,
    ACT_ALIGN_BOTTOM, ACT_DISTRIBUTE_H, ACT_DISTRIBUTE_V, ACT_REANCHOR,
    ACT_LINKS_TO_ARROWS,
    ACT_COLOR_BASE = 100,  // + 0 none, 1..6 presets
    ACT_TRAY_ROW = 200,    // + row
};

// Connections: a cubic curve leaving and entering by the boxes' sides
struct Curve {
    D2D1_POINT_2F p0{}, p1{}, p2{}, p3{};
    D2D1_POINT_2F n0{}, n3{};  // the sides' outward normals
};
Curve edgeCurve(const sheet::Box& from, const sheet::Box& to, sheet::Side fromSide, sheet::Side toSide);
Curve edgeCurveFor(const SheetDoc& doc, const sheet::Edge& edge);
D2D1_POINT_2F bezier(const Curve& curve, float t);
D2D1_POINT_2F sidePoint(const sheet::Box& box, sheet::Side side);
// Distance in sheet units from a point to a curve
float curveDistance(const Curve& curve, float x, float y, float* at = nullptr);
// The node under a sheet point (front first): cards before groups; -1 none
int nodeAt(const SheetDoc& doc, float x, float y, bool groupsToo = true);
// A group's label box, in sheet units
sheet::Box groupLabelBox(App& app, const SheetDoc& doc, const sheet::Node& group);
// The text box's layout while editing (owned by the edit state; do not release)
IDWriteTextLayout* editLayout(App& app, SheetDoc& doc, float width);
float editFontSize(const SheetDoc& doc);
float cardPadding();
}  // namespace sheetui

void sheetSaveView(SheetDoc& doc);
