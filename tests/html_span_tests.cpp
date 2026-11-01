// Inline HTML <sub>, <sup>, <ins>/<u>, <del>/<s> and <mark> (#263): the
// parsed tree, the drawn layout and both exports
#include "document.h"
#include "d2d_init.h"
#include "export.h"
#include "render.h"
#include "utils.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>

namespace {
int failures = 0;
void check(bool value, const std::string& message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
std::string read(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()};
}

// Compact tree text: 'text' for leaves, Type[children] for spans
std::string shape(const ElementPtr& e) {
    if (e->type == ElementType::Text) return "'" + e->text + "'";
    std::string s = qmd::elementTypeToString(e->type);
    if (e->type == ElementType::Code && !e->children.empty()) {
        std::string text;
        for (const auto& c : e->children) text += c->text;
        return s + "['" + text + "']";
    }
    if (e->children.empty()) return s;
    s += "[";
    for (size_t i = 0; i < e->children.size(); i++) {
        if (i) s += " ";
        s += shape(e->children[i]);
        check(e->children[i]->parent == e.get(), "parent links stay valid");
    }
    return s + "]";
}

std::string parsed(App& app, const std::string& source) {
    auto result = app.parser.parse(source);
    check(result.success, "parses: " + source);
    if (!result.success || !result.root) return "";
    std::string s;
    for (size_t i = 0; i < result.root->children.size(); i++) {
        if (i) s += " ";
        s += shape(result.root->children[i]);
    }
    return s;
}

void expect(App& app, const std::string& source, const std::string& want) {
    std::string got = parsed(app, source);
    check(got == want, source + "\n  want " + want + "\n  got  " + got);
}

void parserTests(App& app) {
    expect(app, "H<sub>2</sub>O and x<sup>2</sup>\n",
           "Paragraph['H' Subscript['2'] 'O and x' Superscript['2']]");
    expect(app, "<ins>a</ins> <u>b</u> <del>c</del> <s>d</s> <strike>e</strike> <mark>f</mark>\n",
           "Paragraph[Underline['a'] ' ' Underline['b'] ' ' Strikethrough['c'] ' ' "
           "Strikethrough['d'] ' ' Strikethrough['e'] ' ' Highlight['f']]");
    expect(app, "<ins>a **b** c</ins>\n",
           "Paragraph[Underline['a ' Strong['b'] ' c']]");
    // A Markdown span closing first ends the HTML spans opened inside it
    expect(app, "**a <sub>b** c</sub> d\n",
           "Paragraph[Strong['a ' Subscript['b']] ' c d']");
    // A close tag inside a Markdown span closes its span there, and the
    // Markdown span carries on after it, as a browser reads the misnesting
    expect(app, "<ins>a *b</ins> c* d\n",
           "Paragraph[Underline['a ' Emphasis['b']] Emphasis[' c'] ' d']");
    expect(app, "<ins>a [link *b</ins> c*](https://example.com) d\n",
           "Paragraph[Underline['a ' Link['link ' Emphasis['b']]] Link[Emphasis[' c']] ' d']");
    expect(app, "<sub>a <sub>b *c</sub> d</sub> e* f\n",
           "Paragraph[Subscript['a ' Subscript['b ' Emphasis['c']] Emphasis[' d']] Emphasis[' e'] ' f']");
    // A quoted '>' does not end a tag split across lines; a comment's does
    expect(app, "<sub title=\"a>\nb\">x</sub> y\n", "Paragraph[Subscript['x'] ' y']");
    expect(app, "a <!-- it's --><sub>2</sub>\n", "Paragraph['a <!-- it's -->' Subscript['2']]");
    // A comment spread over lines stays text, whatever tags it mentions
    expect(app, "x <!-- old: a>\n<sub>\nb --> y\n", "Paragraph['x <!-- old: a>\n<sub>\nb --> y']");
    // ~~, ==, ^ and [[wiki|links]] pair across inline HTML spans
    expect(app, "~~H<sub>2</sub>O~~ and ==E = mc<sup>2</sup>== end\n",
           "Paragraph[Strikethrough['H' Subscript['2'] 'O'] ' and ' Highlight['E = mc' Superscript['2']] ' end']");
    expect(app, "~~old <ins>new</ins> text~~ <sub>x ~~y~~</sub>\n",
           "Paragraph[Strikethrough['old ' Underline['new'] ' text'] ' ' Subscript['x ' Strikethrough['y']]]");
    expect(app, "[[Water|H<sub>2</sub>O]] and [[Plain]] but [[H<sub>2</sub>O]]\n",
           "Paragraph[Link['H' Subscript['2'] 'O'] ' and ' Link['Plain'] ' but [[H' Subscript['2'] 'O]]']");
    expect(app, "[[a]] b [[]] c\n", "Paragraph[Link['a'] ' b [[]] c']");
    // Spans opened inside the closed one close with it
    expect(app, "<sub>a <sup>b</sub> c</sup> d\n",
           "Paragraph[Subscript['a ' Superscript['b']] ' c d']");
    // A tag whose attributes wrap onto the next line still counts
    expect(app, "<sub\ntitle=\"x\">a</sub>b <span\nclass=\"y\">c</span>\n",
           "Paragraph[Subscript['a'] 'b <span\nclass=\"y\">c</span>']");
    expect(app, "a <sup>b\n\nnext\n",
           "Paragraph['a ' Superscript['b']] Paragraph['next']");
    expect(app, "stray </sub> end\n", "Paragraph['stray  end']");
    expect(app, "<SUB class=\"x\">u</SUB> <Ins title='t'>v</INS>\n",
           "Paragraph[Subscript['u'] ' ' Underline['v']]");
    expect(app, "<sub/>x <subx>y</subx>\n",
           "Paragraph['<sub/>x <subx>y</subx>']");
    expect(app, "`<sub>x</sub>` and <ins>`code`</ins>\n",
           "Paragraph[Code['<sub>x</sub>'] ' and ' Underline[Code['code']]]");
    expect(app, "<ins>==both== x^2^</ins>\n",
           "Paragraph[Underline[Highlight['both'] ' x' Superscript['2']]]");
    expect(app, "[x<sup>2</sup>](https://example.com)\n",
           "Paragraph[Link['x' Superscript['2']]]");
    expect(app, "# H<sub>2</sub>O\n", "Heading['H' Subscript['2'] 'O']");
    expect(app, "| a | b |\n|---|---|\n| H<sub>2</sub>O | <ins>x\n",
           "Table[TableRow[TableCell['a'] TableCell['b']] "
           "TableRow[TableCell['H' Subscript['2'] 'O'] TableCell[Underline['x']]]]");
    // A sublist after a tight item's text ends that text's open spans
    expect(app, "- a <sub>b\n  - c\n- d\n",
           "List[ListItem['a ' Subscript['b'] List[ListItem['c']]] ListItem['d']]");
    expect(app, "> <ins>quoted\n> more</ins> after\n",
           "BlockQuote[Paragraph[Underline['quoted' SoftBreak 'more'] ' after']]");
    // In an HTML block, spaces beside inline tags stay single spaces
    expect(app, "<p>H<sub>2</sub>O <ins>new</ins> <u>u</u></p>\n",
           "HtmlBlock[Paragraph['H' Subscript['2'] 'O ' Underline['new'] ' ' Underline['u']]]");
    expect(app, "<p>a <b>b</b> c <i>d</i>, <code>e</code> f <a href=\"u\">g</a> h</p>\n",
           "HtmlBlock[Paragraph['a ' Strong['b'] ' c ' Emphasis['d'] ', ' Code['e'] ' f ' Link['g'] ' h']]");
    // A close tag closes its own element: a stray one is ignored
    expect(app, "<p>Formula a</sup> b and <ins>c</ins> end</p>\n",
           "HtmlBlock[Paragraph['Formula a b and ' Underline['c'] ' end']]");
    expect(app, "<p><ins>a <b>b</ins> c</b></p>\n",
           "HtmlBlock[Paragraph[Underline['a ' Strong['b']] ' c']]");
    // Omitted </li> tags close siblings; a nested list still belongs to its item.
    expect(app, "<ul><li>a<li>b</ul>\n<p>after</p>\n",
           "HtmlBlock[List[ListItem['a'] ListItem['b']] Paragraph['after']]");
    expect(app, "<ol><li><b>a<li><i>b</ol>\n<p>after</p>\n",
           "HtmlBlock[List[ListItem[Strong['a']] ListItem[Emphasis['b']]] Paragraph['after']]");
    expect(app, "<ul><li>a<ul><li>b<li>c</ul><li>d</ul>\n",
           "HtmlBlock[List[ListItem['a' List[ListItem['b'] ListItem['c']]] ListItem['d']]]");
    expect(app, "<ul><li>a</li><li>b</li></ul>\n",
           "HtmlBlock[List[ListItem['a'] ListItem['b']]]");
    // Markdown list HTML has an extra wrapper; raw HTML list children do not.
    expect(app, "- <h3>Title</h3>\n", "List[ListItem[HtmlBlock[Heading['Title']]]]");
    expect(app, "1. <h3>Title</h3>\n", "List[ListItem[HtmlBlock[Heading['Title']]]]");
    expect(app, "<ul><li><a id=\"target\"></a><h3>Title</h3></li></ul>\n",
           "HtmlBlock[List[ListItem[Anchor Heading['Title']]]]");
    expect(app, "<ol><li><a id=\"target\"></a><h3>Title</h3></li></ol>\n",
           "HtmlBlock[List[ListItem[Anchor Heading['Title']]]]");
    // Inside <pre> and <code> the tags go and the text stays
    expect(app, "<p><code>H<sub>2</sub>O</code> and text</p>\n",
           "HtmlBlock[Paragraph[Code['H2O'] ' and text']]");
    expect(app, "<pre>a<ins>b</ins>c</pre>\n", "HtmlBlock[CodeBlock['abc']]");
    // One space, never two, around anchors and inside inline tags
    expect(app, "<p>word <a id=\"x\"></a> more <b> bold </b> next</p>\n",
           "HtmlBlock[Paragraph['word ' Anchor 'more ' Strong['bold '] 'next']]");
    expect(app, "<p><a id=\"t\">Title</a> next</p>\n",
           "HtmlBlock[Paragraph[Anchor['Title'] ' next']]");
    expect(app, "<p>word <ruby>base<rt>top</rt></ruby> next</p>\n",
           "HtmlBlock[Paragraph['word ' Ruby['base' RubyText['top']] ' next']]");
    // A linked logo or badge in an HTML heading adds no space to its title
    expect(app, "<h1><a href=\"x\"><img src=\"logo.png\"></a> MyProject</h1>\n",
           "HtmlBlock[Heading[Link 'MyProject']]");
    {
        auto doc = app.parser.parse("<h2>Installation <a href=\"ci\"><img src=\"b.svg\"></a></h2>\n"
                                    "<h1><a href=\"x\"><img src=\"l.png\"></a> MyProject</h1>\n");
        applyTheme(app, 0);
        updateTextFormats(app);
        app.width = 1050;
        app.height = 900;
        app.root = doc.root;
        layoutDocument(app);
        check(app.headings.size() == 2 && app.headings[0].id == "installation" &&
              app.headings[1].id == "myproject" && app.headings[1].text == L"MyProject",
              "HTML headings beside badges keep their slugs and titles");
    }
    // A block ends without the space kept before an inline tag
    expect(app, "<h2><b>Title </b></h2>\n<h2>Installation <a href=\"ci\"><img src=\"b.svg\"></a></h2>\n",
           "HtmlBlock[Heading[Strong['Title']] Heading['Installation' Link]]");
    expect(app, "<h2>Install <a id=\"install\"></a>\n", "HtmlBlock[Heading['Install' Anchor]]");
    // An escaped footnote bracket inside raw HTML stays literal
    {
        auto doc = app.parser.parse("A[^1] <span title=\"\\[^1]\">x</span> <kbd\n>y</kbd>\n\n[^1]: note\n");
        int refs = 0;
        std::function<void(const ElementPtr&)> count = [&](const ElementPtr& e) {
            if (e->type == ElementType::FootnoteReference) refs++;
            for (const auto& c : e->children) count(c);
        };
        if (doc.success) count(doc.root);
        check(refs == 1, "an escaped [^1] inside raw HTML is not a footnote reference");
    }
    // A wrapper alone on its line draws nothing
    expect(app, "Para A\n\n<sub>\n\nSmall print.\n\n</sub>\n\nPara B\n",
           "Paragraph['Para A'] HtmlBlock Paragraph['Small print.'] HtmlBlock Paragraph['Para B']");
}

struct Range { size_t start, end; };
Range findText(const App& app, const std::wstring& text, size_t from = 0) {
    size_t at = app.docText.find(text, from);
    return at == std::wstring::npos ? Range{0, 0} : Range{at, at + text.size()};
}

float runBaseline(const App::LayoutTextRun& run) {
    DWRITE_LINE_METRICS metrics{};
    UINT32 count = 0;
    if (!run.layout || FAILED(run.layout->GetLineMetrics(&metrics, 1, &count)) || !count) {
        return run.pos.y;
    }
    return run.pos.y + metrics.baseline;
}

std::vector<const App::LayoutTextRun*> runsIn(const App& app, Range r) {
    std::vector<const App::LayoutTextRun*> out;
    for (const auto& run : app.layoutTextRuns) {
        if (run.docLength && run.docStart < r.end && run.docStart + run.docLength > r.start) {
            out.push_back(&run);
        }
    }
    return out;
}

bool horizontal(const App::LayoutLine& line) { return std::abs(line.p1.y - line.p2.y) < 0.01f; }

// Every drawn run of the range has a rule under its baseline, inside its
// line box, and the rules on each line join without gaps from the range's
// first ink to its last
void checkUnderlined(const App& app, Range r, const std::string& what) {
    auto runs = runsIn(app, r);
    check(!runs.empty(), what + ": text is drawn");
    struct Row { float top, bottom, left, right; };
    std::vector<Row> rows;
    for (const auto* run : runs) {
        float baseline = runBaseline(*run);
        bool found = false;
        for (const auto& line : app.layoutLines) {
            if (!horizontal(line)) continue;
            float y = line.p1.y;
            if (y > baseline && y < run->bounds.bottom + 1.0f &&
                line.p1.x <= run->bounds.left + 0.5f && line.p2.x >= run->bounds.left + 1.0f) {
                found = true;
            }
        }
        check(found, what + ": a run has an underline below its baseline");
        bool merged = false;
        for (auto& row : rows) {
            if (std::abs(row.top - run->bounds.top) < 0.5f) {
                row.left = std::min(row.left, run->bounds.left);
                row.right = std::max(row.right, run->bounds.right);
                row.bottom = std::max(row.bottom, run->bounds.bottom);
                merged = true;
            }
        }
        if (!merged) {
            rows.push_back({run->bounds.top, run->bounds.bottom, run->bounds.left, run->bounds.right});
        }
    }
    for (const auto& row : rows) {
        // Union of the rules lying in this line box
        std::vector<std::pair<float, float>> spans;
        for (const auto& line : app.layoutLines) {
            if (!horizontal(line) || line.p1.y <= row.top || line.p1.y > row.bottom + 1.0f) continue;
            if (line.p2.x < row.left || line.p1.x > row.right) continue;
            spans.push_back({line.p1.x, line.p2.x});
        }
        std::sort(spans.begin(), spans.end());
        float covered = row.left;
        for (const auto& s : spans) {
            if (s.first <= covered + 0.75f) covered = std::max(covered, s.second);
        }
        check(covered >= row.right - 0.75f,
              what + ": the underline runs unbroken across the line");
    }
}

void checkNoUnderline(const App& app, Range r, const std::string& what) {
    for (const auto* run : runsIn(app, r)) {
        float baseline = runBaseline(*run);
        for (const auto& line : app.layoutLines) {
            if (!horizontal(line)) continue;
            bool under = line.p1.y > baseline && line.p1.y < run->bounds.bottom + 1.0f &&
                         line.p1.x < run->bounds.right - 1.0f && line.p2.x > run->bounds.left + 1.0f;
            check(!under, what + ": stays without an underline");
        }
    }
}

// A rule through the middle of a run: above its baseline, across its ink
bool struck(const App& app, const App::LayoutTextRun& run) {
    for (const auto& line : app.layoutLines) {
        if (horizontal(line) && line.p1.y > run.bounds.top && line.p1.y < runBaseline(run) &&
            line.p1.x <= run.bounds.left + 0.5f && line.p2.x >= run.bounds.right - 0.5f) {
            return true;
        }
    }
    return false;
}

// <del> and ~~ strike a code chip inside them as they strike text
void strikeTests(App& app) {
    auto doc = app.parser.parse("Struck ~~`struck_code()`~~ and <del>`del_code()`</del> beside "
                                "~~plain struck~~ and `plain_code()` words.\n");
    check(doc.success, "the strike document parses");
    if (!doc.success) return;
    app.height = 700;
    app.readingWidthPct = 100;
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (float scale : {1.0f, 1.5f}) {
            app.contentScale = scale;
            updateTextFormats(app);
            for (int width : {1050, 650}) {
                app.width = width;
                app.root = doc.root;
                layoutDocument(app);
                ensureLayoutComplete(app);
                for (const wchar_t* words : {L"struck_code()", L"del_code()", L"plain struck"}) {
                    auto runs = runsIn(app, findText(app, words));
                    check(!runs.empty(), "struck text is drawn");
                    for (const auto* run : runs) {
                        check(struck(app, *run), "a code chip or text inside ~~ or <del> is struck through");
                    }
                }
                for (const auto* run : runsIn(app, findText(app, L"plain_code()"))) {
                    check(!struck(app, *run), "a code chip outside them is not");
                }
            }
        }
    }
}

void layoutTests(App& app, const ElementPtr& root) {
    app.height = 700;
    app.readingWidthPct = 100;
    int checkedLayouts = 0;
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (float scale : {1.0f, 1.5f}) {
            app.contentScale = scale;
            updateTextFormats(app);
            for (int width : {1050, 650}) {
                app.width = width;
                app.root = root;
                layoutDocument(app);
                ensureLayoutComplete(app);
                checkedLayouts++;
                const auto& doc = app.docText;
                check(doc.find(L"<ins>") == doc.rfind(L"<ins>") && doc.find(L"<ins>") != std::wstring::npos,
                      "only the fenced <ins> stays literal");
                check(doc.find(L"<sub>not sub</sub>") != std::wstring::npos, "inline code keeps its tags");
                check(doc.find(L"</sub> tag") == std::wstring::npos, "a stray close tag draws nothing");
                check(doc.find(L"<SUB") == std::wstring::npos && doc.find(L"upper") != std::wstring::npos,
                      "upper-case tags with attributes are spans");
                check(doc.find(L"<p>") == std::wstring::npos && doc.find(L"HTML block") != std::wstring::npos,
                      "the HTML block's spans render");
                check(doc.find(L"final paragraph stays visible") != std::wstring::npos,
                      "content after the spans stays visible");

                // Subscripts drop, superscripts use the small face
                Range water = findText(app, L"Water is H2O");
                auto body = runsIn(app, {water.start, water.start + 5});
                auto sub = runsIn(app, {water.start + 10, water.start + 11});
                check(body.size() == 1 && sub.size() == 1, "body and subscript runs are drawn");
                if (body.size() == 1 && sub.size() == 1) {
                    check(runBaseline(*sub[0]) > runBaseline(*body[0]) + 1.0f,
                          "the subscript sits below the body baseline");
                    check(sub[0]->layout->GetFontSize() < body[0]->layout->GetFontSize(),
                          "the subscript is smaller");
                }
                Range area = findText(app, L"r2, and");
                auto sup = runsIn(app, {area.start + 1, area.start + 2});
                check(sup.size() == 1 && !body.empty() &&
                      sup[0]->layout->GetFontSize() < body[0]->layout->GetFontSize() &&
                      runBaseline(*sup[0]) < runBaseline(*body[0]),
                      "the superscript is small and raised");

                // In a heading, scripts scale with it and keep to its baseline
                Range title = findText(app, L"Chemistry H2O and E = mc2");
                auto heading = runsIn(app, {title.start, title.start + 9});
                auto titleSub = runsIn(app, {title.start + 11, title.start + 12});
                auto titleSup = runsIn(app, {title.end - 1, title.end});
                auto beforeSup = runsIn(app, {title.end - 2, title.end - 1});  // the c of mc, on its line
                check(heading.size() == 1 && titleSub.size() == 1 && titleSup.size() == 1 &&
                      beforeSup.size() == 1, "the heading's scripts are drawn");
                if (heading.size() == 1 && titleSub.size() == 1 && titleSup.size() == 1 &&
                    beforeSup.size() == 1) {
                    const float size = heading[0]->layout->GetFontSize();
                    check(std::abs(titleSub[0]->layout->GetFontSize() - size * 0.68f) < 0.05f &&
                          std::abs(titleSup[0]->layout->GetFontSize() - size * 0.68f) < 0.05f,
                          "heading scripts are in proportion to the heading");
                    check(runBaseline(*titleSub[0]) > runBaseline(*heading[0]) + 1.0f,
                          "a heading subscript drops below the heading baseline");
                    check(runBaseline(*titleSup[0]) < runBaseline(*beforeSup[0]) - 1.0f &&
                          titleSup[0]->pos.y >= titleSup[0]->bounds.top - 0.01f,
                          "a heading superscript rises within its line");
                }

                checkUnderlined(app, findText(app, L"inserted text"), "<ins>");
                checkUnderlined(app, findText(app, L"underlined bold and italic and code"),
                                "<ins> around bold, italic and code");
                checkUnderlined(app, findText(app, L"a plain u tag"), "<u>");
                checkUnderlined(app, findText(app, L"\u4E0B\u5212\u7EBF\u4E2D\u6587"), "Chinese <ins>");
                checkUnderlined(app, findText(app, L"inserted words that keep going"), "wrapped quote <ins>");
                checkUnderlined(app, findText(app, L"nested underline"), "nested list <u>");
                checkUnderlined(app, findText(app, L"water"), "table cell <ins>");
                checkNoUnderline(app, findText(app, L"removed text"), "<del>");
                // Two separate spans: the comma between them stays plain
                Range comma = findText(app, L", a plain u tag");
                checkNoUnderline(app, {comma.start, comma.start + 1}, "the comma between two spans");

                // A link inside <ins> keeps one rule, its own
                for (const auto* run : runsIn(app, findText(app, L"the link"))) {
                    int rules = 0;
                    for (const auto& line : app.layoutLines) {
                        if (horizontal(line) && line.p1.y > runBaseline(*run) &&
                            line.p1.y < run->bounds.bottom + 1.0f &&
                            line.p1.x < run->bounds.right - 1.0f && line.p2.x > run->bounds.left + 1.0f) {
                            rules++;
                        }
                    }
                    check(rules == 1, "a link inside <ins> has a single underline");
                }

                // Math inside <ins>: one level rule, not a step at each formula
                Range mathIns = findText(app, L"a $x$ b $y$ c");
                checkUnderlined(app, mathIns, "<ins> around math");
                auto mathRuns = runsIn(app, mathIns);
                if (!mathRuns.empty()) {
                    const auto* first = mathRuns.front();
                    float lo = 1e9f, hi = -1e9f;
                    for (const auto& line : app.layoutLines) {
                        if (!horizontal(line) || line.p1.y <= runBaseline(*first) ||
                            line.p1.y > first->bounds.bottom + 1.0f ||
                            line.p2.x < first->bounds.left || line.p1.x > mathRuns.back()->bounds.right) {
                            continue;
                        }
                        lo = std::min(lo, line.p1.y);
                        hi = std::max(hi, line.p1.y);
                    }
                    check(hi >= lo && hi - lo < 0.5f, "the rule under math stays level with the text's");
                }

                // <sup><ins> underlines the raised 2; <ins>x<sup> keeps the line's rule
                Range pair = findText(app, L"and x2 beside x2.");
                auto bodyX = runsIn(app, {pair.start + 4, pair.start + 5});
                auto raised = runsIn(app, {pair.start + 5, pair.start + 6});
                auto spanned = runsIn(app, {pair.start + 15, pair.start + 16});
                check(bodyX.size() == 1 && raised.size() == 1 && spanned.size() == 1,
                      "both superscript pairs are drawn");
                if (bodyX.size() == 1 && raised.size() == 1 && spanned.size() == 1) {
                    auto ruleUnder = [&](const App::LayoutTextRun* run) {
                        float best = -1.0f;
                        for (const auto& line : app.layoutLines) {
                            if (horizontal(line) && line.p1.y > runBaseline(*run) &&
                                line.p1.y < run->bounds.bottom + 1.0f &&
                                line.p1.x < run->bounds.right - 1.0f && line.p2.x > run->bounds.left + 1.0f) {
                                best = line.p1.y;
                            }
                        }
                        return best;
                    };
                    // Measured from each 2's own baseline: a script may wrap away from its x
                    const float raisedSize = raised[0]->layout->GetFontSize();
                    const float underRaised = ruleUnder(raised[0]) - runBaseline(*raised[0]);
                    const float underSpanned = ruleUnder(spanned[0]) - runBaseline(*spanned[0]);
                    check(underRaised > 0 && underRaised < raisedSize * 0.3f,
                          "<sup><ins>2</ins></sup> is underlined under the raised 2");
                    check(underSpanned > raisedSize * 0.3f, "<ins>x<sup>2</sup></ins> keeps the line's rule");
                }

                // Ruby inside <ins> in an HTML block is underlined under its base
                checkUnderlined(app, findText(app, L"\u6F22\u5B57"), "ruby inside <ins>");
                // HTML blocks keep the spaces beside their inline tags
                check(doc.find(L"HTML block: H2O and inserted and up.") != std::wstring::npos &&
                      doc.find(L"then bold and italic words.") != std::wstring::npos,
                      "HTML-block spans keep their spaces");
                // A lone wrapper line takes no room
                check(doc.find(L"\n\n\n\nA paragraph after a lone wrapper") == std::wstring::npos &&
                      doc.find(L"A paragraph after a lone wrapper") != std::wstring::npos,
                      "a wrapper alone on its line draws nothing");
                checkNoUnderline(app, findText(app, L"Water is"), "plain text");
                checkNoUnderline(app, findText(app, L"not underlined inside a code block"), "fenced <ins>");

                Range wrapped = findText(app, L"inserted words that keep going");
                Range wrappedAll = {wrapped.start,
                                    findText(app, L"each wrapped line", wrapped.start).end};
                auto wrappedRuns = runsIn(app, wrappedAll);
                float firstTop = wrappedRuns.empty() ? 0 : wrappedRuns.front()->bounds.top;
                bool wraps = std::any_of(wrappedRuns.begin(), wrappedRuns.end(),
                    [&](const App::LayoutTextRun* run) { return run->bounds.top > firstTop + 1.0f; });
                check(wraps, "the quoted <ins> wraps onto another line");
                checkUnderlined(app, wrappedAll, "every wrapped line of the quote");
            }
        }
    }
    check(checkedLayouts == 8, "every theme, scale and width is laid out");
}

void exportTests(App& app, const ElementPtr& root) {
    namespace fs = std::filesystem;
    fs::path output = fs::current_path() / "html-span-test-output";
    fs::create_directories(output);
    app.root = root;
    layoutDocument(app);
    check(exportHtmlFile(app, (output / "spans.html").wstring()), "HTML export succeeds");
    std::string html = read(output / "spans.html");
    check(html.find("H<sub>2</sub>O") != std::string::npos, "HTML keeps <sub>");
    check(html.find("r<sup>2</sup>") != std::string::npos, "HTML keeps <sup>");
    check(html.find("<ins>inserted text</ins>") != std::string::npos, "HTML writes <ins>");
    check(html.find("<ins>a plain u tag</ins>") != std::string::npos, "HTML writes <u> as <ins>");
    check(html.find("<del>an s tag</del>") != std::string::npos, "HTML writes <s> as <del>");
    check(html.find("<mark>a mark tag</mark>") != std::string::npos, "HTML writes <mark>");
    check(html.find("&lt;ins&gt;not underlined") != std::string::npos, "HTML keeps the fence literal");
    check(html.find("H<sub>2</sub>O and <ins>inserted</ins> and <sup>up</sup>.") != std::string::npos,
          "HTML-block spans export with their spaces");
    check(html.find("alt=\"alt with H2O\"") != std::string::npos, "HTML keeps the words of an alt <sub>");

    check(exportDocxFile(app, (output / "spans.docx").wstring()), "Word export succeeds");
    std::string zip = read(output / "spans.docx");
    check(zip.find("<w:u w:val=\"single\"/></w:rPr><w:t xml:space=\"preserve\">inserted text</w:t>") !=
              std::string::npos, "Word underlines <ins>");
    check(zip.find("<w:u w:val=\"single\"/></w:rPr><w:t xml:space=\"preserve\">a plain u tag</w:t>") !=
              std::string::npos, "Word underlines <u>");
    check(zip.find("<w:strike/></w:rPr><w:t xml:space=\"preserve\">removed text</w:t>") !=
              std::string::npos, "Word strikes <del>");
    check(zip.find("<w:vertAlign w:val=\"subscript\"/></w:rPr><w:t xml:space=\"preserve\">2</w:t>") !=
              std::string::npos, "Word lowers <sub>");
    check(zip.find("<w:vertAlign w:val=\"superscript\"/></w:rPr><w:t xml:space=\"preserve\">2</w:t>") !=
              std::string::npos, "Word raises <sup>");
    check(zip.find("&lt;ins&gt;") != std::string::npos, "Word keeps the fence literal");
    check(zip.find("alt with H2O (missing-263.png)") != std::string::npos, "Word keeps the words of an alt <sub>");
}

// ~~ and == around bold, italic, code and links export as they draw: HTML
// nests the spans, Word gives each run both properties, and the words the
// fixture keeps literal stay plain
void markedEmphasisExportTests(App& app) {
    namespace fs = std::filesystem;
    auto doc = parseDocument(app.parser, read(TINTA_MARKED_EMPHASIS_FIXTURE), "struck-and-marked-emphasis.md");
    check(doc.success, "the struck and marked emphasis fixture parses");
    if (!doc.success) return;
    fs::path output = fs::current_path() / "html-span-test-output";
    fs::create_directories(output);
    applyTheme(app, 0);
    app.contentScale = 1.0f;
    updateTextFormats(app);
    app.width = 1050;
    app.root = doc.root;
    layoutDocument(app);
    check(exportHtmlFile(app, (output / "marked.html").wstring()), "the fixture's HTML export succeeds");
    std::string html = read(output / "marked.html");
    for (const char* nested : {
             "<del><strong>Struck_bold</strong></del> text after it",
             "<mark><em>Marked_italic</em></mark>",
             "<del><code>struck_code()</code></del>",
             "<mark><a href=\"#around-emphasis\">Marked_link</a></mark>",
             "<del><mark><strong>Struck_marked_bold</strong></mark></del>",
             "<mark><del><em>Marked_struck_italic</em></del></mark>",
             "<del>Struck_claim<sup id=\"tinta-fnref-1-1\">"}) {
        check(html.find(nested) != std::string::npos, std::string("HTML nests ") + nested);
    }
    for (const char* literal : {
             "~~ <strong>Spaced_struck</strong> ~~",
             "<strong>Compared_left</strong> == <strong>Compared_right</strong>",
             "~~a <strong>Crossing_struck~~ c</strong>",
             "~~<strong>Escaped_struck</strong>~~"}) {
        check(html.find(literal) != std::string::npos, std::string("HTML keeps ") + literal);
    }

    check(exportDocxFile(app, (output / "marked.docx").wstring()), "the fixture's Word export succeeds");
    std::string zip = read(output / "marked.docx");
    check(zip.find("<w:b/><w:strike/></w:rPr><w:t xml:space=\"preserve\">Struck_bold</w:t>") != std::string::npos,
          "Word strikes the bold run");
    check(zip.find("<w:i/><w:highlight w:val=\"yellow\"/></w:rPr><w:t xml:space=\"preserve\">Marked_italic</w:t>") !=
              std::string::npos, "Word marks the italic run");
    check(zip.find("<w:b/><w:strike/><w:highlight w:val=\"yellow\"/></w:rPr><w:t xml:space=\"preserve\">"
                   "Struck_marked_bold</w:t>") != std::string::npos, "Word strikes and marks the bold run");
    check(zip.find("<w:b/></w:rPr><w:t xml:space=\"preserve\">Escaped_struck</w:t>") != std::string::npos,
          "Word keeps an escaped pair's bold plain");
}
}  // namespace

int main() {
    auto state = std::make_unique<App>();
    auto& app = *state;
    if (!initD2D(app)) return 2;
    parserTests(app);
    auto source = read(TINTA_HTML_SPAN_FIXTURE);
    auto doc = parseDocument(app.parser, source, "html-inline-tags-263.md");
    check(doc.success, "the #263 fixture parses");
    if (doc.success) {
        layoutTests(app, doc.root);
        exportTests(app, doc.root);
    }
    strikeTests(app);
    markedEmphasisExportTests(app);
    std::cout << "HTML spans: " << failures << " failures\n";
    return failures ? 1 : 0;
}
