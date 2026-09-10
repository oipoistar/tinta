#include "document.h"
#include "d2d_init.h"
#include "export.h"
#include "render.h"
#include "utils.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>

namespace {
int failures=0;
void check(bool value,const char* message) { if(!value) { std::cerr<<"FAIL: "<<message<<'\n';++failures; } }
std::string read(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary);return {(std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>()};
}
std::vector<ElementPtr> find(const ElementPtr& root,ElementType type) {
    std::vector<ElementPtr> result;
    std::function<void(const ElementPtr&)> walk=[&](const ElementPtr& e) {
        if(e->type==type)result.push_back(e);
        for(const auto& c:e->children) { check(c->parent==e.get(),"AST parent links remain valid");walk(c); }
    };walk(root);return result;
}
void parserTests(App& app) {
    auto parse=[&](std::string s){auto p=app.parser.parse(s);check(p.success,"footnote sample parses");return p.root;};
    auto root=parse("First[^b], second[^a], again[^b].\n\n[^a]: Alpha\n[^b]: Beta\n");
    auto refs=find(root,ElementType::FootnoteReference),defs=find(root,ElementType::FootnoteDefinition);
    check(refs.size()==3 && defs.size()==2,"named, numeric and repeated notes resolve");
    if(refs.size()==3)check(refs[0]->level==1&&refs[1]->level==2&&refs[2]->level==1,"first reference determines numbering");
    check(find(root,ElementType::FootnoteBacklink).size()==3,"every occurrence has a return link");
    root=parse("Literal \\[^a], `[^a]`, [label[^a]](https://host), missing[^none].\n\n[^a]: note\n");
    check(find(root,ElementType::FootnoteReference).empty(),"escaped/code/link/missing references remain literal");
    root=parse("```md\n[^a]: fake\n```\n\n[^a]\n");
    check(find(root,ElementType::FootnoteDefinition).empty(),"fenced definitions remain literal");
    root=parse("<pre>\n[^a]: fake\n</pre>\n\n[^a]\n");
    check(find(root,ElementType::FootnoteDefinition).empty(),"raw preformatted definitions remain literal");
    root=parse("A[^n].\n\n[^n]: first\n\n  second\n\n    - list\n    - item\n\nAfter.\n");
    defs=find(root,ElementType::FootnoteDefinition);
    check(defs.size()==1,"multiline note resolves");
    if(!defs.empty())check(find(defs[0],ElementType::Paragraph).size()>=3 && !find(defs[0],ElementType::List).empty(),"multiline paragraphs and lists survive");
    root=parse("A[^n], x^2^ and H~2~O.\r\n\r\n[^n]: note\r\n");
    check(find(root,ElementType::FootnoteReference).size()==1 && find(root,ElementType::Superscript).size()==1 && find(root,ElementType::Subscript).size()==1,"CRLF, footnotes and script extensions coexist");
    root=parse("[^a]\n\n[^a]: first\n\n[^a]: duplicate\n");
    check(find(root,ElementType::FootnoteDefinition).size()==1,"duplicate definitions do not create duplicate IDs");
    root=parse("[^a]\n\n[^a]: Nested [^b] stays literal.\n[^b]: Nested [^a] stays literal.\n");
    check(find(root,ElementType::FootnoteDefinition).size()==1,"nested/circular references do not recurse");
    root=parse("A[^a], B[^b].\n\n  [^a]: first\n  [^b]: second\n");
    check(find(root,ElementType::FootnoteDefinition).size()==2,"indented sibling definitions remain separate");
    root=parse("[^missing][^other]");
    check(find(root,ElementType::Superscript).empty(),"unresolved adjacent references never become superscript spans");
}
}
int main() {
    namespace fs=std::filesystem;
    auto state=std::make_unique<App>();auto& app=*state;
    if(!initD2D(app))return 2;
    // A message-only window gives native anchor navigation an isolated repaint target.
    app.hwnd=CreateWindowExW(0,L"STATIC",L"Footnote tests",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    parserTests(app);
    auto source=read(TINTA_FOOTNOTE_FIXTURE);
    auto parsed=parseDocument(app.parser,source,"footnotes.md");check(parsed.success,"mixed footnote fixture parses");
    auto refs=find(parsed.root,ElementType::FootnoteReference),defs=find(parsed.root,ElementType::FootnoteDefinition);
    check(refs.size()==7 && defs.size()==2,"all mixed references and both notes render");
    for(auto type:{ElementType::Heading,ElementType::Table,ElementType::BlockQuote,ElementType::List,ElementType::Strong,ElementType::Emphasis,ElementType::Highlight,ElementType::MathInline,ElementType::MathDisplay,ElementType::CodeBlock})
        check(!find(parsed.root,type).empty(),"mixed structures survive footnote parsing");
    app.height=650;app.readingWidthPct=100;
    for(int theme:{0,5}) {
        applyTheme(app,theme);
        for(float scale:{1.0f,1.5f}) {
            app.contentScale=scale;updateTextFormats(app);
            for(int width:{1050,650}) {
                app.width=width;app.root=parsed.root;layoutDocument(app);
                check(app.footnoteAnchors.size()==refs.size()+defs.size(),"native reference/definition anchors are complete");
                check(app.docText.find(L"Final body paragraph")<app.docText.find(L"A longer note"),"notes render after body");
                check(app.docText.find(L"[^missing]")!=std::wstring::npos,"unresolved references stay visible");
                check(app.docText.find(L"A second paragraph in the same note")!=std::wstring::npos,"multiline note text is selectable/searchable");
                check(app.codeBlocks.size()==2,"literal and note SQL blocks remain intact");
                std::set<std::string> links;
                for(const auto& link:app.linkRects)links.insert(link.url);
                for(const auto& ref:refs) {
                    check(links.count(ref->url)>0 && links.count("#"+ref->title)>0,"both directions have native click targets");
                    check(scrollToHeadingId(app,ref->url.substr(1)),"native jump to footnote resolves");
                    check(scrollToHeadingId(app,ref->title),"native return to reference resolves");
                }
                auto text=app.docText;auto anchors=app.footnoteAnchors;
                app.scrollY=app.targetScrollY=0;
                layoutDocumentViewportFirst(app);ensureLayoutComplete(app);
                check(text==app.docText && anchors==app.footnoteAnchors,"full and incremental footnote layouts match");
            }
        }
    }
    fs::path output=fs::current_path()/"footnote-test-output";fs::create_directories(output);
    check(exportHtmlFile(app,(output/"footnotes.html").wstring()),"HTML footnote export succeeds");
    auto html=read(output/"footnotes.html");
    for(const auto& ref:refs)check(html.find("id=\""+ref->title+"\"")!=std::string::npos && html.find("href=\"#"+ref->title+"\"")!=std::string::npos,"HTML IDs and return links pair");
    check(exportDocxFile(app,(output/"footnotes.docx").wstring()),"Word footnote export succeeds");
    auto zip=read(output/"footnotes.docx");
    check(zip.find("word/footnotes.xml")!=std::string::npos && zip.find("relationships/footnotes")!=std::string::npos,"Word footnote part and relationship exist");
    check(zip.find("<w:footnoteReference w:id=\"1\"/>")!=std::string::npos && zip.find("<w:footnote w:id=\"1\">")!=std::string::npos,"Word native reference targets real footnote");
    check(zip.find("NOTEREF _tinta_fn_1")!=std::string::npos,"repeated Word references use a cross-reference field");
    DestroyWindow(app.hwnd);app.hwnd=nullptr;state.reset();CoUninitialize();
    std::cout<<"Footnotes: "<<failures<<" failures\n";return failures?1:0;
}
