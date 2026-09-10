#include "frontmatter_ui.h"
#include "document.h"
#include "d2d_init.h"
#include "editor.h"
#include "overlays.h"
#include "input.h"
#include "render.h"
#include "settings.h"
#include "utils.h"
#include "export.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace {
int failures=0;
void check(bool value,const char* message){if(!value){std::cerr<<"FAIL: "<<message<<'\n';++failures;}}
std::string read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);return {(std::istreambuf_iterator<char>(f)),{}};}
void write(const std::filesystem::path&p,const std::string&s){std::ofstream f(p,std::ios::binary);f<<s;}
void show(fm::Settings&s,const std::string&key){for(auto&r:s.rules)if(r.key==key)r.show=true;}
void dataTests(){
    fm::Settings settings;check(!fm::maintained(settings,"created")&&!fm::maintained(settings,"updated"),"automatic dates default off");
    show(settings,"created");show(settings,"updated");
    const std::string first="2026-09-10T10:00:00Z",now="2026-09-10T11:02:03Z";
    auto source=std::string("---\n# header\ntitle: 'Jane #1' # comment\ntags: [alpha, \"two, words\"]\nupdated: old # keep\ncreated: '2001-02-03' # original\n---\n\n# Body\n");
    auto d=fm::parse(source);check(d.present&&d.safeToWrite&&d.properties.size()==4,"common YAML fields parse");
    check(fm::find(d,"title")->text=="Jane #1"&&fm::find(d,"tags")->items.size()==2,"quoted hashes and commas survive");
    auto stamped=fm::stamp(source,settings,first,now);
    check(stamped.find("updated: \""+now+"\" # keep")!=std::string::npos,"updated scalar replaced without losing its comment");
    check(stamped.find("created: '2001-02-03' # original")!=std::string::npos,"created keeps its first nonempty value");
    auto added=fm::stamp("---\ntitle: x\n---\nBody\n",settings,first,now);
    check(added.find("created: \""+first+"\"\nupdated: \""+now+"\"\n---")!=std::string::npos,"missing date fields are inserted before closing marker");
    auto quotedNull = fm::stamp("---\ncreated: 'null'\nupdated: old\n---\n",settings,first,now);
    check(quotedNull.find("created: 'null'")!=std::string::npos,"quoted null is an existing string, not an empty YAML value");
    auto empty=fm::stamp("---\ncreated: # c\nupdated:\n---\n",settings,first,now);
    check(empty.find("created: \""+first+"\" # c")!=std::string::npos,"empty value preserves YAML comment separation");
    check(empty.find("updated: \""+now+"\"\n")!=std::string::npos,"empty value gets required YAML space");
    for(const std::string bad:{"# No YAML\n", "---\nupdated: unclosed\n", "---\nupdated: one\nupdated: two\n---\n", "---\ntitle: \"multiline\nupdated: literal\nend\"\n---\n", "---\nupdated: [a,\nb]\n---\n", "---\n<<: *defaults\nupdated: old\n---\n"})
        check(fm::stamp(bad,settings,first,now)==bad,"ambiguous or missing frontmatter is not rewritten");
    auto nested=std::string("---\nsettings:\n  updated: nested\ndescription: |\n  created: literal\nupdated: old\n---\n");
    auto ns=fm::stamp(nested,settings,first,now);
    check(ns.find("  updated: nested")!=std::string::npos&&ns.find("  created: literal")!=std::string::npos,"nested fields and block scalars stay unchanged");
    auto opaque=std::string("---\nupdated: |\n  multiline\ncreated: [one, two]\n---\n");
    check(fm::stamp(opaque,settings,first,now)==opaque,"complex timestamp values are never replaced");
    auto crlf=std::string("\xEF\xBB\xBF---\r\ntitle: x\r\n...\r\nBody\r\n");
    auto cs=fm::stamp(crlf,settings,first,now);
    check(cs.substr(0,3)==crlf.substr(0,3)&&cs.find("created: \""+first+"\"\r\n")!=std::string::npos&&cs.find("...\r\nBody\r\n")!=std::string::npos,"BOM, CRLF and alternate closing delimiter survive");
    settings.shown=false;check(fm::stamp(source,settings,first,now)==source,"master Hidden switch also disables maintenance");
    settings.shown=true;for(auto&r:settings.rules)if(r.key=="updated")r.show=false;
    check(fm::stamp(source,settings,first,now)==source,"hidden updated field remains untouched");
    auto lists=fm::parse("---\ntags:\n- one\n- 'two, words'\nauthor: Jane\n---\n");
    check(fm::find(lists,"tags")&&fm::find(lists,"tags")->items.size()==2&&fm::find(lists,"author"),"block lists and the following key stay separate");
    settings.showOther=true;
    check(fm::discover(settings,{{"custom = key", "value"}})&&fm::find(settings,"custom = key")->show,"new keys follow the catch-all rule");
    for(const auto&r:settings.rules){fm::Rule round;check(fm::decodeRule(fm::encodeRule(r),round)&&fm::encodeRule(round)==fm::encodeRule(r),"rule order data and arbitrary key names roundtrip");}
    fm::Rule invalid;check(!fm::decodeRule("0,1,0,1,0,5,0",invalid)&&!fm::decodeRule("GG,1,0,1,0,5,0",invalid),"invalid persisted rules are ignored");
}
void nativeTests(App&app,const std::filesystem::path&output){
    auto fixture=std::filesystem::path(TINTA_FRONTMATTER_FIXTURES)/"frontmatter-settings.md";
    auto source=read(fixture);auto parsed=parseDocument(app.parser,source,"sample.md");app.root=parsed.root;
    check(parsed.success&&app.root->children.front()->type==ElementType::Properties,"mixed frontmatter fixture parses");
    auto properties=app.root->children.front()->properties;
    fm::discover(app.frontmatter,properties);show(app.frontmatter,"author");show(app.frontmatter,"created");show(app.frontmatter,"updated");
    for(auto&r:app.frontmatter.rules)if(r.key=="tags")r.limit=3;
    for(int theme:{0,5})for(float scale:{1.f,1.5f})for(int width:{650,1180}) {
        applyTheme(app,theme);app.contentScale=scale;app.width=width;app.height=width==650?650:900;updateTextFormats(app);
        app.clearLayoutCache();float available=(float)width-80;
        float end=layoutFrontmatterStrip(app,properties,40,40,available,scale);
        check(end>40,"shown properties create a strip");
        for(const auto&r:app.layoutTextRuns)check(r.bounds.left>=39.9f&&r.bounds.right<=width-39.9f&&r.bounds.bottom<=end,"property text wraps within its column and strip");
        check(!app.frontmatterOverflow.empty(),"capped list exposes its remaining values");
        app.root=parsed.root;layoutDocument(app);
        check(app.footnoteAnchors.size()==3&&app.tableRects.size()==1&&app.codeBlocks.size()==1,"footnotes, tables and code survive metadata layout");
        check(app.docText.find(L"Final body paragraph")!=std::wstring::npos,"body remains present after metadata");
        app.showSettings=true;app.settingsSection=3;app.settingsAnimation=1;
        app.renderTarget->Resize(D2D1::SizeU(width,app.height));app.renderTarget->BeginDraw();renderSettingsOverlay(app);
        check(SUCCEEDED(app.renderTarget->EndDraw()),"native frontmatter settings draw at both widths and scales");
        auto panel=settingsPanelRect(app);
        for(const auto&[r,id]:app.settingsHits)check(r.left>=panel.left-.1f&&r.right<=panel.right+.1f&&r.top>=panel.top-.1f&&r.bottom<=panel.bottom+.1f&&r.bottom>r.top&&r.right>r.left,"frontmatter settings controls fit within the panel");
    }
    app.contentScale=1;app.width=1180;app.height=900;updateTextFormats(app);
    auto paint=[&](){app.renderTarget->BeginDraw();renderSettingsOverlay(app);check(SUCCEEDED(app.renderTarget->EndDraw()),"settings repaint succeeds");};
    paint();
    auto before=source;
    frontmatterAction(app,FM_HIDDEN);check(!fm::maintained(app.frontmatter,"updated"),"master UI switch gates date maintenance");
    frontmatterAction(app,FM_SHOWN);check(fm::maintained(app.frontmatter,"updated"),"shown UI switch restores enabled dates");
    frontmatterAction(app,FM_ROW+2*8+5);paint();
    check(app.frontmatterMenu==2&&app.frontmatterMenuRect.right>app.frontmatterMenuRect.left,"list format menu opens");
    frontmatterAction(app,FM_PICK+(int)fm::Format::Hashtags);
    check(app.frontmatter.rules[2].format==fm::Format::Hashtags,"list format selection persists");
    paint();auto first=app.frontmatterRows[0];auto last=app.frontmatterRows[3];
    handleMouseDown(app,app.hwnd,MK_LBUTTON,MAKELPARAM((int)first.left+8,(int)first.top+8));
    check(app.frontmatterDrag==0,"drag handle begins a reorder through normal input");
    frontmatterMouseMove(app,last.left+8,last.bottom-2);
    handleMouseUp(app,app.hwnd,0,MAKELPARAM((int)last.left+8,(int)last.bottom-2));
    check(app.frontmatter.rules[3].key=="title","row drag changes the ordered property model");
    auto reloaded=loadSettings();check(reloaded.frontmatter.rules[3].key=="title"&&fm::maintained(reloaded.frontmatter,"created"),"display order and maintenance reload from settings.ini");
    check(read(fixture)==before,"view and settings changes never rewrite source");
    app.showSettings=false;
    check(exportHtmlFile(app,(output/"frontmatter.html").wstring())&&exportDocxFile(app,(output/"frontmatter.docx").wstring()),"mixed frontmatter document exports successfully");
}
void saveTests(App&app,const std::filesystem::path&output){
    app.frontmatter=fm::Settings{};show(app.frontmatter,"created");show(app.frontmatter,"updated");
    auto path=output/"save.md";std::string original="---\ntitle: Save test\nupdated: old # keep\n---\n\n# Body\n";
    write(path,original);app.currentFile=toUtf8(path.wstring());app.tabs.clear();app.editMode=true;app.editorShowPreview=true;
    restoreEditBuffer(app,toWide(original),true,0,original.size());
    const std::string first="2026-08-01T12:00:00Z";app.frontmatterFirstSeen[frontmatterSeenKey(app)]=first;
    size_t cursor=app.editorCursorPos;saveEditorFile(app,app.hwnd);
    auto saved=read(path);auto metadata=fm::parse(saved);
    check(!app.editorDirty&&fm::find(metadata,"created")&&fm::find(metadata,"created")->text==first,"successful native save records the first observed time");
    check(fm::find(metadata,"updated")->text!="old"&&saved.find("# keep")!=std::string::npos,"native save updates timestamp and preserves comment");
    check(toUtf8(app.editorText)==saved&&app.editorCursorPos==app.editorText.size(),"successful save updates buffer and retains body caret");
    check(!app.undoStack.empty()&&app.undoStack.back().type==App::EditAction::Replace,"metadata changes form one atomic undo action");
    BYTE keyboard[256]{},control[256]{};GetKeyboardState(keyboard);control[VK_CONTROL]=0x80;SetKeyboardState(control);
    handleEditorKeyDown(app,app.hwnd,'Z');SetKeyboardState(keyboard);
    check(toUtf8(app.editorText)==original&&app.editorCursorPos==cursor,"one Undo restores pre-save metadata and caret");
    SetKeyboardState(control);handleEditorKeyDown(app,app.hwnd,'Y');SetKeyboardState(keyboard);
    check(toUtf8(app.editorText)==saved,"Redo restores the complete stamped buffer");
    // Make the date old again but mark the buffer clean: explicit save still updates it.
    restoreEditBuffer(app,toWide(original),false,0,0);saveEditorFile(app,app.hwnd);
    check(read(path).find("updated: old")==std::string::npos,"explicit save maintains dates even for a clean buffer");
    auto blocked=output/"blocked.md";write(blocked,original);app.currentFile=toUtf8(blocked.wstring());restoreEditBuffer(app,toWide(original),true,0,3);
    auto undoSize=app.undoStack.size();HANDLE lock=CreateFileW(blocked.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    check(lock!=INVALID_HANDLE_VALUE,"failure test locks its isolated file");saveEditorFile(app,app.hwnd);if(lock!=INVALID_HANDLE_VALUE)CloseHandle(lock);
    check(app.editorDirty&&toUtf8(app.editorText)==original&&app.undoStack.size()==undoSize&&read(blocked)==original,"failed write leaves source, editor and undo history unchanged");
    app.currentFile=toUtf8(path.wstring());app.frontmatter.shown=false;restoreEditBuffer(app,toWide(original),true,0,0);saveEditorFile(app,app.hwnd);
    check(read(path)==original,"hidden metadata disables automatic date writes");
    app.frontmatter.shown=true;auto plain=std::string("# No frontmatter\n\nBody\n");write(path,plain);restoreEditBuffer(app,toWide(plain),true,0,0);saveEditorFile(app,app.hwnd);
    check(read(path)==plain,"save never adds a frontmatter block to a document without one");
    auto crlf=std::string("\xEF\xBB\xBF---\r\ntitle: BOM\r\nupdated: old # c\r\n---\r\nBody\r\n");write(path,crlf);
    auto normalized=crlf;normalized.erase(std::remove(normalized.begin(),normalized.end(),'\r'),normalized.end());
    restoreEditBuffer(app,toWide(normalized),true,0,normalized.size());saveEditorFile(app,app.hwnd);auto bytes=read(path);
    check(bytes.substr(0,3)==crlf.substr(0,3)&&bytes.find("\r\n")!=std::string::npos&&bytes.find("\r\r\n")==std::string::npos,"native save preserves BOM and original CRLF style");
    for(size_t i=0;i<bytes.size();++i)if(bytes[i]=='\n')check(i>0&&bytes[i-1]=='\r',"all saved line endings remain CRLF");
}
}
int main(){
    namespace fs=std::filesystem;dataTests();auto state=std::make_unique<App>();auto&app=*state;
    app.hwnd=CreateWindowExW(0,L"STATIC",L"Frontmatter tests",WS_POPUP,0,0,1180,900,nullptr,nullptr,nullptr,nullptr);
    if(!app.hwnd||!initD2D(app)||!createRenderTarget(app))return 2;
    fs::path output=fs::current_path()/"frontmatter-test-output";fs::create_directories(output);
    nativeTests(app,output);saveTests(app,output);
    DestroyWindow(app.hwnd);app.hwnd=nullptr;state.reset();CoUninitialize();
    std::cout<<"Frontmatter: "<<failures<<" failures\n";return failures?1:0;
}
