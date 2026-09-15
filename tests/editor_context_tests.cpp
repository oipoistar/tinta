#include "d2d_init.h"
#include "editor.h"
#include "editrail.h"
#include "input.h"
#include "render.h"
#include "search.h"
#include "settings.h"
#include "utils.h"
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
void menuAction(App& app, int action) {
    for (const auto& hit : app.editCtxHits) if (hit.second == action) {
        auto rect = hit.first;
        left(app,{(rect.left+rect.right)/2,(rect.top+rect.bottom)/2});
        return;
    }
    check(false,"requested menu action is available");
    closeEditCtxMenu(app);
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
    select(app,0,8); auto end = caretAt(app,8); end.x += 40; end.y += mid;
    outside(app,end,8);
    select(app,0,app.editorText.size()); outside(app,{end.x,800},app.editorText.size());
    // Clipboard actions use the selected source, and Cut remains undoable.
    Microsoft::WRL::ComPtr<IDataObject> clipboard;
    HRESULT saved = OleGetClipboard(clipboard.GetAddressOf());
    check(SUCCEEDED(saved), "user clipboard is preserved");
    if (SUCCEEDED(saved)) {
        app.editorText = source; rebuildLineStarts(app); select(app,word,word+9);
        point = characterPoint(app,word+2); right(app,point); menuAction(app,101);
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
        HRESULT restored = E_FAIL;
        for (int i=0;i<10 && FAILED(restored=OleSetClipboard(clipboard.Get()));++i) Sleep(20);
        check(SUCCEEDED(restored),"user clipboard is restored");
        if (SUCCEEDED(restored)) OleFlushClipboard();
    }
    app.editorText = source; app.editorDirty = false;
    DestroyWindow(app.hwnd); app.hwnd = nullptr; state.reset(); CoUninitialize();
    OleUninitialize();
    std::cout << "Editor context menu: " << cases << " selection cases, " << failures << " failures\n";
    return failures ? 1 : 0;
}
