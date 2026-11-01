#include "d2d_init.h"
#include "export.h"
#include "render.h"
#include "utils.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <vector>

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

namespace {
// The colour the page draws a word in: the run holding its first letter
bool wordColor(const App& app, const wchar_t* word, D2D1_COLOR_F& color) {
    size_t at = app.docText.find(word);
    for (const auto& r : app.layoutTextRuns)
        if (at != std::wstring::npos && r.docLength && at >= r.docStart && at < r.docStart + r.docLength) {
            color = r.color;
            return true;
        }
    return false;
}
// The Word run that writes exactly this text
std::string wordRun(const std::string& zip, const std::string& text) {
    size_t at = zip.find(">" + text + "</w:t>");
    size_t start = at == std::string::npos ? at : zip.rfind("<w:r>", at);
    return start == std::string::npos ? std::string() : zip.substr(start, at - start);
}
void resetEmphasis(D2DTheme& t) {
    t.boldText.reset(); t.italicText.reset(); t.boldItalicText.reset(); t.selection.reset(); t.rule.reset();
}
// Lines across the whole column in a colour: the page's rules (a table's
// borders share the quote border's colour but stop at the table's edge)
int rules(const App& app, D2D1_COLOR_F color) {
    float right = 0;
    for (const auto& l : app.layoutLines) right = std::max(right, l.p2.x);
    int n = 0;
    for (const auto& l : app.layoutLines)
        if (same(l.color, color) && l.p1.y == l.p2.y && l.p2.x >= right - 0.5f && l.p2.x - l.p1.x > app.width * 0.3f) ++n;
    return n;
}
}

// #310: bold=, italic=, bolditalic=, selection= and hr= colour their parts
// of the page and the exports; missing ones keep what was there
int testEmphasisColours(App& app, const std::filesystem::path& dir) {
    const int before=failures;
    auto fixtures=std::filesystem::path(TINTA_THEME_FIXTURE).parent_path();
    int first=themeCount();
    { std::ofstream f(dir/"themes.ini",std::ios::app); f << '\n' << read(fixtures/"theme-emphasis-310.ini"); }
    loadCustomThemes();
    check(themeCount()==first+5,"#310: five emphasis test themes load");
    const auto& all=themeAt(first);
    check(all.boldText && same(*all.boldText,hexColor(0xAA2200)) && all.italicText && same(*all.italicText,hexColor(0x007755)) &&
          all.boldItalicText && same(*all.boldItalicText,hexColor(0x7722AA)) && all.selection && same(*all.selection,hexColor(0xCC0000)) &&
          all.rule && same(*all.rule,hexColor(0x00AA44)),"#310: every key loads");
    check(themeAt(first+1).boldText && same(*themeAt(first+1).boldText,hexColor(0xFF9E64)),"#310: a # and lower case load");
    const auto& invalid=themeAt(first+4);
    check(!invalid.boldText && !invalid.italicText && !invalid.boldItalicText && !invalid.selection && !invalid.rule,
          "#310: invalid colours keep inheritance");
    for(int i=0;i<THEME_COUNT;++i)
        check(!themeAt(i).boldText && !themeAt(i).italicText && !themeAt(i).boldItalicText && !themeAt(i).selection &&
              !themeAt(i).rule,"#310: built-in themes look as before");
    check(same(*themeEmphasisColor(themeAt(first+2),true,true),hexColor(0xAA2200)),"#310: bold and italic text takes bold= without bolditalic=");
    check(same(*themeEmphasisColor(themeAt(first+3),true,true),hexColor(0x007755)) && !themeEmphasisColor(themeAt(first+3),true,false),
          "#310: with italic= alone, text both takes it and bold keeps its colour");
    const D2D1_COLOR_F own=D2D1::ColorF(0.2f,0.4f,0.9f,0.35f);
    check(same(themeSelectionColor(all,own),D2D1::ColorF(0.8f,0.0f,0.0f,0.35f)) && same(themeSelectionColor(invalid,own),own),
          "#310: the selection takes the theme's hue at the view's strength");
    std::string tmpl=themesIniTemplate();
    check(tmpl.find("; bold=")!=std::string::npos && tmpl.find("bolditalic=")!=std::string::npos &&
          tmpl.find("; selection=")!=std::string::npos && tmpl.find("; hr=")!=std::string::npos,"#310: a new themes.ini names the keys");

    const wchar_t* bold[]={L"Bold_heading",L"Bold_plain",L"Bold_under",L"Mark_in_bold",L"Bold_in_mark",L"Bold_struck",L"Bold_list",
        L"Bold_task",L"Bold_numbered",L"Bold_quote",L"Bold_callout",L"Bold_table",L"Bold_last",L"Bold_footnote"};
    const wchar_t* italic[]={L"Italic_heading",L"Italic_plain",L"Italic_under",L"Italic_list",L"Italic_task",L"Italic_numbered",
        L"Italic_quote",L"Italic_table"};
    const wchar_t* both[]={L"Both_plain",L"Both_nested",L"Both_inner",L"Both_list",L"Both_quote",L"Both_table"};
    const wchar_t* kept[]={L"Plain_text",L"Link_in_bold",L"Bold_in_link",L"Code_in_bold",L"Final_paragraph"};
    for(int index=first;index<first+5;++index) {
        applyTheme(app,index);
        const D2DTheme theme=app.theme;
        for(float scale : {1.0f,1.5f}) {
            app.contentScale=scale; updateTextFormats(app);
            for(int width : {1050,650}) {
                app.width=width;
                app.root=app.parser.parse(read(fixtures/"theme-emphasis-310.md")).root;
                resetEmphasis(app.theme);
                layoutDocument(app);
                auto base=app.layoutTextRuns; float height=app.contentHeight;
                std::vector<D2D1_COLOR_F> colors;
                auto colorsOf=[&](const wchar_t* const* words,size_t n) {
                    for(size_t i=0;i<n;++i) { D2D1_COLOR_F c{}; check(wordColor(app,words[i],c),"#310: the fixture's words render"); colors.push_back(c); }
                };
                colorsOf(bold,std::size(bold)); colorsOf(italic,std::size(italic)); colorsOf(both,std::size(both)); colorsOf(kept,std::size(kept));
                check(rules(app,app.theme.blockquoteBorder)==4,"#310: without hr= the three rules and the footnotes' keep the quote border's colour");
                app.theme=theme;
                layoutDocument(app);
                check(base.size()==app.layoutTextRuns.size() && height==app.contentHeight,"#310: the colours leave the layout as it was");
                for(size_t i=0;i<base.size()&&i<app.layoutTextRuns.size();++i)
                    check(base[i].pos.x==app.layoutTextRuns[i].pos.x && base[i].pos.y==app.layoutTextRuns[i].pos.y,"#310: mixed content stays in place");
                size_t k=0;
                auto expect=[&](const wchar_t* const* words,size_t n,std::optional<D2D1_COLOR_F> color,const char* what) {
                    for(size_t i=0;i<n;++i,++k) {
                        D2D1_COLOR_F c{};
                        bool found=wordColor(app,words[i],c);
                        if(!found || !same(c,color.value_or(colors[k]))) std::wcerr << L"  " << words[i] << L'\n';
                        check(found && same(c,color.value_or(colors[k])),what);
                    }
                };
                expect(bold,std::size(bold),themeEmphasisColor(theme,true,false),"#310: bold text takes bold=, in every block");
                expect(italic,std::size(italic),themeEmphasisColor(theme,false,true),"#310: italic text takes italic=, in every block");
                expect(both,std::size(both),themeEmphasisColor(theme,true,true),"#310: text both takes bolditalic=, else bold=, else italic=");
                expect(kept,std::size(kept),std::nullopt,"#310: plain text, links, code and fences keep their colours");
                check(rules(app,themeRuleColor(theme))==4,"#310: the rules take hr=, the footnotes' too");
            }
        }
        check(exportHtmlFile(app,(dir/"emphasis.html").wstring()),"#310: HTML export succeeds");
        auto html=read(dir/"emphasis.html");
        if(index==first) {
            for(const char* rule : {"strong{color:#aa2200;}","em{color:#007755;}","strong em,em strong{color:#7722aa;}a strong,a em{color:inherit;}",
                                    "::selection{background:rgba(204,0,0,0.35);}","hr{border:none;border-top:1px solid #00aa44;"})
                check(html.find(rule)!=std::string::npos,"#310: HTML carries the theme's emphasis, selection and rule colours");
        } else if(index==first+3) {
            check(html.find("}strong{color:")==std::string::npos && html.find("}em{color:#007755;}")!=std::string::npos &&
                  html.find("strong em,em strong{color:#007755;}")!=std::string::npos,"#310: HTML with italic= alone");
        } else if(index==first+4) {
            check(html.find("}strong{color:")==std::string::npos && html.find("}em{color:")==std::string::npos &&
                  html.find("::selection")==std::string::npos,"#310: HTML without the keys is as before");
        }
        check(exportDocxFile(app,(dir/"emphasis.docx").wstring()),"#310: DOCX export succeeds");
        auto zip=read(dir/"emphasis.docx");
        if(index==first) {
            check(wordRun(zip,"Bold_plain").find("<w:color w:val=\"AA2200\"/>")!=std::string::npos &&
                  wordRun(zip,"Italic_quote").find("<w:color w:val=\"007755\"/>")!=std::string::npos &&
                  wordRun(zip,"Both_table").find("<w:color w:val=\"7722AA\"/>")!=std::string::npos,"#310: Word colours bold and italic runs");
            check(wordRun(zip,"Link_in_bold").find("<w:color w:val=\"2B6CB0\"/>")!=std::string::npos &&
                  wordRun(zip,"Bold_in_link").find("<w:color w:val=\"2B6CB0\"/>")!=std::string::npos,"#310: Word links keep their colour");
            check(!wordRun(zip,"Code_in_bold").empty() && wordRun(zip,"Code_in_bold").find("AA2200")==std::string::npos,
                  "#310: Word code inside bold keeps its own colour");
            check(zip.find("w:sz=\"6\" w:space=\"1\" w:color=\"00AA44\"")!=std::string::npos,"#310: Word rules take hr=");
        } else if(index==first+4) {
            check(wordRun(zip,"Bold_plain").find("<w:color")==std::string::npos,"#310: Word without the keys is as before");
        }
    }
    auto saved=themeAt(first);
    std::wstring font=saved.fontFamily, codeFont=saved.codeFontFamily;
    int index=saveCustomTheme(saved,L"310 saved",font,codeFont);
    loadCustomThemes();
    const auto& back=themeAt(index);
    check(back.boldText && same(*back.boldText,hexColor(0xAA2200)) && back.italicText && back.boldItalicText &&
          back.selection && same(*back.selection,hexColor(0xCC0000)) && back.rule && same(*back.rule,hexColor(0x00AA44)),
          "#310: the keys survive a save from the editor and a reload");
    resetEmphasis(saved);
    saveCustomTheme(saved,L"310 saved",font,codeFont);
    loadCustomThemes();
    check(!themeAt(index).boldText && !themeAt(index).selection && !themeAt(index).rule,"#310: cleared keys stay cleared");
    app.contentScale=1.0f; updateTextFormats(app);
    return failures-before;
}
