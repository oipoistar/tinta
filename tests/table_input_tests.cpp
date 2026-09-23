#include "d2d_init.h"
#include "editor.h"
#include "input.h"
#include "render.h"
#include "search.h"
#include "settings.h"
#include "tableedit.h"
#include "tabs.h"
#include "utils.h"
#include "clipboard_snapshot.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace {
int failures=0;
void check(bool ok,const char* label) { if(!ok){++failures;std::cerr<<"FAIL table/modifiers: "<<label<<'\n';} }
void key(App& app,unsigned vk,bool ctrl=false,bool shift=false,bool alt=false,unsigned ch=0) {
    BYTE old[256],pressed[256];GetKeyboardState(old);memcpy(pressed,old,sizeof old);
    pressed[VK_CONTROL]=ctrl?0x80:0;pressed[VK_SHIFT]=shift?0x80:0;pressed[VK_MENU]=alt?0x80:0;
    SetKeyboardState(pressed);
    if(!handleKeyDown(app,app.hwnd,vk) && ch)handleCharInput(app,app.hwnd,ch);
    SetKeyboardState(old);
}
void type(App& app,const std::wstring& text) { for(auto ch:text)handleCharInput(app,app.hwnd,ch); }
void putClipboard(App& app,const std::wstring& text) {
    bool copied=false;
    for(int attempt=0;attempt<20 && !(copied=copyToClipboard(app.hwnd,text));++attempt) Sleep(5);
    check(copied,"test text reaches the clipboard");
}
void paint(App& app) {
    ensureLayoutComplete(app);
    app.renderTarget->BeginDraw();renderTableEditOverlay(app);renderEditorReadingButton(app);
    check(SUCCEEDED(app.renderTarget->EndDraw()),"native table overlay paints");
}
void buffer(App& app,const std::wstring& text) {
    tableEditCancel(app);closeSearchInput(app);app.confirmExitPending=false;app.pendingTabClose=-1;
    app.editorShowPreview=true;app.scrollY=app.targetScrollY=app.scrollX=app.targetScrollX=0;
    restoreEditBuffer(app,text,false,0,0);app.layoutDirty=true;paint(app);
}
App::TableCellRect cell(App& app,int row,int col) {
    ensureLayoutComplete(app);
    for(auto c:app.tableCellRects)if(c.row==row&&c.col==col)return c;
    check(false,"requested table cell exists");return {};
}
void click(App& app,float x,float y) {
    LPARAM p=MAKELPARAM(static_cast<int>(x),static_cast<int>(y));
    handleMouseDown(app,app.hwnd,MK_LBUTTON,p);handleMouseUp(app,app.hwnd,0,p);
}
void open(App& app,int row,int col) {
    auto c=cell(app,row,col);app.scrollY=app.targetScrollY=std::max(0.0f,c.rect.top-150);
    click(app,documentViewportX(app)+c.rect.left-app.scrollX+10,c.rect.top-app.scrollY+12);
    check(app.tableEditActive && app.tableEditRow==row && app.tableEditCol==col,"public click opens the intended cell");
}
void modifiers(App& app) {
    buffer(app,L"# Modifiers\n\nType here\n");app.editorCursorPos=app.editorText.size();
    bool wrap=app.editorWordWrap;
    key(app,'W',true,false,true,0x2713);
    check(app.editorWordWrap==wrap && app.editorText.back()==L'\u2713',"AltGr character inserts without toggling wrap");
    key(app,'W',true,true);check(app.editorWordWrap==wrap,"Ctrl+Shift+W cannot alias Ctrl+W");
    auto before=app.editorText;
    for(unsigned k:{'F','H','O','S','N','T','B','I','P'})key(app,k,true,false,true);
    check(!app.showSearch && !app.showPrintPreview && app.editorText==before && app.tabs.size()==1,"AltGr command keys cannot trigger app actions");
    key(app,'F',true,true);check(!app.showSearch,"Ctrl+Shift+F cannot alias Find");
    key(app,'W',true);check(app.editorWordWrap!=wrap,"plain Ctrl+W still toggles wrap");
    app.editorCursorPos=0;key(app,VK_RIGHT,true,true);
    check(app.editorHasSelection,"Ctrl+Shift+Right still selects by word");
    app.editorHasSelection=false;
    key(app,'E',true,true);check(app.editorReadingPreview,"explicit Ctrl+Shift+E opens full reading preview");
    key(app,VK_ESCAPE);check(!app.editorReadingPreview,"Escape returns to editing");
    key(app,'F',true);check(app.showSearch,"plain Ctrl+F still opens Find");
    key(app,'A',true,false,true,0x0105);
    check(app.searchQuery==L"\u0105","AltGr text reaches the focused Find field");
    closeSearchInput(app);
}
void cells(App& app,const std::wstring& source) {
    ClipboardSnapshot clipboard(app.hwnd);check(clipboard.captured,"clipboard is safely preserved");
    for(int theme:{0,5})for(int width:{650,1050}) {
        applyTheme(app,theme);app.width=width;app.height=900;updateTextFormats(app);buffer(app,source);
        check(app.tableCellRects.size()==24 && app.codeBlocks.size()==1,"mixed fixture retains both tables and code");
        open(app,1,1);key(app,VK_HOME,true);type(app,L"X");
        check(app.tableEditText.rfind(L"XWB",0)==0,"insertion starts at the cell caret");
        key(app,'Z',true);check(app.tableEditText==L"WB202609171624520001","cell-local undo restores typing");
        app.tableEditCaret=app.tableEditAnchor=2;D2D1_POINT_2F point{};check(tableEditCaretPoint(app,point),"cell has IME/caret geometry");
        click(app,point.x+1,point.y-app.textFormat->GetFontSize()*0.6f);type(app,L"!");
        check(app.tableEditText==L"WB!202609171624520001","mouse click repositions inside the active cell");
        key(app,'A',true);type(app,L"abcdef");key(app,VK_HOME,true);key(app,VK_RIGHT,false,true);key(app,VK_RIGHT,false,true);type(app,L"Z");
        check(app.tableEditText==L"Zcdef","Shift selection is replaced by typing");
        app.tableEditCaret=app.tableEditAnchor=0;tableEditCaretPoint(app,point);
        auto c=cell(app,1,1);float y=point.y-app.textFormat->GetFontSize()*0.6f;
        handleMouseDown(app,app.hwnd,MK_LBUTTON,MAKELPARAM(static_cast<int>(point.x),static_cast<int>(y)));
        handleMouseMove(app,app.hwnd,MAKELPARAM(static_cast<int>(documentViewportX(app)+c.rect.right-app.scrollX),static_cast<int>(y)));
        handleMouseUp(app,app.hwnd,0,0);type(app,L"selected");
        check(app.tableEditText==L"selected" && !app.tableEditSelecting && GetCapture()!=app.hwnd,"drag selection replaces text and releases capture");
        if(clipboard.captured) {
            key(app,'A',true);key(app,'C',true);check(clipboardLine(app.hwnd)==L"selected","Ctrl+C copies the cell selection");
            key(app,'X',true);check(app.tableEditText.empty(),"Ctrl+X cuts the cell selection");
            key(app,'V',true);check(app.tableEditText==L"selected","Ctrl+V pastes into the cell");
            key(app,'A',true);putClipboard(app,L"\u4e2d\u6587 \U0001F600 e\u0301 | pasted");key(app,'V',true);
            check(app.tableEditText==L"\u4e2d\u6587 \U0001F600 e\u0301 | pasted" && app.editorText==source,"Unicode paste replaces only cell text before commit");
        }
        key(app,'A',true);type(app,L"A\U0001F600e\u0301\u4e2d");key(app,VK_LEFT);key(app,VK_BACK);
        check(app.tableEditText==L"A\U0001F600\u4e2d","Backspace removes one combining cluster");key(app,VK_BACK);
        check(app.tableEditText==L"A\u4e2d","Backspace does not split emoji surrogates");
        key(app,'A',true);type(app,L"changed | value");key(app,VK_RETURN);paint(app);
        check(app.editorText.find(L"changed \\| value")!=app.editorText.npos && app.tableCellRects.size()==24,"commit escapes pipes without changing table structure");
        key(app,'Z',true);
        if(app.editorText!=source) {
            size_t p=0;while(p<source.size()&&p<app.editorText.size()&&source[p]==app.editorText[p])++p;
            std::cerr<<"Undo mismatch at "<<p<<" actual="<<toUtf8(app.editorText.substr(p,60))<<" expected="<<toUtf8(source.substr(p,60))<<'\n';
        }
        check(app.editorText==source,"document Undo restores the whole committed cell");
        open(app,2,1);if(clipboard.captured){putClipboard(app,L"WB12345");key(app,'V',true);check(app.tableEditText==L"WB12345","reporter empty-cell paste works");}
        key(app,VK_TAB);check(app.tableEditCol==2 && app.tableEditRow==2,"Tab commits then opens the next cell");
        key(app,'A',true);type(app,std::wstring(100,L'\u4e2d'));key(app,VK_END,true);paint(app);
        check(tableEditCaretPoint(app,point) && app.tableEditScrollY>0,"wrapped cell scrolls its caret into view");
        auto box=cell(app,2,2).rect;
        check(point.y>=box.top-app.scrollY && point.y<=box.bottom-app.scrollY+2,"wrapped caret stays in the visible cell");
        key(app,VK_HOME,true);paint(app);check(app.tableEditScrollY==0,"Home scrolls the cell back to its beginning");
        key(app,VK_ESCAPE);check(!app.tableEditActive && !app.editorReadingPreview,"Escape cancels only the active cell");
        open(app,3,3);key(app,VK_END,true);type(app,L"!");key(app,VK_RETURN);
        check(app.editorText.find(L"a\\|b!")!=app.editorText.npos && app.editorText.find(L"a\\\\|b")==app.editorText.npos,"editing preserves an existing escaped pipe");
        open(app,2,1);key(app,'W',true,false,true,0x2713);
        check(app.tableEditText.find(L'\u2713')!=app.tableEditText.npos,"AltGr types into the cell");
        key(app,'F',true);check(app.searchActive && !app.tableEditActive,"Find commits the cell and takes focus");closeSearchInput(app);
    }
}
void reading(App& app,const std::wstring& source) {
    buffer(app,source);app.tabs.clear();tabsInit(app);
    auto path=std::filesystem::current_path()/L"table-live-preview.md";
    std::ofstream(path,std::ios::binary)<<toUtf8(source);app.currentFile=toUtf8(path.wstring());app.tabs[0].path=app.currentFile;
    open(app,2,1);type(app,L"UNSAVED");
    key(app,'E',true,true);paint(app);
    auto edited=app.editorText;auto undo=app.undoStack.size();auto caret=app.editorCursorPos;
    check(app.editorReadingPreview && app.editorDirty && !app.confirmExitPending && !app.tableEditActive,"reading view commits a live cell without saving or prompting");
    check(documentViewportX(app)==0 && editorPaneWidth(app)==0 && app.docText.find(L"UNSAVED")!=app.docText.npos,"full-width preview contains unsaved table text");
    std::ifstream disk(path,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(disk)),{});
    check(bytes==toUtf8(source),"reading preview leaves the file on disk unchanged");
    type(app,L"must not insert");key(app,'V',true);check(app.editorText==edited,"reading view cannot edit an invisible source caret");
    key(app,'E',false,false,false,'e');
    check(!app.editorReadingPreview && app.editorText==edited && app.undoStack.size()==undo && app.editorCursorPos==caret,"return-to-edit key preserves text/caret/undo and does not insert e");
    key(app,'Z',true);check(app.editorText==source,"Undo works after a preview round trip");
    // Esc keeps its pre-3.7.3 meaning on unsaved edits: the save dialog (#242)
    type(app,L"X");key(app,VK_ESCAPE);
    check(app.editMode && app.confirmExitPending && !app.editorReadingPreview,"Escape on unsaved edits opens the save dialog, not the reading view");
    confirmExitAction(app,app.hwnd,3);
    check(app.editMode && app.editorDirty && !app.confirmExitPending,"Keep editing returns to the unsaved buffer");
    key(app,'E',true,true);check(app.editorReadingPreview,"Ctrl+Shift+E still reads the unsaved buffer");
    edited=app.editorText;tabOpenStartPage(app,app.hwnd);tabActivate(app,app.hwnd,0);
    check(app.editorText==edited && app.editorDirty && !app.editorReadingPreview,"tab switch preserves the unsaved reading buffer");
    setEditorReadingPreview(app,true);tabCloseIndex(app,app.hwnd,0);
    check(app.confirmExitPending && app.pendingTabClose==0,"closing an unsaved reading tab still prompts");
    confirmExitAction(app,app.hwnd,3);check(app.editorText==edited,"cancel close preserves unsaved text");
    key(app,'S',true);check(!app.editorDirty && app.editorReadingPreview,"Ctrl+S saves while retaining reading view");
}
}
namespace {
// #242: Esc leaves edit mode as it did before 3.7.3, and the reading view
// wears the reader's chrome: a full tab strip, a draggable title bar, tab
// clicks that reach the strip, and a page that starts below the strip
void escapeAndChrome(App& app) {
    auto dir=std::filesystem::path(TINTA_FIND_FIXTURE).parent_path();
    auto path=dir/L"reading-view-242.md";
    std::ifstream input(path);std::string bytes((std::istreambuf_iterator<char>(input)),{});
    auto source=toWide(bytes);check(!source.empty(),"reading view fixture loads");
    for(int theme:{0,5})for(int width:{650,1050}) {
        applyTheme(app,theme);app.width=width;app.height=900;updateTextFormats(app);
        app.currentFile=toUtf8(path.wstring());app.tabs.clear();tabsInit(app);
        App::DocTab companion;companion.id=++app.tabIdCounter;
        companion.path=toUtf8((dir/L"table-input-236.md").wstring());companion.title=L"table-input-236.md";
        app.tabs.push_back(companion);app.activeTab=0;
        // Clean buffer: the first Esc arms the exit, the second leaves edit mode
        buffer(app,source);
        check(app.docText.find(L"Final heading")!=app.docText.npos,"mixed fixture lays out completely");
        key(app,VK_ESCAPE);
        check(app.editMode && !app.editorReadingPreview && app.escPressedOnce,"first Escape on a clean buffer keeps editing and arms the exit");
        key(app,VK_ESCAPE);
        check(!app.editMode && !app.editorReadingPreview,"second Escape leaves edit mode for the reader");
        // Unsaved edits: Esc opens the save dialog instead of the reading view
        buffer(app,source);app.editorCursorPos=app.editorText.size();type(app,L"!");
        key(app,VK_ESCAPE);
        check(app.editMode && app.confirmExitPending && !app.editorReadingPreview,"Escape on unsaved edits opens the save dialog");
        confirmExitAction(app,app.hwnd,3);
        // The reading view keeps its entry point and wears the reader's chrome
        key(app,'E',true,true);
        check(app.editorReadingPreview && app.editorDirty && !editSplitPreview(app),"Ctrl+Shift+E reads unsaved edits outside the split layout");
        check(documentViewportX(app)==0 && documentViewportWidth(app)==static_cast<float>(app.width),"reading view spans the window without the sheet inset");
        ensureLayoutComplete(app);
        check(!app.scrollAnchors.empty() && app.scrollAnchors.front().renderedY>=chromeTopHeight(app),"reading page starts below the tab strip");
        check(app.docText.find(L"Final heading")!=app.docText.npos,"reading view lays out the whole unsaved document");
        app.renderTarget->BeginDraw();renderTabStrip(app);check(SUCCEEDED(app.renderTarget->EndDraw()),"reading view title strip renders");
        auto drag=titleDragRect(app);
        check(drag.right>=captionIslandLeft(app) && drag.right-drag.left>=dpi(app,12),"the empty title bar beside the window buttons drags the window");
        D2D1_RECT_F companionRect{};bool companionShown=false,plus=false;
        for(const auto& hit:app.tabHits){
            if(hit.index==1){companionRect=hit.rect;companionShown=hit.rect.right-hit.rect.left>=dpi(app,40);}
            if(hit.index==-2)plus=true;
        }
        check(companionShown && plus,"tabs and the new-tab button are drawn in the reading view");
        // Esc in the reading view returns to the editor, not the reader
        key(app,VK_ESCAPE);
        check(app.editMode && !app.editorReadingPreview && app.editorDirty,"Escape in the reading view returns to the editor");
        // A tab click in the reading view reaches the strip
        key(app,'E',true,true);
        if(companionShown) {
            click(app,(companionRect.left+companionRect.right)/2,(companionRect.top+companionRect.bottom)/2);
            check(app.activeTab==1 && !app.editMode,"clicking a tab in the reading view switches tabs");
            tabActivate(app,app.hwnd,0);
            check(app.editMode && app.editorDirty && !app.editorText.empty() && app.editorText.back()==L'!',"returning to the tab restores the unsaved buffer");
        }
        app.editorDirty=false;
    }
}
}
int runTableInputTests() {
    OleInitialize(nullptr);
    auto state=std::make_unique<App>();auto& app=*state;
    app.hwnd=CreateWindowExW(0,L"STATIC",L"Table input tests",WS_POPUP,0,0,1050,900,nullptr,nullptr,nullptr,nullptr);
    if(!app.hwnd || !initD2D(app) || !createRenderTarget(app))return 2;
    app.width=1050;app.height=900;updateTextFormats(app);Settings settings;settings.keyProfile="windows";applyKeymap(app,settings);
    auto fixture=std::filesystem::path(TINTA_FIND_FIXTURE).parent_path()/L"table-input-236.md";
    // The editor normalizes CRLF. Read the expected fixture in text mode too,
    // so fresh Windows checkouts behave like LF checkouts in these comparisons.
    std::ifstream input(fixture);std::string bytes((std::istreambuf_iterator<char>(input)),{});
    auto source=toWide(bytes);check(!source.empty(),"fixture loads");app.currentFile=toUtf8(fixture.wstring());tabsInit(app);
    modifiers(app);cells(app,source);reading(app,source);escapeAndChrome(app);
    DestroyWindow(app.hwnd);app.hwnd=nullptr;state.reset();CoUninitialize();OleUninitialize();
    std::cout<<"Table and modifier input: "<<failures<<" failures\n";return failures?1:0;
}
