#include "d2d_init.h"
#include "editor.h"
#include "input.h"
#include "render.h"
#include "settings.h"
#include "utils.h"
#include <cmath>
#include <iostream>
#include <memory>

namespace {
int failures = 0, sequences = 0;
void check(bool ok, const char* message) {
    if (!ok) { if (failures < 25) std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
void key(App& app, unsigned vk, bool shift = false, bool ctrl = false) {
    BYTE original[256], pressed[256];
    GetKeyboardState(original);
    memcpy(pressed,original,sizeof pressed);
    pressed[VK_CONTROL] = ctrl ? 0x80 : 0;
    pressed[VK_SHIFT] = shift ? 0x80 : 0;
    pressed[VK_MENU] = 0;
    SetKeyboardState(pressed);
    handleKeyDown(app,app.hwnd,vk);
    SetKeyboardState(original);
}
void place(App& app, size_t pos) {
    app.editorCursorPos=pos;
    app.editorCaretUpstreamPos=std::wstring::npos;
    app.editorDesiredCol=-1;
    app.editorHasSelection=false;
    app.editorSelStart=app.editorSelEnd=pos;
}
int caretRow(App& app) {
    D2D1_POINT_2F point{};
    check(editorCaretPoint(app,point),"caret geometry is available");
    float height=app.editorTextFormat->GetFontSize()*1.5f;
    return (int)std::floor((point.y+app.editorScrollY-chromeTopHeight(app)-dpi(app,8))/height+0.001f);
}
void paint(App& app) {
    app.cursorBlinkOn=true;
    app.renderTarget->BeginDraw();
    renderEditor(app,editorPaneWidth(app));
    check(SUCCEEDED(app.renderTarget->EndDraw()),"source renders with selection and caret");
}
void clusterBoundary(const App& app) {
    size_t pos=app.editorCursorPos;
    if (pos>0 && pos<app.editorText.size()) {
        wchar_t next=app.editorText[pos], previous=app.editorText[pos-1];
        check(!(previous>=0xd800 && previous<=0xdbff && next>=0xdc00 && next<=0xdfff),
              "vertical movement never splits an emoji surrogate pair");
        check(next!=0x0301,"vertical movement never splits a combining sequence");
    }
}
void paragraph(App& app, const wchar_t* marker, bool shift) {
    ++sequences;
    size_t start=app.editorText.find(marker), end=app.editorText.find(L'\n',start);
    check(start!=std::wstring::npos && end!=std::wstring::npos,"fixture paragraph exists");
    if (start==std::wstring::npos || end==std::wstring::npos) return;
    place(app,start); app.editorScrollY=app.editorScrollX=0;
    int row=caretRow(app), steps=0;
    while (app.editorCursorPos<=end && steps<100) {
        size_t before=app.editorCursorPos;
        key(app,VK_DOWN,shift); ++steps;
        check(app.editorCursorPos>before,"Down advances the insertion position from column zero");
        check(caretRow(app)==++row,"Down moves exactly one visual row");
        check(!shift || (app.editorSelStart==start && app.editorSelEnd==app.editorCursorPos),
              "Shift+Down retains its anchor and advances its endpoint");
        clusterBoundary(app);
        if (app.editorCursorPos==before) break;
    }
    check(app.editorCursorPos==end+1,"Down exits the wrapped paragraph at the next logical line");
    paint(app);
    D2D1_POINT_2F point{}; editorCaretPoint(app,point);
    check(point.y>=chromeTopHeight(app) && point.y<app.height,"scrolling keeps the caret visible");
    for (int n=0;n<steps;++n) {
        key(app,VK_UP,shift);
        check(caretRow(app)==--row,"Up moves exactly one visual row without skipping");
        clusterBoundary(app);
    }
    check(app.editorCursorPos==start,"Up reverses the traversal to paragraph start");
    check(!app.editorHasSelection,"reversing Shift selection back to its anchor clears it");
}
void edgeClicks(App& app) {
    // No whitespace: the right edge is an ambiguous insertion position shared
    // with the next row. A click must stay on the row that was clicked.
    auto source=app.editorText;
    app.editorText=std::wstring(500,L'x')+L"\nshort\n\nend";
    app.editorWordWrap=true; rebuildLineStarts(app);
    app.editorScrollY=app.editorScrollX=0; place(app,0);
    float height=app.editorTextFormat->GetFontSize()*1.5f;
    float textX=dpi(app,48)+editorGutterWidth(app);
    float width=editorPaneWidth(app)-textX-dpi(app,16);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    app.dwriteFactory->CreateTextLayout(app.editorText.data(),500,app.editorTextFormat,
        width,1e7f,layout.GetAddressOf());
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    UINT32 count=0; layout->GetLineMetrics(nullptr,0,&count);
    std::vector<DWRITE_LINE_METRICS> rows(count);
    layout->GetLineMetrics(rows.data(),count,&count);
    float x=0,y=0; DWRITE_HIT_TEST_METRICS hit{};
    layout->HitTestTextPosition(rows[0].length-1,TRUE,&x,&y,&hit);
    app.lastClickTime={}; app.clickCount=0;
    handleEditorMouseDown(app,app.hwnd,(int)(textX+x),(int)(chromeTopHeight(app)+dpi(app,8)+height/2));
    handleEditorMouseUp(app,app.hwnd,0,0);
    check(app.editorCursorPos==rows[0].length && caretRow(app)==0,"right-edge click stays at end of clicked row");
    key(app,VK_SHIFT,true);
    check(caretRow(app)==0,"pressing Shift preserves the wrap side");
    key(app,VK_DOWN,true);
    check(caretRow(app)==1,"Down from a trailing wrap edge visits the very next row");
    key(app,VK_UP,true);
    check(caretRow(app)==0,"Up returns to the trailing edge without skipping");
    key(app,VK_LEFT);
    key(app,VK_RIGHT);
    check(caretRow(app)==1,"Right across a wrap boundary enters its next row");
    // Clicking/dragging to the left edge of a continuation row preserves it.
    app.lastClickTime={}; app.clickCount=0;
    handleEditorMouseDown(app,app.hwnd,(int)textX,(int)(chromeTopHeight(app)+dpi(app,8)+height*1.5f));
    check(caretRow(app)==1,"left-edge click stays at start of continuation row");
    handleEditorMouseMove(app,app.hwnd,(int)textX,(int)(chromeTopHeight(app)+dpi(app,8)+height*2.5f));
    handleEditorMouseUp(app,app.hwnd,0,0);
    check(caretRow(app)==2 && app.editorHasSelection,"dragging selects through the next wrapped row");
    // Preferred x survives short lines; Up/Down do not cycle at blank lines/EOF.
    place(app,500); int row=caretRow(app);
    key(app,VK_DOWN); check(caretRow(app)==row+1,"Down reaches a short logical line");
    key(app,VK_DOWN); check(caretRow(app)==row+2,"Down reaches an empty line");
    key(app,VK_UP); key(app,VK_UP);
    check(app.editorCursorPos==500 && caretRow(app)==row,"Up restores preferred x at a long line end");
    key(app,VK_END,false,true); size_t last=app.editorCursorPos;
    key(app,VK_DOWN); check(app.editorCursorPos==last,"Down at EOF remains stable");
    key(app,VK_HOME,false,true); key(app,VK_UP);
    check(app.editorCursorPos==0,"Up at BOF remains stable");
    key(app,VK_DOWN); size_t insert=app.editorCursorPos;
    auto before=app.editorText;
    handleCharInput(app,app.hwnd,L'Q');
    check(app.editorText==before.substr(0,insert)+L"Q"+before.substr(insert),"typing inserts at the reached wrap boundary");
    key(app,'Z',false,true);
    check(app.editorText==before && app.editorCursorPos==insert,"Undo restores text and the insertion position");
    app.editorText=source; rebuildLineStarts(app); app.editorDirty=false;
}
}
int runWrappedCursorTests() {
    auto state=std::make_unique<App>(); App& app=*state;
    app.hwnd=CreateWindowExW(0,L"STATIC",L"Wrapped cursor tests",WS_POPUP,0,0,1050,900,nullptr,nullptr,nullptr,nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width=1050; app.height=900; updateTextFormats(app);
    Settings settings; settings.keyProfile="windows"; applyKeymap(app,settings);
    app.currentFile=TINTA_WRAP_FIXTURE; enterEditMode(app); editorReparse(app); ensureLayoutComplete(app);
    check(app.tableCellRects.size()==6 && app.codeBlocks.size()==1,"mixed fixture retains its table and code");
    auto original=app.editorText;
    for (int theme : {0,5}) for (float scale : {1.0f,1.5f}) {
        applyTheme(app,theme); app.contentScale=scale; updateTextFormats(app);
        for (int width : {650,1050}) for (bool preview : {true,false}) {
            app.width=width; app.editorShowPreview=preview;
            for (bool wrap : {true,false}) for (bool shift : {false,true}) {
                app.editorWordWrap=wrap; rebuildLineStarts(app);
                for (const wchar_t* marker : {L"A long English",L"\x4e2d\x6587\x6bb5\x843d",L"Unicode clusters:"})
                    paragraph(app,marker,shift);
            }
            edgeClicks(app);
        }
    }
    check(app.editorText==original,"navigation leaves the mixed Markdown unchanged");
    DestroyWindow(app.hwnd); app.hwnd=nullptr; state.reset(); CoUninitialize();
    std::cout << "Wrapped cursor: " << sequences << " sequences, " << failures << " failures\n";
    return failures ? 1 : 0;
}
