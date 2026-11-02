#include "d2d_init.h"
#include "editor.h"
#include "editrail.h"
#include "i18n.h"
#include "input.h"
#include "render.h"
#include "search.h"
#include "settings.h"
#include "utils.h"
#include "clipboard_snapshot.h"
#include <iostream>
#include <memory>
#include <windowsx.h>

namespace {
int failures = 0, cases = 0;
void check(bool ok, const char* message) {
    if (!ok) { if (failures < 30) std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
void key(App& app, unsigned vk, bool shift = false, bool ctrl = false) {
    BYTE original[256], pressed[256];
    GetKeyboardState(original); memcpy(pressed,original,sizeof pressed);
    pressed[VK_CONTROL] = ctrl ? 0x80 : 0;
    pressed[VK_SHIFT] = shift ? 0x80 : 0;
    pressed[VK_MENU] = 0;
    SetKeyboardState(pressed); handleKeyDown(app,app.hwnd,vk); SetKeyboardState(original);
}
D2D1_POINT_2F caretAt(App& app, size_t pos, bool upstream = false) {
    auto cursor = app.editorCursorPos, affinity = app.editorCaretUpstreamPos;
    app.editorCursorPos = pos;
    app.editorCaretUpstreamPos = upstream ? pos : std::wstring::npos;
    D2D1_POINT_2F point{};
    check(editorCaretPoint(app,point), "caret geometry is available");
    app.editorCursorPos = cursor; app.editorCaretUpstreamPos = affinity;
    return point;
}
D2D1_POINT_2F characterPoint(App& app, size_t pos, float fraction = 0.5f) {
    auto start = caretAt(app,pos), end = caretAt(app,pos+1,true);
    return {start.x + (end.x-start.x)*fraction, start.y+app.editorTextFormat->GetFontSize()*0.75f};
}
void left(App& app, D2D1_POINT_2F point) {
    handleMouseDown(app,app.hwnd,MK_LBUTTON,MAKELPARAM((int)point.x,(int)point.y));
    handleMouseUp(app,app.hwnd,0,MAKELPARAM((int)point.x,(int)point.y));
}
void doubleClick(App& app, D2D1_POINT_2F point) {
    app.lastClickTime = {}; app.clickCount = 0;
    left(app,point); left(app,point);
}
void right(App& app, D2D1_POINT_2F point) {
    POINT screen{(LONG)point.x,(LONG)point.y}; ClientToScreen(app.hwnd,&screen);
    handleContextMenu(app,app.hwnd,MAKELPARAM(screen.x,screen.y));
    app.renderTarget->BeginDraw();
    renderEditor(app,editorPaneWidth(app)); renderEditCtxMenu(app);
    check(SUCCEEDED(app.renderTarget->EndDraw()), "selection and editor menu render");
    check(app.editCtxOpen, "right-click opens the editor context menu");
}
bool enabled(const App& app, int action) {
    for (const auto& hit : app.editCtxHits) if (hit.second == action) return true;
    return false;
}
bool clipboardEquals(App& app, const std::wstring& expected) {
    // Clipboard observers can briefly lock it after a copy notification.
    for (int i=0;i<20;++i) {
        if (clipboardLine(app.hwnd) == expected) return true;
        Sleep(10);
    }
    return false;
}
std::wstring oleClipboardText() {
    // Reads the clipboard the way OLE applications such as Word paste it.
    // OLE fails at once with CLIPBRD_E_CANT_OPEN while another program has
    // the clipboard open, as Remote Desktop's rdpclip and clipboard history
    // do just after each change, so the read is retried for a second.
    std::wstring text; HRESULT result = E_FAIL;
    for (int attempt=0;attempt<50;++attempt) {
        if (attempt) Sleep(20);
        Microsoft::WRL::ComPtr<IDataObject> clipboard;
        FORMATETC format{CF_UNICODETEXT,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL}; STGMEDIUM medium{};
        if (SUCCEEDED(result = OleGetClipboard(clipboard.GetAddressOf())) && SUCCEEDED(result = clipboard->GetData(&format,&medium))) {
            if (auto chars = static_cast<const wchar_t*>(GlobalLock(medium.hGlobal))) { text = chars; GlobalUnlock(medium.hGlobal); }
            ReleaseStgMedium(&medium);
            return text;
        }
    }
    std::cerr << "OLE clipboard read failed: 0x" << std::hex << result << std::dec << '\n';
    return text;
}
void menuAction(App& app, int action) {
    for (const auto& hit : app.editCtxHits) if (hit.second == action) {
        auto rect = hit.first;
        left(app,{(rect.left+rect.right)/2,(rect.top+rect.bottom)/2});
        return;
    }
    check(false,"requested menu action is available");
    closeEditCtxMenu(app);
}
bool hasChip(const App& app, const char* key) {
    for (const auto& chip : app.signalChips) if (chip.text == tr(app,key)) return true;
    return false;
}
void select(App& app, size_t start, size_t end) {
    app.editorSelStart = start; app.editorSelEnd = end;
    app.editorCursorPos = end; app.editorHasSelection = start != end;
    app.editorCaretUpstreamPos = std::wstring::npos;
}
void preserve(App& app, D2D1_POINT_2F point) {
    ++cases;
    auto start = app.editorSelStart, end = app.editorSelEnd;
    auto cursor = app.editorCursorPos, affinity = app.editorCaretUpstreamPos;
    right(app,point);
    check(app.editorHasSelection && app.editorSelStart == start && app.editorSelEnd == end,
          "right-click inside retains the selection and its direction");
    check(app.editorCursorPos == cursor && app.editorCaretUpstreamPos == affinity,
          "right-click inside retains the caret and its wrap side");
    check(enabled(app,100) && enabled(app,101), "Cut and Copy are enabled and clickable");
    closeEditCtxMenu(app);
}
void outside(App& app, D2D1_POINT_2F point, size_t expected) {
    right(app,point);
    check(!app.editorHasSelection && !enabled(app,100) && !enabled(app,101),
          "right-click outside clears selection and disables Cut/Copy");
    check(app.editorCursorPos == expected, "outside right-click relocates the insertion point");
    closeEditCtxMenu(app);
}
// Clipboard actions use the selected source, and Cut remains undoable. A
// running OLE application owns the clipboard first, as after copying from a
// browser or terminal: restoring that through OleGetClipboard's live proxy
// overflowed the stack in ole32 and ended these tests with 0xc000041d.
void clipboardActions(App& app, const std::wstring& source) {
    ClipboardSnapshot user(app.hwnd);
    check(user.captured, "user clipboard is preserved");
    if (!user.captured) return;
    const std::wstring elsewhere = L"Copied in another application \u4e2d\u6587";
    OleClipboardSource running(elsewhere);
    check(running.owned && clipboardEquals(app,elsewhere), "a running OLE application owns the clipboard");
    {
        ClipboardSnapshot saved(app.hwnd);
        check(saved.captured, "the other application's clipboard is captured");
        size_t word = source.find(L"selection");
        app.editorText = source; rebuildLineStarts(app); select(app,word,word+9);
        auto point = characterPoint(app,word+2); right(app,point); menuAction(app,101);
        check(clipboardEquals(app,L"selection") && app.editorText == source && app.editorHasSelection,
              "menu Copy copies exactly the selection without changing the source or selection");
        right(app,point); menuAction(app,100);
        check(clipboardEquals(app,L"selection") && app.editorText == source.substr(0,word)+source.substr(word+9),
              "menu Cut removes exactly the selected source text");
        key(app,'Z',false,true); check(app.editorText == source,"Undo restores context-menu Cut");
        size_t unicode = source.find(L"Unicode clusters: ") + 18;
        size_t end = source.find(L'.',unicode);
        select(app,unicode,end);
        auto unicodePoint = characterPoint(app,unicode);
        app.editorScrollY = std::max(0.0f,unicodePoint.y-180.0f);
        right(app,characterPoint(app,unicode));
        check(app.editorHasSelection && app.editorSelStart == unicode && app.editorSelEnd == end,
              "Unicode selection survives opening the menu");
        menuAction(app,101);
        auto copied = clipboardLine(app.hwnd);
        if (copied != source.substr(unicode,end-unicode))
            std::cerr << "Clipboard Unicode mismatch: expected length " << end-unicode
                      << ", actual length " << copied.size() << ", previous ASCII copy "
                      << (copied == L"selection") << ", menu still open " << app.editCtxOpen << '\n';
        check(clipboardEquals(app,source.substr(unicode,end-unicode)),
              "menu Copy preserves emoji, combining marks and Chinese text");
        // Another program can hold the clipboard open for a moment. Cut
        // waits briefly for it, and while it stays held leaves the text
        // instead of deleting what it could not copy.
        app.editorScrollY = 0; select(app,word,word+9); point = characterPoint(app,word+2);
        {
            ClipboardHolder held(1000);
            check(held.opened, "another program holds the clipboard");
            app.signalChips.clear();
            right(app,point); menuAction(app,100);
            check(app.editorText == source && app.editorHasSelection, "menu Cut keeps text it could not copy");
            check(hasChip(app,"toast.clipboard_busy"), "menu Cut says the clipboard is busy");
            app.signalChips.clear();
            key(app,'X',false,true);
            check(app.editorText == source && app.editorHasSelection, "Ctrl+X keeps text it could not copy");
            check(hasChip(app,"toast.clipboard_busy"), "Ctrl+X says the clipboard is busy");
            app.signalChips.clear();
            key(app,'C',false,true);
            check(hasChip(app,"toast.clipboard_busy"), "the editor's Ctrl+C says the clipboard is busy");
        }
        {
            ClipboardHolder moment(20);
            key(app,'X',false,true);
            check(clipboardEquals(app,L"selection") && app.editorText == source.substr(0,word)+source.substr(word+9),
                  "Ctrl+X waits for a clipboard held for a moment");
        }
        key(app,'Z',false,true); check(app.editorText == source,"Undo restores the Cut that waited");
    }
    check(clipboardEquals(app,elsewhere), "the other application's clipboard is restored");
    check(oleClipboardText() == elsewhere, "OLE applications can paste the restored clipboard");
}

// The viewer's copies wait a moment for a clipboard another program holds.
// Held for longer, they copy nothing and say so, claim no copy, and keep a
// selection they could not copy.
void viewerCopies(App& app) {
    ClipboardSnapshot user(app.hwnd);
    check(user.captured, "user clipboard is preserved for the viewer copies");
    if (!user.captured) return;
    exitEditMode(app); ensureLayoutComplete(app);
    app.scrollX = app.targetScrollX = app.scrollY = app.targetScrollY = 0;
    const size_t at = app.docText.find(L"selection");
    check(at != std::wstring::npos && !app.codeBlocks.empty() && !app.tableRects.empty(),
          "the viewer lays out the fixture's text, code block and table");
    if (at == std::wstring::npos || app.codeBlocks.empty() || app.tableRects.empty()) return;
    auto viewSelect = [&] { app.selAnchor = at; app.selFocus = at + 9; app.hasSelection = true; };
    // A block's Copy button: the rightmost in its top band, brought into view
    auto pressCopy = [&](const D2D1_RECT_F& bounds, float width) {
        app.scrollY = app.targetScrollY = std::max(0.0f, bounds.top - 200.0f);
        float pad = 8.0f * app.contentScale * app.zoomFactor;
        float right = std::min(bounds.right, app.scrollX + documentViewportWidth(app));
        D2D1_POINT_2F point{right - pad - dpi(app,width) / 2 - app.scrollX + documentViewportX(app),
                            bounds.top + pad + dpi(app,13.0f) - app.scrollY};
        handleMouseMove(app,app.hwnd,MAKELPARAM((int)point.x,(int)point.y));
        left(app,point);
    };
    {
        ClipboardHolder held(1000);
        check(held.opened, "another program holds the clipboard for the viewer");
        viewSelect(); app.signalChips.clear();
        key(app,'C',false,true);
        check(app.hasSelection && hasChip(app,"toast.clipboard_busy") && !hasChip(app,"toast.copied"),
              "viewer Ctrl+C keeps a selection it could not copy and says the clipboard is busy");
        app.hasSelection = false; app.signalChips.clear();
        key(app,'C',false,true);
        check(hasChip(app,"toast.clipboard_busy") && !hasChip(app,"toast.copied"),
              "copying the whole document claims no copy while the clipboard is held");
        app.signalChips.clear();
        pressCopy(app.codeBlocks.front().bounds, 72.0f);
        check(hasChip(app,"toast.clipboard_busy") && !hasChip(app,"codeblock.copied"),
              "the code block's Copy says the clipboard is busy");
        app.signalChips.clear();
        pressCopy(app.tableRects.front().bounds, 88.0f);
        check(hasChip(app,"toast.clipboard_busy") && !hasChip(app,"table.copied"),
              "the table's Copy claims no copy while the clipboard is held");
        app.signalChips.clear();
        copyFilePath(app,app.hwnd,app.currentFile);
        check(hasChip(app,"toast.clipboard_busy") && !hasChip(app,"toast.copied"),
              "Copy file path says the clipboard is busy");
    }
    {
        ClipboardHolder moment(20);
        app.scrollY = app.targetScrollY = 0;
        viewSelect(); app.signalChips.clear();
        key(app,'C',false,true);
        check(clipboardEquals(app,L"selection") && !app.hasSelection && hasChip(app,"toast.copied") &&
                  !hasChip(app,"toast.clipboard_busy"),
              "viewer Ctrl+C waits for a clipboard held for a moment");
    }
    {
        ClipboardHolder moment(20);
        app.signalChips.clear();
        pressCopy(app.tableRects.front().bounds, 88.0f);
        // clipboardLine reads tabs and line breaks as spaces
        std::wstring flat = app.tableRects.front().tsv;
        for (wchar_t& ch : flat) if (ch == L'\t' || ch == L'\r' || ch == L'\n') ch = L' ';
        check(hasChip(app,"table.copied") && clipboardEquals(app,flat),
              "the table's Copy waits for a clipboard held for a moment");
    }
}

// #313: a word of any alphabetic script is one word to a double-click and
// to Ctrl+arrows, and a drag after a double- or triple-click grows the
// selection by whole words or lines, keeping what the clicks took
void wordsOfEveryScript(App& app) {
    app.editorDirty = false;
    if (app.editMode) exitEditMode(app);
    app.currentFile = std::string(TINTA_SOURCE_DIR) + "/tests/fixtures/live/Selecting words.md";
    app.editorLive = false;
    enterEditMode(app); editorReparse(app);
    const std::wstring text = app.editorText;
    check(app.editMode && !editorLiveOn(app) && text.find(L"| Kelime |") != std::wstring::npos,
          "#313: the fixture opens in Source");
    app.editorShowPreview = false; app.editorWordWrap = true; app.width = 1050;
    app.editorScrollY = app.editorScrollX = 0; updateTextFormats(app); rebuildLineStarts(app);
    auto selected = [&] {
        size_t a = std::min(app.editorSelStart,app.editorSelEnd), b = std::max(app.editorSelStart,app.editorSelEnd);
        return app.editorHasSelection ? text.substr(a,b-a) : std::wstring();
    };
    const wchar_t* words[] = {
        L"de\u011fi\u015fiklik", L"g\u00fczelle\u015ftirmek", L"\u00e7al\u0131\u015fkanl\u0131k",
        L"\u00f6\u011frencilerimizden", L"I\u011fd\u0131r", L"\u015e\u0131rnak", L"A\u011fr\u0131",
        L"\u03ba\u03b1\u03bb\u03b7\u03bc\u03ad\u03c1\u03b1", L"\u0437\u0434\u0440\u0430\u0432\u0441\u0442\u0432\u0443\u0439",
        L"ng\u01b0\u1eddi", L"cafe\u0301", L"g\u00f6\u011f\u00fcs", L"de\u011fi\u015fken_ad\u0131"};
    for (const wchar_t* word : words) {
        const size_t at = text.find(word);
        check(at != std::wstring::npos, "#313: the fixture holds each word");
        if (at == std::wstring::npos) continue;
        // On its first letter, on a letter past the ASCII range and on its last
        for (size_t into : {(size_t)0, wcslen(word) / 2, wcslen(word) - 1}) {
            auto point = characterPoint(app,at+into);
            app.editorScrollY = std::max(0.0f,point.y+app.editorScrollY-200.0f);
            point = characterPoint(app,at+into);
            doubleClick(app,point);
            check(selected() == word, "#313: double-click selects the whole word, letters past ASCII and all");
        }
    }
    // CJK stays a run of its own beside Latin words
    const size_t chinese = text.find(L"\u4e2d\u6587\u6d4b\u8bd5");
    auto chinesePoint = characterPoint(app,chinese+1);
    app.editorScrollY = std::max(0.0f,chinesePoint.y+app.editorScrollY-200.0f);
    doubleClick(app,characterPoint(app,chinese+1));
    check(selected() == L"\u4e2d\u6587\u6d4b\u8bd5", "#313: double-click on Chinese still takes the Chinese run alone");

    // Ctrl+Right and Ctrl+Left step over whole Turkish words
    app.editorScrollY = 0;
    const size_t line = text.find(L"T\u00fcrk\u00e7e");
    app.editorCursorPos = app.editorSelStart = app.editorSelEnd = line; app.editorHasSelection = false;
    key(app,VK_RIGHT,false,true);
    check(app.editorCursorPos == line + 6, "#313: Ctrl+Right steps over a Turkish word whole");
    key(app,VK_RIGHT,false,true);
    check(app.editorCursorPos == line + 16, "#313: and over the next");
    const size_t change = text.find(L"de\u011fi\u015fiklik");
    app.editorCursorPos = app.editorSelStart = app.editorSelEnd = change + 10; app.editorHasSelection = false;
    key(app,VK_LEFT,false,true);
    check(app.editorCursorPos == change, "#313: Ctrl+Left steps back over a Turkish word whole");

    // A drag after a double-click grows by words both ways and keeps the word
    auto screen = [&](size_t pos) { auto p = characterPoint(app,pos,0.2f); return POINT{(LONG)p.x,(LONG)p.y}; };
    auto doubleThenDrag = [&](size_t press, size_t to, int clicks) {
        POINT p = screen(press), q = screen(to);
        app.lastClickTime = {}; app.clickCount = 0;
        for (int i = 1; i < clicks; ++i) {
            handleEditorMouseDown(app,app.hwnd,p.x,p.y); handleEditorMouseUp(app,app.hwnd,p.x,p.y);
        }
        handleEditorMouseDown(app,app.hwnd,p.x,p.y);
        check(app.editorSelecting, "#313: the last click of a double- or triple-click holds on to the pointer");
        handleEditorMouseMove(app,app.hwnd,q.x,q.y);
        handleEditorMouseUp(app,app.hwnd,q.x,q.y);
    };
    const size_t pretty = text.find(L"g\u00fczelle\u015ftirmek"), selects = text.find(L"se\u00e7ilir");
    doubleThenDrag(change+3, pretty+4, 2);
    check(app.editorSelStart == change && app.editorSelEnd == pretty + 14 && app.editorCursorPos == pretty + 14,
          "#313: a drag on after a double-click takes whole words up to the pointer's");
    doubleThenDrag(change+3, selects+2, 2);
    check(std::min(app.editorSelStart,app.editorSelEnd) == selects && std::max(app.editorSelStart,app.editorSelEnd) == change + 10 &&
              app.editorCursorPos == selects,
          "#313: a drag back takes whole words back and keeps the double-clicked one");
    doubleThenDrag(change+3, change+5, 2);
    check(selected() == L"de\u011fi\u015fiklik", "#313: a drag within the word keeps just the word");
    // A triple-click's drag takes whole lines
    const size_t second = text.find(L'\n', line) + 1, third = text.find(L'\n', second) + 1;
    doubleThenDrag(line+3, second+3, 3);
    check(app.editorSelStart == line && app.editorSelEnd == third, "#313: a drag after a triple-click takes whole lines");
    // A plain click's drag still goes letter by letter (away from a
    // selection, which a press would move instead, #289)
    app.editorCursorPos = app.editorSelStart = app.editorSelEnd = 0; app.editorHasSelection = false;
    app.lastClickTime = {}; app.clickCount = 0;
    POINT p = screen(change+2), q = screen(change+5);
    handleEditorMouseDown(app,app.hwnd,p.x,p.y); handleEditorMouseMove(app,app.hwnd,q.x,q.y); handleEditorMouseUp(app,app.hwnd,q.x,q.y);
    check(app.editorSelStart == change + 2 && app.editorSelEnd == change + 5, "#313: a plain drag still selects letter by letter");
    app.editorDirty = false;
}
}

int runEditorContextTests() {
    if (FAILED(OleInitialize(nullptr))) return 2;
    auto state = std::make_unique<App>(); App& app = *state;
    app.hwnd = CreateWindowExW(0,L"STATIC",L"Editor context tests",WS_POPUP,0,0,1050,900,nullptr,nullptr,nullptr,nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050; app.height = 900; updateTextFormats(app);
    Settings settings; settings.keyProfile = "windows"; applyKeymap(app,settings);
    app.currentFile = TINTA_CONTEXT_FIXTURE;
    enterEditMode(app); editorReparse(app); ensureLayoutComplete(app);
    const auto source = app.editorText;
    check(app.tableCellRects.size() == 6 && app.codeBlocks.size() == 1, "mixed fixture table and code lay out correctly");
    for (int theme : {0,5}) for (int width : {650,1050})
    for (bool preview : {true,false}) for (bool wrap : {true,false}) {
        applyTheme(app,theme); app.width = width; app.editorShowPreview = preview;
        app.editorWordWrap = wrap; updateTextFormats(app); rebuildLineStarts(app);
        app.editorScrollY = app.editorScrollX = 0;
        size_t word = source.find(L"selection");
        auto point = characterPoint(app,word+2);
        doubleClick(app,point);
        check(app.editorHasSelection && source.substr(app.editorSelStart,app.editorSelEnd-app.editorSelStart) == L"selection",
              "double-click selects the English word");
        preserve(app,point);
        // The last glyph's trailing half is selected, even though its nearest
        // insertion position equals the exclusive end of the selection.
        select(app,word,word+9); preserve(app,characterPoint(app,word+8,0.8f));
        select(app,word+9,word); preserve(app,point);
        size_t chinese = source.find(L"\u4e2d\u6587\u6d4b\u8bd5");
        auto chinesePoint = characterPoint(app,chinese+1);
        app.editorScrollY = std::max(0.0f,chinesePoint.y-180.0f);
        chinesePoint = characterPoint(app,chinese+1);
        doubleClick(app,chinesePoint);
        check(app.editorHasSelection && app.editorSelStart == chinese && app.editorSelEnd == source.find(L'\n',chinese),
              "double-click selects the Chinese text");
        preserve(app,chinesePoint);
        size_t paragraph = source.find(L"A long English"), finish = source.find(L'\n',paragraph);
        auto wrappedPoint = characterPoint(app,paragraph+150);
        app.editorScrollY = std::max(0.0f,wrappedPoint.y+app.editorScrollY-180.0f);
        if (!wrap) app.editorScrollX = std::max(0.0f,wrappedPoint.x-editorPaneWidth(app)/2);
        wrappedPoint = characterPoint(app,paragraph+150);
        select(app,paragraph,finish); preserve(app,wrappedPoint);
        if (wrap) {
            app.editorScrollY = app.editorScrollX = 0;
            float firstRow = caretAt(app,paragraph).y;
            size_t boundary = paragraph+1;
            while (boundary < finish && caretAt(app,boundary).y == firstRow) ++boundary;
            check(boundary < finish,"long paragraph has a soft wrap");
            select(app,paragraph,boundary); app.editorCaretUpstreamPos = boundary;
            auto edge = characterPoint(app,boundary-1,0.8f);
            app.editorScrollY = std::max(0.0f,edge.y-180.0f);
            preserve(app,characterPoint(app,boundary-1,0.8f));
        }
    }
    app.editorShowPreview = false; app.editorWordWrap = true; app.width = 1050;
    app.editorScrollY = app.editorScrollX = 0; updateTextFormats(app); rebuildLineStarts(app);
    size_t word = source.find(L"selection");
    auto point = characterPoint(app,word+2);
    // Drag and keyboard-created selections must behave the same way.
    app.lastClickTime = {}; app.clickCount = 0;
    auto from = caretAt(app,word+8), to = caretAt(app,word+1);
    float mid = app.editorTextFormat->GetFontSize()*0.75f;
    handleEditorMouseDown(app,app.hwnd,(int)from.x,(int)(from.y+mid));
    handleEditorMouseMove(app,app.hwnd,(int)to.x,(int)(to.y+mid));
    handleEditorMouseUp(app,app.hwnd,0,0);
    check(app.editorHasSelection && app.editorSelStart > app.editorSelEnd, "backwards drag creates a reversed selection");
    preserve(app,point);
    select(app,word,word); key(app,VK_RIGHT,true); key(app,VK_RIGHT,true);
    preserve(app,characterPoint(app,word));
    // Opening the source menu also releases Find focus, retaining source selection.
    select(app,word,word+9); app.showSearch = app.searchActive = true;
    preserve(app,point);
    check(!app.searchActive, "source context menu takes focus from Find");
    app.showSearch = false;
    select(app,word,word+9); outside(app,characterPoint(app,word+10,0.1f),word+10);

    // Empty selected lines include a visible newline cell, but not the blank
    // area past that cell or the page below the last line.
    app.editorText = L"selected\n\nnext"; rebuildLineStarts(app); select(app,0,10);
    auto blank = caretAt(app,9); blank.x += 3; blank.y += mid;
    preserve(app,blank);
    select(app,0,8); auto outsidePoint = caretAt(app,8); outsidePoint.x += 40; outsidePoint.y += mid;
    outside(app,outsidePoint,8);
    select(app,0,app.editorText.size()); outside(app,{outsidePoint.x,800},app.editorText.size());
    clipboardActions(app,source);
    app.editorText = source; app.editorDirty = false;
    viewerCopies(app);
    wordsOfEveryScript(app);
    DestroyWindow(app.hwnd); app.hwnd = nullptr; state.reset(); CoUninitialize();
    OleUninitialize();
    std::cout << "Editor context menu: " << cases << " selection cases, " << failures << " failures\n";
    return failures ? 1 : 0;
}
