#include "d2d_init.h"
#include "export.h"
#include "render.h"
#include "utils.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
std::string read(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()};
}
bool same(D2D1_COLOR_F a, D2D1_COLOR_F b) {
    return std::abs(a.r-b.r)<.001f && std::abs(a.g-b.g)<.001f && std::abs(a.b-b.b)<.001f && std::abs(a.a-b.a)<.001f;
}
void runColor(App& app, const wchar_t* text, D2D1_COLOR_F color) {
    bool found=false;
    for (const auto& r : app.layoutTextRuns)
        if (r.docLength && app.docText.substr(r.docStart,r.docLength)==text) {
            found=true; check(same(r.color,color), "native heading/highlight uses theme colour");
        }
    check(found,"native marker renders");
}
}

int testThemeColours(App& app, const std::filesystem::path& dir) {
    auto fixtures=std::filesystem::path(TINTA_THEME_FIXTURE).parent_path();
    int first=themeCount();
    { std::ofstream f(dir/"themes.ini",std::ios::app); f << '\n' << read(fixtures/"theme-colours.ini"); }
    loadCustomThemes();
    check(themeCount()==first+3,"three colour test themes load");
    const auto& invalid=themeAt(first+2);
    for(int i=1;i<=6;++i) check(same(themeHeadingColor(invalid,i),invalid.heading),"invalid/missing heading override inherits");
    check(!invalid.highlightBackground && !invalid.highlightText,"invalid highlight colours retain defaults");
    for(int index : {first, first+1}) {
        applyTheme(app,index);
        auto theme=app.theme;
        for(float scale : {1.0f,1.5f}) {
            app.contentScale=scale; updateTextFormats(app);
            for(int width : {1050,650}) {
                app.width=width;
                app.root=app.parser.parse(read(fixtures/"theme-colours.md")).root;
                layoutDocument(app);
                const wchar_t* headings[]={L"Heading_one",L"Heading_two",L"Heading_three",L"Heading_four",L"Heading_five",L"Heading_six"};
                for(int i=0;i<6;++i) runColor(app,headings[i],themeHeadingColor(theme,i+1));
                for(auto marker : {L"Marked_plain",L"Marked_table",L"Marked_quote",L"Marked_list",L"Marked_bold"})
                    runColor(app,marker,*theme.highlightText);
                bool background=false;
                for(const auto& rect:app.layoutRects) if(same(rect.color,*theme.highlightBackground)) background=true;
                check(background,"native marked-text background uses explicit theme colour");
                auto runs=app.layoutTextRuns; float height=app.contentHeight;
                for(auto& color:app.theme.headingColors)color.reset();
                app.theme.highlightBackground.reset(); app.theme.highlightText.reset();
                layoutDocument(app);
                check(runs.size()==app.layoutTextRuns.size() && height==app.contentHeight,"theme overrides do not change geometry");
                for(size_t i=0;i<runs.size()&&i<app.layoutTextRuns.size();++i)
                    check(runs[i].pos.x==app.layoutTextRuns[i].pos.x && runs[i].pos.y==app.layoutTextRuns[i].pos.y,"mixed content stays in place");
                app.theme=theme;
            }
        }
        check(exportHtmlFile(app,(dir/"colours.html").wstring()),"HTML colour export succeeds");
        auto html=read(dir/"colours.html");
        check(html.find(index==first ? "h1{color:#aa3344;border-color:#aa3344;}" : "h1{color:#ff99aa;border-color:#ff99aa;}")!=std::string::npos,"HTML heading override");
        check(html.find(index==first ? "mark{background:#99ddcc;color:#102030;" : "mark{background:#ccbbdd;color:#112233;")!=std::string::npos,"HTML marked-text colours");
        check(exportDocxFile(app,(dir/"colours.docx").wstring()),"DOCX colour export succeeds");
        auto zip=read(dir/"colours.docx");
        check(zip.find(index==first ? "w:fill=\"99DDCC\"" : "w:fill=\"CCBBDD\"")!=std::string::npos,"DOCX arbitrary highlight shading");
        check(zip.find(index==first ? "w:val=\"AA3344\"" : "w:val=\"FF99AA\"")!=std::string::npos,"DOCX heading override even for dark theme");
    }
    auto saved=themeAt(first);
    std::wstring font=saved.fontFamily, codeFont=saved.codeFontFamily;
    int index=saveCustomTheme(saved,L"208 saved",font,codeFont);
    loadCustomThemes();
    check(same(*themeAt(index).headingColors[5],hexColor(0x008877)),"H6 survives editor save and reload");
    check(same(*themeAt(index).highlightText,hexColor(0x102030)),"highlight text survives editor save and reload");
    saved.headingColors[0].reset(); saved.highlightText.reset();
    saveCustomTheme(saved,L"208 saved",font,codeFont);
    loadCustomThemes();
    check(!themeAt(index).headingColors[0] && !themeAt(index).highlightText,"clearing editor overrides persists inheritance");
    return failures;
}
