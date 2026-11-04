// CSS themes: the engine on its own (css.h, docs/specs/theming.md). Tokens,
// error recovery, selectors and specificity, the cascade, values, colours,
// at-rules, diagnostics, interning, and the fixture themes.

#include "css.h"
#include "../src/css_internal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>

using namespace css;

namespace {

int failures = 0, checks = 0;
void check(bool value, const std::string& message) {
    ++checks;
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

const std::string fixtureDir = TINTA_CSS_FIXTURE_DIR;

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

bool near(double a, double b, double eps = 0.002) { return std::fabs(a - b) <= eps; }

std::string show(const Color& c) {
    char buffer[96];
    std::snprintf(buffer, sizeof buffer, "%s (%.4f %.4f %.4f / %.3f)", formatColor(c).c_str(), c.r, c.g, c.b, c.a);
    return buffer;
}

bool sameColor(const Color& c, float r, float g, float b, float a = 1, float eps = 1.5f / 255) {
    return near(c.r, r, eps) && near(c.g, g, eps) && near(c.b, b, eps) && near(c.a, a, eps);
}

bool sameColor(const Color& c, const char* text, float eps = 1.5f / 255) {
    std::optional<Color> want = parseColor(text);
    return want && sameColor(c, want->r, want->g, want->b, want->a, eps);
}

void checkColor(const char* text, const char* want, const std::string& what, bool dark = false, float eps = 1.5f / 255) {
    std::optional<Color> c = parseColor(text, dark);
    check(c && sameColor(*c, want, eps), what + ": " + text + " gave " + (c ? show(*c) : std::string("nothing")) + ", want " + want);
}

std::shared_ptr<const Sheet> sheetOf(const std::string& text, std::vector<Diagnostic>* out = nullptr,
                                     Origin origin = Origin::Theme, const ImportSource& imports = nullptr,
                                     const std::string& file = "test.css") {
    std::vector<Diagnostic> diagnostics;
    auto sheet = parse(text, file, origin, imports, diagnostics);
    if (out) *out = diagnostics;
    return sheet;
}

bool hasDiagnostic(const std::vector<Diagnostic>& list, const std::string& part, int line = 0, int column = 0,
                   int level = -1) {
    for (const Diagnostic& d : list) {
        if (d.message.find(part) != std::string::npos && (!line || d.line == line) && (!column || d.column == column) &&
            (level < 0 || d.level == level)) {
            return true;
        }
    }
    return false;
}

std::string dump(const std::vector<Diagnostic>& list) {
    std::string out;
    for (const Diagnostic& d : list) {
        out += "  " + d.file + ":" + std::to_string(d.line) + ":" + std::to_string(d.column) + " [" +
               "EWN"[d.level] + "] " + d.message + "\n";
    }
    return out;
}

// A page to style: elements as the HTML export writes them
struct El {
    std::string tag, id;
    std::vector<std::string> classes;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<El> kids;
    bool checked = false, text = true;
    Engine::Context ctx = 0;
};

El el(const std::string& spec, std::vector<El> kids = {}) {
    El e;
    std::string current;
    char kind = 't';
    auto flush = [&] {
        if (kind == 't') e.tag = current;
        else if (kind == '#') e.id = current;
        else e.classes.push_back(current);
        current.clear();
    };
    for (char c : spec) {
        if (c == '#' || c == '.') {
            flush();
            kind = c;
        } else {
            current += c;
        }
    }
    flush();
    e.kids = std::move(kids);
    return e;
}

El with(El e, const std::string& name, const std::string& value) {
    e.attrs.emplace_back(name, value);
    return e;
}
El checkedEl(El e) {
    e.checked = true;
    return e;
}
El emptyEl(El e) {
    e.text = false;
    return e;
}

void attach(Engine& engine, El& node) {
    size_t n = node.kids.size();
    std::vector<Element> elements(n);
    for (size_t i = 0; i < n; ++i) {
        El& k = node.kids[i];
        Element& e = elements[i];
        e.tag = k.tag;
        e.id = k.id;
        for (const std::string& c : k.classes) e.classes.push_back(c);
        for (const auto& a : k.attrs) e.attributes.emplace_back(a.first, a.second);
        e.index = (int)i + 1;
        e.count = (int)n;
        for (size_t j = 0; j < n; ++j) {
            if (node.kids[j].tag != k.tag) continue;
            ++e.typeCount;
            if (j <= i) ++e.typeIndex;
        }
        e.checked = k.checked;
        e.empty = k.kids.empty() && !k.text;
    }
    for (size_t i = 0; i < n; ++i) {
        node.kids[i].ctx = engine.child(node.ctx, elements[i], elements.data(), i);
        attach(engine, node.kids[i]);
    }
}

El& at(El& root, std::initializer_list<size_t> path) {
    El* e = &root;
    for (size_t i : path) e = &e->kids[i];
    return *e;
}

void layout(Engine& engine, El& page) {
    page.ctx = engine.root();
    attach(engine, page);
}

// html > body > the elements the selector tests look at
El samplePage() {
    return el("html", {el("body", {
        with(el("h1#top.big"), "lang", "en-GB"),                     // 0
        with(el("p.intro.lead"), "data-x", "Hello World"),            // 1
        el("p"),                                                      // 2
        el("ul", {el("li.a"), checkedEl(el("li.b")), el("li.c")}),    // 3
        with(el("div.math-display", {el("span.math-tag")}), "lang", "fr"),  // 4
        emptyEl(el("p")),                                             // 5
        el("blockquote.alert-note", {el("p.alert-title"), el("p", {el("strong", {el("em")})})}),  // 6
    })});
}

// Whether a selector picks the element at `path` of the sample page
bool picks(const std::string& selector, std::initializer_list<size_t> path) {
    std::vector<Diagnostic> diagnostics;
    auto sheet = sheetOf(selector + " { background-color: #00ff00 }", &diagnostics);
    Engine engine;
    engine.load({sheet}, Media());
    El page = samplePage();
    layout(engine, page);
    return engine.style(at(page, path).ctx).backgroundColor == Color{0, 1, 0, 1};
}

uint32_t specificity(const std::string& selector) {
    auto sheet = sheetOf(selector + " { color: red }");
    if (sheet->rules.empty() || sheet->rules[0].selectors.empty()) return 0xFFFFFFFF;
    return sheet->rules[0].selectors[0].specificity;
}
uint32_t S(uint32_t a, uint32_t b, uint32_t c) { return a << 20 | b << 10 | c; }

// One element's style under a sheet: html > body > p.x > span
struct One {
    Engine engine;
    El page;
    std::vector<Diagnostic> diagnostics;
    explicit One(const std::string& css, Media media = Media()) {
        auto sheet = parse(css, "one.css", Origin::Theme, nullptr, diagnostics);
        engine.load({sheet}, media);
        page = el("html", {el("body", {el("p.x#y", {el("span")})})});
        layout(engine, page);
    }
    const ComputedStyle& html() { return engine.style(page.ctx); }
    const ComputedStyle& body() { return engine.style(at(page, {0}).ctx); }
    const ComputedStyle& p() { return engine.style(at(page, {0, 0}).ctx); }
    const ComputedStyle& span() { return engine.style(at(page, {0, 0, 0}).ctx); }
};

// --- Tokens --------------------------------------------------------------------------

void tokenizerTests() {
    auto one = [](const std::string& text) {
        std::vector<Token> t = tokenize(text);
        return t.size() == 1 ? t[0] : Token();
    };
    Token t = one("\\31 23");
    check(t.type == Tok::Ident && t.text == "123", "a hex escape and the space after it make an identifier");
    t = one("\\E9 t\\E9");
    check(t.type == Tok::Ident && t.text == "\xC3\xA9t\xC3\xA9", "hex escapes become UTF-8");
    t = one("caf\xC3\xA9");
    check(t.type == Tok::Ident && t.text == "caf\xC3\xA9", "UTF-8 letters are identifier letters");
    t = one("\"x\\\ny\"");
    check(t.type == Tok::String && t.text == "xy", "an escaped newline in a string vanishes");
    t = one("'a\\'b'");
    check(t.type == Tok::String && t.text == "a'b", "an escaped quote stays in the string");
    std::vector<Token> bad = tokenize("'bad\nnext'");
    check(!bad.empty() && bad[0].type == Tok::BadString, "a raw newline ends a string as a bad string");
    t = one("+.5e1");
    check(t.type == Tok::Number && near(t.number, 5) && !t.integer, "+.5e1 is the number 5, not an integer");
    t = one("-.5");
    check(t.type == Tok::Number && near(t.number, -0.5), "-.5 is a number");
    t = one("10e-1");
    check(t.type == Tok::Number && near(t.number, 1), "10e-1 is 1");
    t = one("7");
    check(t.type == Tok::Number && t.integer, "7 is an integer");
    t = one("1e3px");
    check(t.type == Tok::Dimension && near(t.number, 1000) && t.text == "px", "1e3px is a dimension of 1000px");
    t = one("1em");
    check(t.type == Tok::Dimension && near(t.number, 1) && t.text == "em", "1em is not an exponent");
    t = one("2n-1");
    check(t.type == Tok::Dimension && t.text == "n-1", "2n-1 is a dimension with the unit n-1");
    t = one("50%");
    check(t.type == Tok::Percentage && near(t.number, 50), "50% is a percentage");
    t = one("--custom-x");
    check(t.type == Tok::Ident && t.text == "--custom-x", "--custom-x is one identifier");
    t = one("-moz-x");
    check(t.type == Tok::Ident && t.text == "-moz-x", "a vendor prefix is part of the identifier");
    t = one("#fff");
    check(t.type == Tok::Hash && t.idHash && t.text == "fff", "#fff is an id hash");
    t = one("#1a");
    check(t.type == Tok::Hash && !t.idHash && t.text == "1a", "#1a is a hash but no id");
    t = one("url( x.png )");
    check(t.type == Tok::Url && t.text == "x.png", "url() without quotes is a url token, spaces trimmed");
    t = one("URL(a b)");
    check(t.type == Tok::BadUrl, "a space inside an unquoted url() makes a bad url");
    std::vector<Token> quoted = tokenize("url(\"q.png\")");
    check(quoted.size() == 3 && quoted[0].type == Tok::Function && quoted[1].type == Tok::String && quoted[1].text == "q.png",
          "url() with a quoted string is a function");
    std::vector<Token> comment = tokenize("a/* c */b/* open");
    check(comment.size() == 2 && comment[0].text == "a" && comment[1].text == "b", "comments vanish, an open one runs to the end");
    std::vector<Token> cdo = tokenize("<!-- -->");
    check(cdo.size() == 3 && cdo[0].type == Tok::CDO && cdo[2].type == Tok::CDC, "<!-- and --> are their own tokens");
    t = one("@media");
    check(t.type == Tok::AtKeyword && t.text == "media", "@media is an at-keyword");
    std::vector<Token> slash = tokenize("\\\n");
    check(!slash.empty() && slash[0].type == Tok::Delim && slash[0].delim == '\\', "a backslash before a newline is a delim");
    std::vector<Token> ops = tokenize("a>b~c+d");
    check(ops.size() == 7 && ops[1].delim == '>' && ops[3].delim == '~' && ops[5].delim == '+' && ops[6].type == Tok::Ident,
          "combinators are delims; +d is not a number");
    std::vector<Node> nodes = parseNodes("f(a, [b {c}]) (d)");
    check(nodes.size() == 3 && nodes[0].function("f") && nodes[0].children.size() == 4 &&
          nodes[0].children[3].is(Tok::OpenSquare) && nodes[2].is(Tok::OpenParen),
          "functions and blocks nest as component values");
    std::string text;
    serializeNodes(parseNodes("calc( 1px  +  2em )"), text);
    check(text == "calc( 1px + 2em )", "component values serialize back: " + text);
}

// --- Error recovery ---------------------------------------------------------------------

void recoveryTests() {
    {
        One one("p { color: #ff0000; @@ ; font-size: 20px }");
        check(one.p().fontSize == 20 && sameColor(one.p().color, "#ff0000"), "a stray token drops itself, not the block");
    }
    {
        One one("p { color: ; font-size: 21px }");
        check(one.p().fontSize == 21 && hasDiagnostic(one.diagnostics, "`color` has no value"),
              "an empty value drops only its declaration");
    }
    {
        One one("p, ..bad { color: #ff0000 } span { color: #0000ff }");
        check(!sameColor(one.p().color, "#ff0000") && sameColor(one.span().color, "#0000ff"),
              "a bad selector drops its rule and nothing after it");
        check(hasDiagnostic(one.diagnostics, "the rule is skipped", 1, 4), "the bad selector is reported where it goes wrong\n" + dump(one.diagnostics));
    }
    {
        One one("p { color: #ff0000 !important; font-size: 22px } p { color: #0000ff }");
        check(sameColor(one.p().color, "#ff0000") && one.p().fontSize == 22, "!important beats a later rule");
    }
    {
        One one("p { color: #ff0000 ! IMPORTANT } p { color: #0000ff }");
        check(sameColor(one.p().color, "#ff0000"), "! important with a space and in capitals");
    }
    {
        One one("@foo bar { p { color: #ff0000 } } span { color: #0000ff }");
        check(!sameColor(one.p().color, "#ff0000") && sameColor(one.span().color, "#0000ff"),
              "an unknown at-rule's block is skipped whole");
        check(hasDiagnostic(one.diagnostics, "unknown at-rule `@foo`", 1, 1, Diagnostic::Error), "and reported");
    }
    {
        One one("p { color: rgb(1, 2, 3)); font-size: 23px } span { color: #0000ff }");
        check(one.p().fontSize == 23 && sameColor(one.span().color, "#0000ff"), "an extra ) is a stray token");
    }
    {
        One one("} p { color: #ff0000 } span { font-size: 24px }");
        check(one.span().fontSize == 24, "a stray } at the top only spoils the rule it starts");
    }
    {
        One one("p { font-size: 25px");
        check(one.p().fontSize == 25 && hasDiagnostic(one.diagnostics, "missing its `}`"), "an unclosed block at the end still applies");
    }
    {
        One one("p { font-size: 26px; span { color: #ff0000 } color: #00ff00 }");
        check(one.p().fontSize == 26 && sameColor(one.p().color, "#00ff00") && !sameColor(one.span().color, "#ff0000"),
              "a nested rule is skipped, the declarations around it kept");
        check(hasDiagnostic(one.diagnostics, "nested rules are not supported"), "and reported");
    }
    {
        One one("p { --empty:; --block: { a: b }; font-size: 27px }");
        check(one.p().fontSize == 27 && one.p().custom("--block") == "{ a: b }",
              "custom properties take empty values and blocks");
    }
    {
        One one("<!-- p { font-size: 28px } -->");
        check(one.p().fontSize == 28, "<!-- and --> at the top are ignored");
    }
}

// --- Selectors ----------------------------------------------------------------------------

void specificityTests() {
    check(specificity("*") == 0, "* has no specificity");
    check(specificity("li") == S(0, 0, 1), "li");
    check(specificity("ul li") == S(0, 0, 2), "ul li");
    check(specificity("ul ol+li") == S(0, 0, 3), "ul ol+li");
    check(specificity("h1 + *[rel=up]") == S(0, 1, 1), "h1 + *[rel=up]");
    check(specificity("ul ol li.red") == S(0, 1, 3), "ul ol li.red");
    check(specificity("li.red.level") == S(0, 2, 1), "li.red.level");
    check(specificity("#x34y") == S(1, 0, 0), "#x34y");
    check(specificity("#s12:not(FOO)") == S(1, 0, 1), "#s12:not(FOO)");
    check(specificity(".foo :is(.bar, #baz)") == S(1, 1, 0), ".foo :is(.bar, #baz) takes its most specific argument");
    check(specificity(":where(#a, .b) p") == S(0, 0, 1), ":where() adds nothing");
    check(specificity("li:nth-child(2n+1)") == S(0, 1, 1), "li:nth-child(2n+1)");
    check(specificity("p::before") == S(0, 0, 2), "a pseudo-element counts as a type");
    check(specificity("p:before") == S(0, 0, 2), "the legacy :before too");
    check(specificity(":root") == S(0, 1, 0), ":root");
    check(specificity("a[href]:not(.x)") == S(0, 2, 1), "a[href]:not(.x)");
    check(specificity("p:only-child") == S(0, 1, 1), ":only-child counts once");
    check(specificity("p:hover") == 0xFFFFFFFF, ":hover drops its rule");
    check(specificity("p:has(a)") == 0xFFFFFFFF, ":has() drops its rule");
    check(specificity("p::first-line") == 0xFFFFFFFF, "::first-line drops its rule");
    check(specificity("li:nth-child(2 of .x)") == 0xFFFFFFFF, "nth-child(... of S) is reported unsupported");
    check(specificity("p > ") == 0xFFFFFFFF, "a dangling combinator is invalid");
    check(specificity("ns|p") == 0xFFFFFFFF, "namespaces are unsupported");
}

void matchingTests() {
    check(picks("h1", {0, 0}) && picks("H1", {0, 0}), "type selectors, any case");
    check(picks("*", {0, 2}), "the universal selector");
    check(picks("#top", {0, 0}) && !picks("#TOP", {0, 0}), "ids are case-sensitive");
    check(picks(".intro", {0, 1}) && picks(".intro.lead", {0, 1}) && !picks(".intro.nope", {0, 1}), "classes");
    check(picks("p.intro", {0, 1}) && !picks("p.intro", {0, 2}), "a tag and a class");
    check(picks("[data-x]", {0, 1}) && !picks("[data-x]", {0, 2}), "[attr]");
    check(picks("[data-x=\"Hello World\"]", {0, 1}) && !picks("[data-x=\"hello world\"]", {0, 1}), "[attr=value]");
    check(picks("[data-x=\"hello world\" i]", {0, 1}), "[attr=value i]");
    check(picks("[data-x~=World]", {0, 1}) && !picks("[data-x~=Wor]", {0, 1}), "[attr~=word]");
    check(picks("[data-x^=Hell]", {0, 1}) && picks("[data-x$=rld]", {0, 1}) && picks("[data-x*=\"lo W\"]", {0, 1}),
          "[attr^=] [attr$=] [attr*=]");
    check(picks("[lang|=en]", {0, 0}) && picks("[lang|=en-GB]", {0, 0}) && !picks("[lang|=e]", {0, 0}), "[attr|=]");
    check(picks("[class~=lead]", {0, 1}) && picks("[id=top]", {0, 0}), "class and id as attributes");
    check(picks("body > p", {0, 2}) && !picks("body > strong", {0, 6, 1, 0}), "the child combinator");
    check(picks("blockquote strong", {0, 6, 1, 0}) && !picks("blockquote > strong", {0, 6, 1, 0}), "the descendant combinator");
    check(picks("h1 + p", {0, 1}) && !picks("h1 + p", {0, 2}), "the next-sibling combinator");
    check(picks("h1 ~ p", {0, 2}) && picks("h1 ~ ul", {0, 3}) && !picks("p ~ h1", {0, 0}), "the subsequent-sibling combinator");
    check(picks("li.a + li", {0, 3, 1}) && !picks("li.a + li", {0, 3, 2}) && picks("li.a ~ li", {0, 3, 2}) &&
          !picks("li ~ li.a", {0, 3, 0}), "sibling combinators in a list");
    check(picks("p + p + ul", {0, 3}) && !picks("h1 + p + ul", {0, 3}), "chained sibling combinators");
    check(picks(".alert-note > p + p strong", {0, 6, 1, 0}), "siblings inside a parent");
    check(picks(":root", {}) && !picks("body:root", {0}), ":root is the html element");
    check(picks("li:first-child", {0, 3, 0}) && !picks("li:first-child", {0, 3, 1}), ":first-child");
    check(picks("li:last-child", {0, 3, 2}) && !picks("li:only-child", {0, 3, 2}) && picks("em:only-child", {0, 6, 1, 0, 0}),
          ":last-child and :only-child");
    check(picks("li:nth-child(2)", {0, 3, 1}) && picks("li:nth-child(odd)", {0, 3, 2}) &&
          !picks("li:nth-child(odd)", {0, 3, 1}) && picks("li:nth-child(even)", {0, 3, 1}), ":nth-child(2), odd and even");
    check(picks("li:nth-child(-n+2)", {0, 3, 1}) && !picks("li:nth-child(-n+2)", {0, 3, 2}) &&
          picks("li:nth-child(3n+3)", {0, 3, 2}) && picks("li:nth-child( 2n + 1 )", {0, 3, 0}), ":nth-child(An+B)");
    check(picks("li:nth-last-child(1)", {0, 3, 2}) && !picks("li:nth-last-child(1)", {0, 3, 1}), ":nth-last-child()");
    check(picks("p:nth-of-type(2)", {0, 2}) && picks("p:first-of-type", {0, 1}) && picks("p:last-of-type", {0, 5}) &&
          picks("p:nth-last-of-type(1)", {0, 5}), "the -of-type pseudo-classes");
    check(picks("h1:only-of-type", {0, 0}) && !picks("p:only-of-type", {0, 1}), ":only-of-type");
    check(picks(":empty", {0, 5}) && !picks("p:empty", {0, 2}), ":empty");
    check(picks(":checked", {0, 3, 1}) && !picks(":checked", {0, 3, 0}), ":checked");
    check(picks(":lang(en)", {0, 0}) && picks(":lang(fr)", {0, 4}) && !picks(":lang(de)", {0, 4}), ":lang() with prefixes");
    check(picks("span:lang(fr)", {0, 4, 0}), ":lang() follows the inherited lang attribute");
    check(picks(":is(h1, h2)", {0, 0}) && picks(":where(.intro)", {0, 1}), ":is() and :where()");
    check(picks("p:not(.intro)", {0, 2}) && !picks("p:not(.intro)", {0, 1}) && picks(":not(p, h1)", {0, 3}), ":not()");
    check(picks("li:not(:first-child)", {0, 3, 1}), ":not() with a pseudo-class");
    check(picks(":is(blockquote p) > strong", {0, 6, 1, 0}) && !picks(":not(.alert-note > p) > strong", {0, 6, 1, 0}),
          "complex selectors inside :is() and :not()");
    check(picks(":is(h1 + p)", {0, 1}) && !picks(":is(h1 + p)", {0, 2}) && picks("p:not(h1 + p)", {0, 2}) &&
          !picks("p:not(h1 + p)", {0, 1}), "sibling combinators inside :is() and :not()");
    check(picks("#top ~ ul > .b", {0, 3, 1}) && picks(":is(p + p) + ul", {0, 3}) && !picks(":is(h1 + p) + ul", {0, 3}),
          "ids, and nested sibling selectors before a combinator");
    check(!picks("p:hover", {0, 2}) && !picks("p, a:hover", {0, 2}), "an unsupported pseudo-class drops the rule");
}

// --- The cascade and inheritance ------------------------------------------------------

Engine::Context paragraph(Engine& engine, El& page) {
    page = el("html", {el("body", {el("p.x#y", {el("span")})})});
    layout(engine, page);
    return at(page, {0, 0}).ctx;
}

void cascadeTests() {
    {
        One one("p { color: #ff0000 } .x { color: #0000ff }");
        check(sameColor(one.p().color, "#0000ff"), "a class beats a type");
    }
    {
        One one("p { color: #ff0000 } p { color: #0000ff }");
        check(sameColor(one.p().color, "#0000ff"), "the later of equal rules wins");
    }
    {
        One one("#y { color: #ff0000 } p.x { color: #0000ff }");
        check(sameColor(one.p().color, "#ff0000"), "an id beats classes");
    }
    {
        One one("#y { color: #ff0000 } p { color: #0000ff !important }");
        check(sameColor(one.p().color, "#0000ff"), "!important beats specificity");
    }
    auto origins = [](const std::string& base, const std::string& theme, const std::string& folder) {
        Engine engine;
        engine.load({sheetOf(base, nullptr, Origin::Base), sheetOf(theme, nullptr, Origin::Theme),
                     sheetOf(folder, nullptr, Origin::Folder)}, Media());
        El page;
        return engine.style(paragraph(engine, page)).color;
    };
    check(sameColor(origins("body p.x#y { color: #ff0000 }", "p { color: #0000ff }", ""), "#0000ff"),
          "the theme beats the base whatever the specificity");
    check(sameColor(origins("", "#y { color: #0000ff }", "p { color: #00ff00 }"), "#00ff00"), "the folder beats the theme");
    check(sameColor(origins("p { color: #ff0000 !important }", "p { color: #0000ff !important }",
                            "p { color: #00ff00 !important }"), "#ff0000"),
          "important declarations reverse the origins: the base's wins");
    check(sameColor(origins("", "p { color: #0000ff !important }", "#y { color: #00ff00 }"), "#0000ff"),
          "the theme's important beats the folder's normal");
    check(sameColor(origins("", "p { color: #0000ff !important }", "p { color: #00ff00 !important }"), "#0000ff"),
          "the theme's important beats the folder's important");
    {
        One one("p { border-top-color: #ff0000; border: 1px solid #0000ff }");
        check(sameColor(one.p().border[0].color, "#0000ff"), "a later shorthand resets its longhands");
    }
    {
        One one("p { border: 1px solid #0000ff; border-top-color: #ff0000 }");
        check(sameColor(one.p().border[0].color, "#ff0000") && sameColor(one.p().border[1].color, "#0000ff"),
              "a later longhand overrides its shorthand");
    }
    {
        One one("p { margin: 0 } p { color: inherit }");
        const ComputedStyle& p = one.p();
        check(p.has(Prop::MarginTop) && p.has(Prop::MarginLeft) && p.has(Prop::Color) && !p.has(Prop::FontSize) &&
              !one.span().has(Prop::MarginTop), "declared records what rules set on the element itself");
    }
}

void inheritanceTests() {
    {
        One one("body { color: #00ff00; background-color: #ff0000 }");
        check(sameColor(one.p().color, "#00ff00") && one.p().backgroundColor.a == 0,
              "color inherits, background-color does not");
    }
    {
        One one("body { background-color: #ff0000 } p { background-color: inherit; color: initial }"
                " body { color: #00ff00 } span { color: unset; margin-top: 5px } span { margin: unset }");
        check(sameColor(one.p().backgroundColor, "#ff0000"), "inherit takes the parent's value");
        check(sameColor(one.p().color, "#000000"), "initial is the property's first value");
        check(sameColor(one.span().color, "#000000") && one.span().margin[0] == 0,
              "unset inherits what inherits and resets what does not");
    }
    {
        One one("p { color: #00ff00 } span { color: revert }");
        check(sameColor(one.span().color, "#00ff00"), "revert acts as unset");
    }
    {
        One one("html { font-size: 10px } body { font-size: 20px } p { font-size: 1.5em } span { font-size: 2rem }");
        check(one.html().fontSize == 10 && one.p().fontSize == 30 && one.span().fontSize == 20, "em and rem font sizes");
    }
    {
        One one("body { font-size: 20px } p { font-size: 50% } span { font-size: larger }");
        check(one.p().fontSize == 10 && near(one.span().fontSize, 12), "percentages and larger");
    }
    {
        One one("p { font-size: xx-small } span { font-size: smaller } body { font-size: xxx-large }");
        check(near(one.p().fontSize, 9.6f) && near(one.span().fontSize, 8) && one.body().fontSize == 48,
              "absolute and relative size keywords");
    }
    {
        One one("body { font-size: 10px; line-height: 2 } p { font-size: 20px }");
        check(one.body().lineHeight == 20 && one.body().lineHeightFactor == 2 && one.p().lineHeight == 40 &&
              one.p().lineHeightFactor == 2, "a unitless line-height inherits as the factor");
    }
    {
        One one("body { font-size: 10px; line-height: 150% } p { font-size: 20px } span { line-height: normal }");
        check(one.body().lineHeight == 15 && one.p().lineHeight == 15 && one.p().lineHeightFactor == 0 &&
              one.span().lineHeight == 0, "a percentage line-height inherits as px; normal is 0");
    }
    {
        One one("body { font-weight: 400 } p { font-weight: bolder } span { font-weight: bolder }");
        check(one.p().fontWeight == 700 && one.span().fontWeight == 900, "bolder");
    }
    {
        One one("body { font-weight: bold } p { font-weight: lighter } span { font-weight: lighter }");
        check(one.p().fontWeight == 400 && one.span().fontWeight == 100, "lighter");
    }
    {
        One one("body { text-underline-offset: 3px; text-decoration: underline } p { letter-spacing: 0.1em; font-size: 20px }");
        check(one.p().decoration.offset == 3 && !one.p().decoration.underline && near(one.span().letterSpacing, 2),
              "text-underline-offset inherits, the line does not; em letter-spacing is px");
    }
}

// --- var() and calc() ---------------------------------------------------------------------

void varTests() {
    {
        One one(":root { --c: #00ff00 } p { color: var(--c) }");
        check(sameColor(one.p().color, "#00ff00") && one.span().custom("--c") == "#00ff00", "var() and inheritance");
        check(one.span().customColor("--c") && sameColor(*one.span().customColor("--c"), "#00ff00"), "customColor()");
    }
    {
        One one("p { color: var(--missing, #0000ff); background-color: var(--a, var(--b, #ff0000)) }");
        check(sameColor(one.p().color, "#0000ff") && sameColor(one.p().backgroundColor, "#ff0000"), "fallbacks, nested");
    }
    {
        // A custom property holding var() is written out again; a hash
        // starting with a digit must stay a colour, not become an escape
        One one(":root { --a: #00ff00; --b: var(--a); --c: 1px solid var(--a) } p { color: var(--b); border-top: var(--c) }");
        check(sameColor(one.p().color, "#00ff00") && sameColor(one.p().border[0].color, "#00ff00") &&
              one.p().custom("--b") == "#00ff00", "#00ff00 through two var()s: " + one.p().custom("--b"));
    }
    {
        One one("body { color: #00ff00 } p { color: var(--missing); background-color: var(--missing) }");
        check(sameColor(one.p().color, "#00ff00") && one.p().backgroundColor.a == 0,
              "a var() with no value makes the declaration unset");
        check(hasDiagnostic(one.engine.diagnostics(), "is invalid once var() is substituted", 1),
              "and the engine reports it\n" + dump(one.engine.diagnostics()));
    }
    {
        One one(":root { --a: var(--b); --b: var(--a); --c: var(--a, #ff0000); --d: var(--d) }"
                " p { color: var(--a, #0000ff); background-color: var(--c, #00ff00); border-top-color: var(--d, #00ff00) }");
        check(sameColor(one.p().color, "#0000ff"), "a cycle leaves its custom properties without a value");
        check(sameColor(one.p().backgroundColor, "#ff0000"), "one that uses a cycle takes its own fallback");
        check(sameColor(one.p().border[0].color, "#00ff00"), "a property that refers to itself is a cycle");
        check(hasDiagnostic(one.engine.diagnostics(), "`--a` is part of a var() cycle"), "cycles are reported");
    }
    {
        One one("body { --x: 5px } p { --y: calc(var(--x) * 2); margin-top: var(--x); margin-bottom: var(--y) }");
        check(one.p().margin[0] == 5 && one.p().margin[2] == 10 && one.p().custom("--y") == "calc(5px * 2)",
              "custom properties hold var() substituted text");
    }
    {
        One one("p { --b: 2px dashed #ff0000; border: var(--b); --m: 7px; margin: var(--m) 0; --e:; padding: var(--e) 4px }");
        const ComputedStyle& p = one.p();
        check(p.border[3].width == 2 && p.border[1].style == LineStyle::Dashed && sameColor(p.border[2].color, "#ff0000"),
              "var() in a shorthand expands when computed");
        check(p.margin[0] == 7 && p.margin[1] == 0 && p.margin[2] == 7 && p.padding[0] == 4 && p.padding[3] == 4,
              "var() among a shorthand's values, and an empty one");
    }
    {
        One one("p { --w: thick; border-top: var(--w) solid }");
        check(one.p().border[0].width == 5 && one.p().border[0].style == LineStyle::Solid, "var() with keywords");
    }
    {
        One one("body { --x: 1px } p { --x: initial; margin-top: var(--x, 9px) } span { --x: inherit; margin-top: var(--x) }");
        check(one.p().margin[0] == 9 && one.span().custom("--x").empty(), "--x: initial removes it, inherit copies it");
    }
    {
        One one("p { --font: 700 20px/2 serif; font: var(--font) }");
        check(one.p().fontWeight == 700 && one.p().fontSize == 20 && one.p().lineHeight == 40, "the font shorthand from var()");
    }
}

void calcTests() {
    {
        One one("p { font-size: calc(1rem + 2px); margin-top: calc(50% - 10px); line-height: calc(1em + 4px) }");
        check(one.p().fontSize == 18 && one.p().margin[0] == 390 && one.p().lineHeight == 22,
              "calc() with rem, % of the reading width and em");
    }
    {
        One one("p { font-size: 20px; padding-left: calc(2em / 2); margin-left: calc(calc(2px * 3) + (4px - 1px)) }");
        check(one.p().padding[3] == 20 && one.p().margin[3] == 9, "division and nested calc()");
    }
    {
        One one("p { font-size: min(20px, 2em); margin-top: max(10px, 1em); margin-bottom: clamp(12px, 5em, 30px) }");
        check(one.p().fontSize == 20 && one.p().margin[0] == 20 && one.p().margin[2] == 30, "min(), max() and clamp()");
    }
    {
        One one("p { max-width: calc(100% - 2em); padding-top: calc(10px * pi / pi) }");
        check(one.p().maxWidth.kind == Length::Px && one.p().maxWidth.value == 768 && near(one.p().padding[0], 10),
              "calc() mixing % and em, and constants");
    }
    {
        One one("p { margin-top: calc(1px + 2); margin-bottom: calc(2px * 3px) }");
        check(hasDiagnostic(one.diagnostics, "`calc(1px + 2)` is not a valid value for `margin-top`") &&
              hasDiagnostic(one.diagnostics, "`calc(2px * 3px)`"), "calc() with mismatched types is invalid");
    }
    {
        One one("p { margin-top: 10%; max-width: 50% }", Media{false, false, 1000});
        check(one.p().margin[0] == 100 && one.p().maxWidth.kind == Length::Percent && one.p().maxWidth.resolve(600) == 300,
              "percentages of the width, and max-width kept as a percentage");
    }
    {
        One one("p { margin-top: 1in; margin-right: 72pt; margin-bottom: 2.54cm; margin-left: 6pc; padding-top: 10mm;"
                " padding-right: 40q; padding-bottom: 2ex; padding-left: 2ch }");
        const ComputedStyle& p = one.p();
        check(p.margin[0] == 96 && p.margin[1] == 96 && near(p.margin[2], 96, 0.01) && p.margin[3] == 96 &&
              near(p.padding[0], 37.795, 0.01) && near(p.padding[1], 37.795, 0.01) && p.padding[2] == 16 && p.padding[3] == 16,
              "absolute units, and ex and ch as half an em");
    }
}

// --- Colours ---------------------------------------------------------------------------------

void colorTests() {
    checkColor("#f00", "#ff0000", "three hex digits");
    checkColor("#ff000080", "#ff000080", "eight hex digits");
    checkColor("#0f08", "#00ff0088", "four hex digits");
    checkColor("RED", "#ff0000", "named colours in any case");
    checkColor("rebeccapurple", "#663399", "rebeccapurple");
    checkColor("transparent", "#00000000", "transparent");
    checkColor("rgb(255, 0, 0)", "#ff0000", "legacy rgb()");
    checkColor("rgba(255, 0, 0, .25)", "#ff000040", "legacy rgba()");
    checkColor("rgb(100%, 0%, 0%)", "#ff0000", "rgb() in percentages");
    checkColor("rgb(255 0 0 / 50%)", "#ff000080", "modern rgb() with an alpha");
    checkColor("rgba(0 0 255 / 0.5)", "#0000ff80", "rgba() is rgb()");
    checkColor("rgb(calc(255 / 2) 0 0)", "#800000", "calc() in a component", false, 1.0f / 255);
    checkColor("rgb(300 -5 0)", "#ff0000", "components are clamped");
    checkColor("hsl(120 100% 25%)", "#008000", "hsl()");
    checkColor("hsl(120, 100%, 25%)", "#008000", "legacy hsl()");
    checkColor("hsla(240 100% 50% / 0.5)", "#0000ff80", "hsla()");
    checkColor("hsl(0.5turn 100% 50%)", "#00ffff", "a hue in turns");
    checkColor("hsl(none 0% 50%)", "#808080", "none in a modern colour");
    checkColor("hwb(0 0% 0%)", "#ff0000", "hwb()");
    checkColor("hwb(120 60% 60%)", "#808080", "hwb() with whiteness and blackness past 100%");
    checkColor("lab(54.29% 80.82 69.88)", "#ff0000", "lab()");
    checkColor("lab(100 0 0)", "#ffffff", "lab() white");
    checkColor("lch(54.29% 106.84 40.85)", "#ff0000", "lch()");
    checkColor("oklab(62.8% 0.2249 0.1258)", "#ff0000", "oklab()");
    checkColor("oklch(62.8% 0.2577 29.23)", "#ff0000", "oklch() red");
    checkColor("oklch(45.2% 0.313 264.05)", "#0000ff", "oklch() blue");
    checkColor("oklch(100% 0 0)", "#ffffff", "oklch() white");
    checkColor("color-mix(in srgb, red 50%, blue)", "#800080", "color-mix() in srgb", false, 1.0f / 255);
    std::optional<Color> half = parseColor("color-mix(in srgb, red 50%, blue)");
    check(half && near(half->r, 0.5, 0.0001) && near(half->b, 0.5, 0.0001), "color-mix(in srgb, red 50%, blue) is rgb(127.5 0 127.5)");
    checkColor("color-mix(in srgb, red, blue 25%)", "#bf0040", "the missing percentage is the rest");
    std::optional<Color> scaled = parseColor("color-mix(in srgb, red 20%, blue 20%)");
    check(scaled && near(scaled->r, 0.5, 0.001) && near(scaled->a, 0.4, 0.001), "percentages under 100% scale the alpha");
    std::optional<Color> gray = parseColor("color-mix(in oklab, white, black)");
    check(gray && near(gray->r, 0.3885, 0.004) && near(gray->r, gray->b, 0.002), "color-mix() in oklab");
    checkColor("color-mix(in hsl, red, blue)", "#ff00ff", "color-mix() in hsl takes the shorter hue");
    checkColor("color-mix(in hsl longer hue, red, blue)", "#00ff00", "longer hue");
    checkColor("color-mix(in srgb-linear, black, white)", "#bcbcbc", "color-mix() in srgb-linear");
    std::optional<Color> lchMix = parseColor("color-mix(in lch, red, white)");
    std::optional<Color> lchWant = parseColor("lch(77.145 53.42 40.85)");
    check(lchMix && lchWant && sameColor(*lchMix, lchWant->r, lchWant->g, lchWant->b, 1, 2.5f / 255),
          "white's hue is powerless when mixing in lch: " + (lchMix ? show(*lchMix) : "") + " want " + (lchWant ? show(*lchWant) : ""));
    std::optional<Color> faded = parseColor("color-mix(in oklab, #3366cc 15%, transparent)");
    check(faded && sameColor(*faded, 0.2f, 0.4f, 0.8f, 0.15f, 2.0f / 255), "mixing with transparent fades: " + (faded ? show(*faded) : ""));
    check(!parseColor("color-mix(in srgb, red 0%, blue 0%)") && !parseColor("color-mix(in nowhere, red, blue)"),
          "color-mix() with nothing to mix or an unknown space");
    checkColor("light-dark(#ffffff, #000000)", "#ffffff", "light-dark() in light");
    checkColor("light-dark(#ffffff, #000000)", "#000000", "light-dark() in dark", true);
    std::optional<Color> wide = parseColor("oklch(90% 0.4 150)");
    check(wide && wide->r >= 0 && wide->g <= 1 && wide->b >= 0, "colours outside sRGB are clamped");
    check(parseColor("1f2328") && sameColor(*parseColor("1f2328"), "#1f2328"), "a themes.ini value without #");
    check(!parseColor("currentColor") && !parseColor("#ggg") && !parseColor("rgb(1 2)") && !parseColor("hsl(1, 2, 3)") &&
          !parseColor("rgb(from red r g b)"), "what is not a colour");
    check(formatColor(Color{1, 0.5f, 0, 1}) == "#ff8000" && formatColor(Color{0, 0, 0, 0.5f}) == "#00000080",
          "formatColor()");
    const char* names[] = {"aliceblue", "antiquewhite", "aqua", "aquamarine", "azure", "beige", "bisque", "black",
        "blanchedalmond", "blue", "blueviolet", "brown", "burlywood", "cadetblue", "chartreuse", "chocolate", "coral",
        "cornflowerblue", "cornsilk", "crimson", "cyan", "darkblue", "darkcyan", "darkgoldenrod", "darkgray", "darkgreen",
        "darkgrey", "darkkhaki", "darkmagenta", "darkolivegreen", "darkorange", "darkorchid", "darkred", "darksalmon",
        "darkseagreen", "darkslateblue", "darkslategray", "darkslategrey", "darkturquoise", "darkviolet", "deeppink",
        "deepskyblue", "dimgray", "dimgrey", "dodgerblue", "firebrick", "floralwhite", "forestgreen", "fuchsia",
        "gainsboro", "ghostwhite", "gold", "goldenrod", "gray", "green", "greenyellow", "grey", "honeydew", "hotpink",
        "indianred", "indigo", "ivory", "khaki", "lavender", "lavenderblush", "lawngreen", "lemonchiffon", "lightblue",
        "lightcoral", "lightcyan", "lightgoldenrodyellow", "lightgray", "lightgreen", "lightgrey", "lightpink",
        "lightsalmon", "lightseagreen", "lightskyblue", "lightslategray", "lightslategrey", "lightsteelblue",
        "lightyellow", "lime", "limegreen", "linen", "magenta", "maroon", "mediumaquamarine", "mediumblue",
        "mediumorchid", "mediumpurple", "mediumseagreen", "mediumslateblue", "mediumspringgreen", "mediumturquoise",
        "mediumvioletred", "midnightblue", "mintcream", "mistyrose", "moccasin", "navajowhite", "navy", "oldlace",
        "olive", "olivedrab", "orange", "orangered", "orchid", "palegoldenrod", "palegreen", "paleturquoise",
        "palevioletred", "papayawhip", "peachpuff", "peru", "pink", "plum", "powderblue", "purple", "rebeccapurple",
        "red", "rosybrown", "royalblue", "saddlebrown", "salmon", "sandybrown", "seagreen", "seashell", "sienna",
        "silver", "skyblue", "slateblue", "slategray", "slategrey", "snow", "springgreen", "steelblue", "tan", "teal",
        "thistle", "tomato", "turquoise", "violet", "wheat", "white", "whitesmoke", "yellow", "yellowgreen"};
    int found = 0;
    for (const char* name : names) found += parseColor(name) ? 1 : 0;
    check(sizeof(names) / sizeof(names[0]) == 148 && found == 148, "all 148 named colours: " + std::to_string(found));
    checkColor("navajowhite", "#ffdead", "a named colour's value");
    {
        One one("p { color: #00ff00; border: 1px solid; box-shadow: 1px 1px; background-color: currentColor;"
                " text-decoration-color: currentColor }");
        const ComputedStyle& p = one.p();
        check(sameColor(p.border[0].color, "#00ff00") && sameColor(p.boxShadow[0].color, "#00ff00") &&
              sameColor(p.backgroundColor, "#00ff00") && !p.decoration.color, "currentColor where colours are left out");
        check(sameColor(one.span().border[0].color, "#00ff00") && one.span().border[0].width == 0,
              "a border colour starts as the text colour, its width as 0");
    }
    {
        One one("body { color: #ff0000 } p { color: color-mix(in srgb, currentColor 50%, #0000ff) }");
        check(sameColor(one.p().color, "#800080", 1.0f / 255), "currentColor in color is the parent's");
    }
}

void lightDarkTests() {
    auto sheet = sheetOf(":root { color-scheme: light dark; --ink: light-dark(#111111, #eeeeee) }"
                         " body { color: var(--ink); background-color: light-dark(white, black) }"
                         " @media (prefers-color-scheme: dark) { p { font-size: 30px } }");
    Engine engine;
    engine.load({sheet}, Media());
    El page = el("html", {el("body", {el("p")})});
    layout(engine, page);
    uint64_t light = engine.generation();
    check(sameColor(engine.style(at(page, {0}).ctx).color, "#111111") &&
          sameColor(engine.style(at(page, {0}).ctx).backgroundColor, "#ffffff") &&
          engine.style(at(page, {0, 0}).ctx).fontSize == 16, "light-dark() and media in light");
    check(engine.style(page.ctx).colorScheme == ColorScheme::LightDark, "color-scheme: light dark");
    Engine::Context body = at(page, {0}).ctx;
    engine.setMedia(Media{true, false, 800});
    check(engine.generation() != light, "setMedia() changes the generation");
    check(sameColor(engine.style(body).color, "#eeeeee") && sameColor(engine.style(body).backgroundColor, "#000000") &&
          engine.style(at(page, {0, 0}).ctx).fontSize == 30, "light-dark() and media in dark, contexts kept");
    check(engine.style(body).customColor("--ink") && sameColor(*engine.style(body).customColor("--ink"), "#eeeeee"),
          "customColor() follows the media");
    uint64_t dark = engine.generation();
    engine.setMedia(Media{true, false, 800});
    check(engine.generation() == dark, "the same media again changes nothing");
}

// --- At-rules ----------------------------------------------------------------------------------

void mediaTests() {
    const Media screen{false, false, 800}, print{false, true, 800}, narrow{false, false, 500}, dark{true, false, 800};
    auto size = [](const std::string& query, const Media& media) {
        One one("@media " + query + " { p { font-size: 30px } }", media);
        return one.p().fontSize;
    };
    check(size("print", print) == 30 && size("print", screen) == 16, "@media print");
    check(size("(min-width: 40em)", screen) == 30 && size("(min-width: 40em)", narrow) == 16, "(min-width) in em");
    check(size("not print", screen) == 30 && size("not print", print) == 16, "not print");
    check(size("screen and (max-width: 600px)", narrow) == 30 && size("screen and (max-width: 600px)", screen) == 16 &&
          size("screen and (max-width: 600px)", Media{false, true, 500}) == 16, "screen and (max-width)");
    check(size("only screen and (width >= 700px)", screen) == 30 && size("only screen and (width >= 700px)", narrow) == 16,
          "only, and a range");
    check(size("(400px <= width <= 600px)", narrow) == 30 && size("(400px <= width <= 600px)", screen) == 16,
          "a range with two ends");
    check(size("print, (max-width: 600px)", print) == 30 && size("print, (max-width: 600px)", narrow) == 30 &&
          size("print, (max-width: 600px)", screen) == 16, "a list of queries");
    check(size("(prefers-color-scheme: light)", screen) == 30 && size("(prefers-color-scheme: light)", dark) == 16,
          "prefers-color-scheme");
    check(size("(min-width: 600px) and (max-width: 900px)", screen) == 30 && size("(min-width: 600px) and (max-width: 900px)", narrow) == 16,
          "and");
    const char* nested = "not ((min-width: 600px) or (prefers-color-scheme: dark))";
    check(size(nested, narrow) == 30 && size(nested, screen) == 16 && size(nested, Media{true, false, 500}) == 16,
          "not and or with parentheses");
    {
        One one("@media screen { @media (min-width: 600px) { p { font-size: 30px } } }", screen);
        One small("@media screen { @media (min-width: 600px) { p { font-size: 30px } } }", narrow);
        check(one.p().fontSize == 30 && small.p().fontSize == 16, "nested @media");
    }
    {
        One one("@media (hover: hover) { p { font-size: 30px } } @media tv { p { font-size: 31px } }", screen);
        check(one.p().fontSize == 16 && hasDiagnostic(one.diagnostics, "`(hover)` is never true in Tinta", 1, 1, Diagnostic::Warning),
              "media features Tinta does not have never match, with a warning");
    }
    {
        One one("@media (min-width: red) { p { font-size: 30px } }", screen);
        check(one.p().fontSize == 16 && hasDiagnostic(one.diagnostics, "is not a valid media query", 1, 1, Diagnostic::Error),
              "a broken query never matches and is an error");
    }
}

ImportSource memorySource(std::map<std::string, std::string>& files) {
    return [&files](std::string_view name, std::string_view from, std::string& text, std::string& file) {
        std::string key(name);
        if (!files.count(key)) {
            size_t slash = from.find_last_of('/');
            key = std::string(from.substr(0, slash == std::string_view::npos ? 0 : slash + 1)) + std::string(name);
            if (!files.count(key)) return false;
        }
        text = files[key];
        file = key;
        return true;
    };
}

void importTests() {
    std::map<std::string, std::string> files = {
        {"Paper", "p { font-size: 20px; color: #ff0000 } h1 { font-size: 40px }"},
        {"/t/base.css", "@import \"more.css\";\np { margin-top: 3px }"},
        {"/t/more.css", "p { margin-bottom: 4px }"},
        {"/t/print.css", "@charset \"utf-8\";\np { font-size: 11px }"},
        {"/t/a.css", "@import \"b.css\";\np { padding-top: 1px }"},
        {"/t/b.css", "@import \"a.css\";\np { padding-bottom: 2px }"},
        {"/t/grid.css", "p { padding-left: 9px }"},
    };
    ImportSource source = memorySource(files);
    std::string css = "@import \"Paper\";\n@import url(\"base.css\");\n@import url(print.css) print;\n"
                      "@import \"missing.css\";\n@import \"a.css\";\n@import \"grid.css\" supports(display: grid);\n"
                      "p { color: #0000ff }\n@import \"late.css\";\n";
    std::vector<Diagnostic> diagnostics;
    auto sheet = sheetOf(css, &diagnostics, Origin::Theme, source, "/t/theme.css");
    for (const Media& media : {Media{false, false, 800}, Media{false, true, 800}}) {
        Engine engine;
        engine.load({sheet}, media);
        El page;
        const ComputedStyle& p = engine.style(paragraph(engine, page));
        check(sameColor(p.color, "#0000ff") && p.fontSize == (media.print ? 11 : 20),
              "a built-in import, and one for print only");
        check(p.margin[0] == 3 && p.margin[2] == 4 && p.padding[0] == 1 && p.padding[2] == 2 && p.padding[3] == 0,
              "imports nest, and a supports() condition holds an import back");
    }
    check(hasDiagnostic(diagnostics, "cannot find `missing.css` to import", 4, 1, Diagnostic::Error), "a missing import");
    check(hasDiagnostic(diagnostics, "imports a sheet that imports this one", 1, 1, Diagnostic::Error), "an import cycle");
    bool cycleInB = false;
    for (const Diagnostic& d : diagnostics) cycleInB |= d.file == "/t/b.css" && d.message.find("cycle") == std::string::npos &&
                                                        d.message.find("imports this one") != std::string::npos;
    check(cycleInB, "the cycle is reported in the file that closes it\n" + dump(diagnostics));
    check(hasDiagnostic(diagnostics, "`@import` must come before the other rules", 8, 1), "an import after rules");
    std::string out = serialize(*sheet);
    check(out.find("@import") == std::string::npos && out.find("@charset") == std::string::npos,
          "serialize() inlines every import and drops @charset");
    check(out.find("@media print {\n\np { font-size: 11px }\n}") != std::string::npos &&
          out.find("@supports (display: grid) {\np { padding-left: 9px }\n}") != std::string::npos,
          "inlined imports keep their media and supports conditions:\n" + out);
    check(out.find("p { margin-bottom: 4px }") < out.find("p { margin-top: 3px }") &&
          out.find("p { margin-top: 3px }") < out.find("p { color: #0000ff }"), "in the order they apply");
}

void fontFaceTests() {
    std::vector<Diagnostic> diagnostics;
    auto sheet = sheetOf("@font-face { font-family: \"Literata\"; src: url(\"fonts/Literata.ttf\") format(\"truetype\"),"
                         " local(\"Literata Regular\"), url(https://example.com/x.woff2); font-weight: 900 200;"
                         " font-style: italic; font-display: swap; unicode-range: U+0-7F }\n"
                         "@font-face { font-family: Inter Display; src: url(C:/Fonts/inter.ttf), local(Inter); font-weight: bold }\n"
                         "@font-face { font-family: Broken }\n"
                         "@media print { @font-face { font-family: Printed; src: url(\"../p.otf\") } }",
                         &diagnostics, Origin::Theme, nullptr, "C:/themes/ink.css");
    const std::vector<FontFace>& faces = fontFaces(*sheet);
    check(faces.size() == 3, "three usable faces");
    if (faces.size() == 3) {
        check(faces[0].family == "Literata" && faces[0].sources.size() == 2 &&
              faces[0].sources[0] == "C:/themes/fonts/Literata.ttf" && faces[0].sources[1] == "local:Literata Regular",
              "sources resolved beside the sheet, and local() names");
        check(faces[0].weightMin == 200 && faces[0].weightMax == 900 && faces[0].style == FontStyle::Italic &&
              faces[0].file == "C:/themes/ink.css" && faces[0].line == 1, "a weight range, the style and where it was declared");
        check(faces[1].family == "Inter Display" && faces[1].sources[0] == "C:/Fonts/inter.ttf" &&
              faces[1].sources[1] == "local:Inter" && faces[1].weightMin == 700 && faces[1].weightMax == 700,
              "unquoted names, absolute paths and bold");
        check(faces[2].family == "Printed" && faces[2].sources[0] == "C:/themes/../p.otf", "a face inside @media");
    }
    check(hasDiagnostic(diagnostics, "is not fetched", 1, 0, Diagnostic::Warning) &&
          hasDiagnostic(diagnostics, "`unicode-range` is not supported") &&
          hasDiagnostic(diagnostics, "needs a `font-family` and a `src`", 3, 1), "font-face problems are reported\n" + dump(diagnostics));
}

void supportsTests() {
    auto applies = [](const std::string& condition) {
        One one("@supports " + condition + " { p { font-size: 30px } }");
        return one.p().fontSize == 30;
    };
    check(applies("(color: oklch(50% 0.1 200))"), "@supports a colour Tinta reads");
    check(!applies("(display: grid)") && applies("not (display: grid)"), "@supports a property Tinta does not draw");
    check(applies("(color: red) and (font-size: 1px)") && !applies("(color: red) and (display: grid)"), "and");
    check(applies("(display: grid) or (color: red)"), "or");
    check(applies("selector(:is(a, b))") && !applies("selector(a:hover)"), "selector()");
    check(applies("(--x: anything)") && !applies("(color: nonsense)"), "custom properties and invalid values");
    check(applies("(border: 1px solid red)") && !applies("(border: 1px solid nonsense)"), "shorthands");
    check(!applies("font-tech(color-COLRv1)") && applies("((color: red))"), "unknown functions, and nested parentheses");
}

// --- Properties ---------------------------------------------------------------------------------

bool hasFeature(const std::vector<FontFeature>& list, const char* tag, float value) {
    uint32_t t = (uint32_t)tag[0] | (uint32_t)tag[1] << 8 | (uint32_t)tag[2] << 16 | (uint32_t)tag[3] << 24;
    for (const FontFeature& f : list) {
        if (f.tag == t) return f.value == value;
    }
    return false;
}

void shorthandTests() {
    {
        One one("p { font: italic small-caps bold condensed 12px/1.5 \"Fira Sans\", Arial Black, serif }");
        const ComputedStyle& p = one.p();
        check(p.fontStyle == FontStyle::Italic && p.fontWeight == 700 && p.fontStretch == 75 && p.fontSize == 12 &&
              p.lineHeight == 18 && p.lineHeightFactor == 1.5f, "the font shorthand");
        check(p.fontFamily == std::vector<std::string>{"Fira Sans", "Arial Black", "serif"} && hasFeature(p.fontFeatures, "smcp", 1),
              "its family list and small-caps");
    }
    {
        One one("p { font-variant: small-caps; font-weight: 300; font: 20px serif }");
        check(one.p().fontFeatures.empty() && one.p().fontWeight == 400 && one.p().fontSize == 20,
              "font resets what it leaves out");
    }
    {
        One one("p { font: 1.2em/normal monospace } span { font: bold }");
        check(near(one.p().fontSize, 19.2f) && one.p().lineHeight == 0 && hasDiagnostic(one.diagnostics, "`bold` is not a valid value for `font`"),
              "font with em and normal; without a size it is invalid");
    }
    {
        One one("p { background: #ff0000 } span { background: linear-gradient(to right, red, blue 80%) }");
        const ComputedStyle& span = one.span();
        check(sameColor(one.p().backgroundColor, "#ff0000") && !one.p().backgroundImage, "background: a colour");
        check(span.backgroundImage && span.backgroundImage->angle == 90 && span.backgroundImage->stops.size() == 2 &&
              span.backgroundImage->stops[0].position == -1 && near(span.backgroundImage->stops[1].position, 0.8f) &&
              span.backgroundColor.a == 0, "background: a gradient, the colour reset");
    }
    {
        One one("p { background: url(x.png) no-repeat center / cover #00ff00 }");
        check(sameColor(one.p().backgroundColor, "#00ff00") && !one.p().backgroundImage &&
              hasDiagnostic(one.diagnostics, "positions, sizes and repeats are not drawn") &&
              hasDiagnostic(one.diagnostics, "`url()` backgrounds are not drawn"), "what background parts Tinta leaves out");
    }
    {
        One one("p { background: linear-gradient(45deg, red 0%, blue 50% 75%, green) } span { background-image: linear-gradient(to top left, red, blue) }");
        const auto& g = one.p().backgroundImage;
        check(g && g->angle == 45 && g->stops.size() == 4 && g->stops[0].position == 0 && near(g->stops[1].position, 0.5f) &&
              near(g->stops[2].position, 0.75f) && g->stops[3].position == -1 && sameColor(g->stops[3].color, "green"),
              "gradient angles and stops with two positions");
        check(one.span().backgroundImage && one.span().backgroundImage->angle == 315, "to top left");
    }
    {
        One one("p { border: 2px dashed #ff0000 } span { border-top: thick double #00ff00; border-left: solid }");
        const ComputedStyle& p = one.p();
        bool all = true;
        for (const Border& b : p.border) all = all && b.width == 2 && b.style == LineStyle::Dashed && sameColor(b.color, "#ff0000");
        check(all, "border sets every side");
        const ComputedStyle& span = one.span();
        check(span.border[0].width == 5 && span.border[0].style == LineStyle::Double && sameColor(span.border[0].color, "#00ff00") &&
              span.border[3].width == 3 && span.border[3].style == LineStyle::Solid && span.border[1].width == 0,
              "border-top, and medium when only a style is given");
    }
    {
        One one("p { border-width: 1px 2px 3px 4px; border-style: solid; border-color: #ff0000 #0000ff }"
                " span { border-width: 1px 2px 3px; border-style: groove }");
        const ComputedStyle& p = one.p();
        check(p.border[0].width == 1 && p.border[1].width == 2 && p.border[2].width == 3 && p.border[3].width == 4 &&
              sameColor(p.border[0].color, "#ff0000") && sameColor(p.border[1].color, "#0000ff") &&
              sameColor(p.border[2].color, "#ff0000") && sameColor(p.border[3].color, "#0000ff"), "four and two values");
        const ComputedStyle& span = one.span();
        check(span.border[0].width == 1 && span.border[1].width == 2 && span.border[2].width == 3 && span.border[3].width == 2 &&
              span.border[0].style == LineStyle::Solid && hasDiagnostic(one.diagnostics, "`groove` borders are drawn solid"),
              "three values, and groove drawn solid");
    }
    {
        One one("p { border-radius: 1px 2px 3px 4px } span { border-radius: 5px 6px } body { border-radius: 8px / 4px }");
        check(one.p().radius[0] == 1 && one.p().radius[3] == 4 && one.span().radius[0] == 5 && one.span().radius[1] == 6 &&
              one.span().radius[2] == 5 && one.body().radius[2] == 8 && hasDiagnostic(one.diagnostics, "elliptical corners"),
              "border-radius");
    }
    {
        One one("p { margin: 1px 2px 3px 4px; padding: 5px 6px 7px } span { margin: 0 auto; padding: -1px }");
        const ComputedStyle& p = one.p();
        check(p.margin[0] == 1 && p.margin[1] == 2 && p.margin[2] == 3 && p.margin[3] == 4 && p.padding[0] == 5 &&
              p.padding[1] == 6 && p.padding[2] == 7 && p.padding[3] == 6, "margin and padding");
        check(one.span().margin[1] == 0 && hasDiagnostic(one.diagnostics, "`-1px` is not a valid value for `padding`"),
              "auto margins are 0; negative padding is invalid");
    }
    {
        One one("p { text-decoration: underline dotted #ff0000 2px } span { text-decoration: line-through overline }");
        const Decoration& d = one.p().decoration;
        check(d.underline && !d.lineThrough && d.style == LineStyle::Dotted && d.color && sameColor(*d.color, "#ff0000") &&
              d.thickness == 2, "text-decoration");
        check(one.span().decoration.lineThrough && one.span().decoration.overline && !one.span().decoration.underline,
              "two lines");
    }
    {
        One one("p { font-variant: all-small-caps tabular-nums slashed-zero no-common-ligatures }");
        const auto& f = one.p().fontFeatures;
        check(hasFeature(f, "smcp", 1) && hasFeature(f, "c2sc", 1) && hasFeature(f, "tnum", 1) && hasFeature(f, "zero", 1) &&
              hasFeature(f, "liga", 0) && hasFeature(f, "clig", 0), "font-variant into OpenType features");
    }
    {
        One one("p { font-variant-numeric: oldstyle-nums diagonal-fractions; font-feature-settings: \"liga\" 0, \"ss01\", \"onum\" off;"
                " font-variation-settings: \"wght\" 650, \"opsz\" 12 } span { font-variant-numeric: lining-nums oldstyle-nums }");
        const auto& f = one.p().fontFeatures;
        check(hasFeature(f, "onum", 0) && hasFeature(f, "frac", 1) && hasFeature(f, "liga", 0) && hasFeature(f, "ss01", 1),
              "font-feature-settings overrides font-variant");
        const auto& v = one.p().fontVariations;
        check(v.size() == 2 && v[0].tag == ('w' | 'g' << 8 | 'h' << 16 | 't' << 24) && v[0].value == 650, "font-variation-settings");
        check(hasDiagnostic(one.diagnostics, "not a valid value for `font-variant-numeric`"), "conflicting numeric values are invalid");
    }
    {
        One one("body { font-variant-caps: small-caps } p { font-feature-settings: \"kern\" 1 } span { font-variant-ligatures: none }");
        check(hasFeature(one.p().fontFeatures, "smcp", 1) && hasFeature(one.p().fontFeatures, "kern", 1) &&
              hasFeature(one.span().fontFeatures, "calt", 0) && hasFeature(one.span().fontFeatures, "kern", 1),
              "font features inherit longhand by longhand");
    }
    {
        One one("p { list-style: square inside } span { list-style: none } body { list-style-type: \"- \" }");
        check(one.p().listStyle == ListStyle::Square && one.span().listStyle == ListStyle::None &&
              one.body().listStyle == ListStyle::String && one.body().listString == "- " &&
              hasDiagnostic(one.diagnostics, "`inside` list markers"), "list-style and a string marker");
    }
    {
        One one("p { list-style-type: upper-roman } span { list-style-type: georgian }");
        check(one.p().listStyle == ListStyle::UpperRoman && one.span().listStyle == ListStyle::Decimal &&
              hasDiagnostic(one.diagnostics, "`georgian` is drawn as decimal"), "counter styles Tinta has and has not");
    }
    {
        One one("p { counter-reset: a b 2; counter-increment: a } span { counter-reset: none }");
        const auto& reset = one.p().counterReset;
        check(reset.size() == 2 && reset[0] == std::make_pair(std::string("a"), 0) && reset[1] == std::make_pair(std::string("b"), 2) &&
              one.p().counterIncrement.size() == 1 && one.p().counterIncrement[0].second == 1 && one.span().counterReset.empty(),
              "counter-reset and counter-increment with their defaults");
    }
    {
        One one("p { opacity: 50%; max-width: 40em } span { opacity: 0.25; max-width: none; box-shadow: 0 1px 2px rgba(0,0,0,.2), inset 0 0 0 1px #ff0000 }");
        check(one.p().opacity == 0.5f && one.p().maxWidth.kind == Length::Px && one.p().maxWidth.value == 640 &&
              one.span().opacity == 0.25f && one.span().maxWidth.kind == Length::None, "opacity and max-width");
        const auto& s = one.span().boxShadow;
        check(s.size() == 2 && s[0].y == 1 && s[0].blur == 2 && near(s[0].color.a, 0.2f) && s[1].inset && s[1].spread == 1 &&
              sameColor(s[1].color, "#ff0000"), "box-shadow lists");
    }
    {
        One one(":root { accent-color: #ff0000; caret-color: auto; color-scheme: dark } p { text-align: justify;"
                " text-transform: uppercase; text-indent: 2em; font-stretch: 125%; font-style: oblique 10deg; caret-color: #00ff00 }");
        check(one.html().accentColor && sameColor(*one.html().accentColor, "#ff0000") && !one.html().caretColor &&
              one.html().colorScheme == ColorScheme::Dark && one.p().accentColor && one.p().colorScheme == ColorScheme::Dark,
              "accent-color, caret-color and color-scheme inherit");
        const ComputedStyle& p = one.p();
        check(p.textAlign == TextAlign::Justify && p.textTransform == TextTransform::Uppercase && p.textIndent == 32 &&
              p.fontStretch == 125 && p.fontStyle == FontStyle::Oblique && p.caretColor && one.span().textIndent == 32,
              "text and font longhands");
    }
}

void pseudoTests() {
    auto sheet = sheetOf("p::before { content: \"\\00A7  \" counter(section, upper-roman) \". \" }"
                         " p::after { content: counters(item, \".\", lower-alpha) attr(data-x); color: #ff0000 }"
                         " li::marker { content: none; color: #00ff00 } h1::before { content: normal } h1::after { color: red }"
                         " ::selection { background-color: #0000ff; color: white } blockquote::selection { color: #ff0000 }");
    Engine engine;
    engine.load({sheet}, Media());
    El page = el("html", {el("body", {el("blockquote", {el("p")}), el("ul", {el("li")}), el("h1")})});
    layout(engine, page);
    Engine::Context p = at(page, {0, 0, 0}).ctx, li = at(page, {0, 1, 0}).ctx, h1 = at(page, {0, 2}).ctx;
    const ComputedStyle* before = engine.pseudo(p, Pseudo::Before);
    check(before && before->content.size() == 3 && before->content[0].text == "\xC2\xA7 " &&
          before->content[1].kind == ContentPart::Counter && before->content[1].text == "section" &&
          before->content[1].style == ListStyle::UpperRoman && before->content[2].text == ". ", "::before content");
    const ComputedStyle* after = engine.pseudo(p, Pseudo::After);
    check(after && after->content.size() == 2 && after->content[0].kind == ContentPart::Counters &&
          after->content[0].separator == "." && after->content[0].style == ListStyle::LowerAlpha &&
          after->content[1].kind == ContentPart::Attr && after->content[1].text == "data-x" && sameColor(after->color, "#ff0000"),
          "::after with counters() and attr()");
    const ComputedStyle* marker = engine.pseudo(li, Pseudo::Marker);
    check(marker && marker->content.empty() && sameColor(marker->color, "#00ff00") && !engine.pseudo(p, Pseudo::Marker),
          "::marker styles; none is no content");
    check(!engine.pseudo(h1, Pseudo::Before) && !engine.pseudo(h1, Pseudo::After), "no ::before or ::after without content");
    const ComputedStyle* selection = engine.pseudo(p, Pseudo::Selection);
    const ComputedStyle* quote = engine.pseudo(at(page, {0, 0}).ctx, Pseudo::Selection);
    check(selection && sameColor(selection->backgroundColor, "#0000ff") && sameColor(selection->color, "#ffffff"), "::selection");
    check(quote && sameColor(quote->color, "#ff0000") && sameColor(quote->backgroundColor, "#0000ff"), "::selection by element");

    auto rootOnly = sheetOf(":root::selection { background-color: #0000ff }");
    engine.load({rootOnly}, Media());
    layout(engine, page);
    const ComputedStyle* inherited = engine.pseudo(at(page, {0, 0, 0}).ctx, Pseudo::Selection);
    check(inherited && sameColor(inherited->backgroundColor, "#0000ff"), "the root's ::selection reaches every element");
    engine.load({sheetOf("p { color: red }")}, Media());
    layout(engine, page);
    check(!engine.pseudo(at(page, {0, 0, 0}).ctx, Pseudo::Selection), "no ::selection rule, no selection style");
}

void serializeTests() {
    std::map<std::string, std::string> files = {{"Paper", "h1 { color: #0000ff }"}};
    ImportSource source = memorySource(files);
    std::string css = "/* theme */\n@charset \"utf-8\";\n@import \"Paper\" screen;\n"
                      "@font-face { font-family: X; src: url(x.ttf) }\nbody { display: flex; color: #ff0000; -webkit-font-smoothing: auto }\n"
                      "@media print { p { color: #000000 } }\n.unknown:hover { cursor: pointer }\n@keyframes k { to { opacity: 0 } }\n";
    auto sheet = sheetOf(css, nullptr, Origin::Theme, source);
    std::string out = serialize(*sheet);
    check(out.find("/* theme */") == 0 && out.find("@media screen {\nh1 { color: #0000ff }\n}") != std::string::npos,
          "serialize() keeps comments and wraps a screen import");
    check(out.find("display: flex") != std::string::npos && out.find("-webkit-font-smoothing: auto") != std::string::npos &&
          out.find("@font-face { font-family: X; src: url(x.ttf) }") != std::string::npos &&
          out.find(".unknown:hover { cursor: pointer }") != std::string::npos && out.find("@keyframes k") != std::string::npos,
          "what Tinta ignores is kept for a browser");
    auto again = sheetOf(out);
    for (bool print : {false, true}) {
        Engine a, b;
        a.load({sheet}, Media{false, print, 800});
        b.load({again}, Media{false, print, 800});
        El pa = el("html", {el("body", {el("h1"), el("p")})}), pb = pa;
        layout(a, pa);
        layout(b, pb);
        for (size_t i = 0; i < 2; ++i) {
            check(a.style(at(pa, {0, i}).ctx).color == b.style(at(pb, {0, i}).ctx).color, "the serialized sheet styles the same");
        }
    }
}

// --- Diagnostics ---------------------------------------------------------------------------------

void diagnosticTests() {
    std::string text = readFile(fixtureDir + "/mistakes.css");
    check(!text.empty(), "the mistakes fixture is there");
    std::vector<Diagnostic> d;
    auto sheet = sheetOf(text, &d, Origin::Theme, nullptr, "mistakes.css");
    struct Want {
        const char* part;
        int line, column, level;
    };
    const Want wants[] = {
        {"unknown property `colour`: did you mean `color`?", 3, 3, Diagnostic::Warning},
        {"`#ggg` is not a valid value for `color`", 4, 10, Diagnostic::Error},
        {"`display` has no effect: Tinta lays out blocks itself", 5, 3, Diagnostic::Warning},
        {"`16px line-height: 1.5` is not a valid value for `font-size`", 6, 14, Diagnostic::Error},
        {"`rgb(10 20)` is not a valid value for `background-color`", 8, 21, Diagnostic::Error},
        {"`div.sidebar` can never match a Markdown page", 10, 1, Diagnostic::Warning},
        {"`nav ul` can never match a Markdown page: Markdown makes no `nav`", 11, 1, Diagnostic::Warning},
        {"`:hover` is not supported; the rule is skipped", 12, 3, Diagnostic::Warning},
        {"`::first-line` is not supported; the rule is skipped", 13, 4, Diagnostic::Warning},
        {"unknown property `font-wieght`: did you mean `font-weight`?", 14, 6, Diagnostic::Warning},
        {"`transition` has no effect: Tinta does not animate", 14, 25, Diagnostic::Warning},
        {"`@keyframes` has no effect: Tinta does not animate", 15, 1, Diagnostic::Warning},
        {"unknown at-rule `@frobnicate`", 16, 1, Diagnostic::Error},
        {"`red !importnt` is not a valid value for `color`", 17, 13, Diagnostic::Error},
        {"nested rules are not supported; this one is skipped", 19, 3, Diagnostic::Warning},
        {"expected `:` after `padding`", 22, 21, Diagnostic::Error},
        {"`border-radius`: elliptical corners are drawn round", 23, 23, Diagnostic::Warning},
        {"`(hover)` is never true in Tinta", 24, 1, Diagnostic::Warning},
        {"`-webkit-font-smoothing` belongs to one browser; Tinta ignores it", 25, 14, Diagnostic::Note},
        {"a block is missing its `}`", 26, 4, Diagnostic::Warning},
    };
    for (const Want& w : wants) {
        check(hasDiagnostic(d, w.part, w.line, w.column, w.level),
              std::string("diagnostic `") + w.part + "` at " + std::to_string(w.line) + ":" + std::to_string(w.column));
    }
    bool files = std::all_of(d.begin(), d.end(), [](const Diagnostic& x) { return x.file == "mistakes.css"; });
    check(d.size() == sizeof(wants) / sizeof(wants[0]) && files, "exactly these diagnostics, in its file\n" + dump(d));
    Engine engine;
    engine.load({sheet}, Media());
    El page = el("html", {el("body", {el("h3"), el("h4"), el("a"), el("p")})});
    layout(engine, page);
    check(sameColor(engine.style(at(page, {0, 0}).ctx).color, "green") && sameColor(engine.style(at(page, {0, 1}).ctx).color, "red") &&
          sameColor(engine.style(at(page, {0}).ctx).color, "black") && sameColor(engine.style(at(page, {0, 2}).ctx).color, "black") &&
          engine.style(at(page, {0, 3}).ctx).margin[0] == 16, "what is right in the sheet still applies");
    std::vector<Diagnostic> more;
    sheetOf("p { colr: red; bckground-color: red; zzzzzz: 1 }\n  @media print { h1 { font-weight: bolder !important; } }", &more);
    check(hasDiagnostic(more, "did you mean `color`?", 1, 5) && hasDiagnostic(more, "did you mean `background-color`?", 1, 16) &&
          hasDiagnostic(more, "unknown property `zzzzzz`", 1, 38) && !hasDiagnostic(more, "zzzzzz`: did you mean") && more.size() == 3,
          "suggestions only for names close to one\n" + dump(more));
    std::vector<Diagnostic> families;
    sheetOf("p { transform-style: flat; grid-template-columns: 1fr; mask-image: none; scroll-margin-top: 4px }", &families);
    check(hasDiagnostic(families, "`transform-style` has no effect: Tinta does not animate") &&
          hasDiagnostic(families, "`grid-template-columns` has no effect: Tinta lays out blocks itself") &&
          hasDiagnostic(families, "`mask-image` is not drawn by Tinta") &&
          hasDiagnostic(families, "`scroll-margin-top` has no effect"), "families of properties Tinta leaves alone\n" + dump(families));
}

// --- Interning ---------------------------------------------------------------------------------------

void internTests() {
    auto sheet = sheetOf("tr:nth-child(even) td { background-color: #eeeeee } td { padding: 4px }"
                         " li + li { margin-top: 2px } h2 ~ p { color: #ff0000 } td:first-child { font-weight: bold }");
    Engine engine;
    engine.load({sheet}, Media());
    El table = el("table", {el("tbody")});
    for (int r = 0; r < 2000; ++r) table.kids[0].kids.push_back(el("tr", {el("td"), el("td"), el("td")}));
    El list = el("ul");
    for (int i = 0; i < 500; ++i) list.kids.push_back(el("li"));
    El body = el("body", {el("p"), el("h2"), table, list});
    for (int i = 0; i < 1000; ++i) body.kids.push_back(el("p"));
    El page = el("html", {body});
    auto start = std::chrono::steady_clock::now();
    layout(engine, page);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::set<Engine::Context> rows, cells, items, paragraphs;
    El& tbody = at(page, {0, 2, 0});
    for (El& tr : tbody.kids) {
        rows.insert(tr.ctx);
        for (El& td : tr.kids) cells.insert(td.ctx);
    }
    for (El& li : at(page, {0, 3}).kids) items.insert(li.ctx);
    for (El& p : at(page, {0}).kids) {
        if (p.tag == "p") paragraphs.insert(p.ctx);
    }
    check(rows.size() == 2 && cells.size() == 4, "2,000 rows make 2 row contexts and 4 cell contexts: " +
                                                  std::to_string(rows.size()) + " and " + std::to_string(cells.size()));
    check(items.size() == 2 && paragraphs.size() == 2, "li + li and h2 ~ p tell apart only the first and the rest");
    check(sameColor(engine.style(tbody.kids[1].kids[1].ctx).backgroundColor, "#eeeeee") &&
          engine.style(tbody.kids[0].kids[1].ctx).backgroundColor.a == 0 &&
          engine.style(tbody.kids[0].kids[0].ctx).fontWeight == 700 && engine.style(tbody.kids[0].kids[2].ctx).fontWeight == 400,
          "even rows and first cells styled");
    check(engine.style(at(page, {0, 3, 0}).ctx).margin[0] == 0 && engine.style(at(page, {0, 3, 499}).ctx).margin[0] == 2 &&
          !sameColor(engine.style(at(page, {0, 0}).ctx).color, "#ff0000") && sameColor(engine.style(at(page, {0, 999}).ctx).color, "#ff0000"),
          "sibling selectors over long runs");
    std::vector<Engine::Context> before;
    for (El& tr : tbody.kids) before.push_back(tr.kids[2].ctx);
    layout(engine, page);
    std::vector<Engine::Context> after;
    for (El& tr : tbody.kids) after.push_back(tr.kids[2].ctx);
    check(before == after, "contexts are stable across calls");
    Element td;
    td.tag = "td";
    td.index = 1;
    td.count = 3;
    td.typeIndex = 1;
    td.typeCount = 3;
    check(engine.child(tbody.kids[7].ctx, td) == tbody.kids[7].kids[0].ctx, "a lone child() call finds the same context");
    std::vector<Element> trs(2000);
    for (int r = 0; r < 2000; ++r) {
        trs[r].tag = "tr";
        trs[r].index = trs[r].typeIndex = r + 1;
        trs[r].count = trs[r].typeCount = 2000;
    }
    auto childStart = std::chrono::steady_clock::now();
    for (int r = 0; r < 2000; ++r) engine.child(tbody.ctx, trs[r], trs.data(), (size_t)r);
    double childUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - childStart).count() / 2000;
    Engine plain;
    plain.load({sheetOf("td { padding: 4px } p + p { color: red }")}, Media());
    layout(plain, page);
    std::set<Engine::Context> plainRows;
    for (El& tr : tbody.kids) plainRows.insert(tr.ctx);
    check(plainRows.size() == 1, "without positional selectors every row is one context");
    std::cout << "  interning: " << (6000 + 500 + 1002) << " elements laid out in " << ms << " ms; child() takes "
              << childUs << " us\n";
}

// --- The fixture themes ----------------------------------------------------------------------------

ImportSource fileSource() {
    return [](std::string_view name, std::string_view from, std::string& text, std::string& file) {
        size_t slash = from.find_last_of("/\\");
        std::string path = std::string(from.substr(0, slash == std::string_view::npos ? 0 : slash + 1)) + std::string(name);
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::stringstream s;
        s << in.rdbuf();
        text = s.str();
        file = path;
        return true;
    };
}

struct Themed {
    Engine engine;
    std::vector<Diagnostic> diagnostics;
    std::shared_ptr<const Sheet> sheet;
    El page;
    Themed(const std::string& name, Media media) {
        std::string path = fixtureDir + "/" + name;
        sheet = parse(readFile(path), path, Origin::Theme, fileSource(), diagnostics);
        engine.load({sheet}, media);
        page = el("html", {el("body", {
            el("h1"),                                                                      // 0
            el("p", {with(el("a"), "href", "https://example.com"), el("strong"), el("code")}),  // 1
            el("h2"),                                                                      // 2
            el("pre", {el("code.language-python", {el("span.token.keyword"), el("span.token.comment")})}),  // 3
            el("table", {el("tbody", {el("tr", {el("td")}), el("tr", {el("td")})})}),      // 4
            el("ul", {el("li", {el("ul", {el("li"), el("li")})}), el("li")}),              // 5
            el("ol", {el("li", {el("ol", {el("li")})})}),                                  // 6
            el("blockquote.alert-note", {el("p.alert-title")}),                            // 7
            el("h6"),                                                                      // 8
            el("mark"),                                                                    // 9
            el("p", {el("img"), with(el("a"), "href", "#local")}),                         // 10
            el("blockquote", {el("p")}),                                                   // 11
            el("h3"),                                                                      // 12
        })});
        layout(engine, page);
    }
    const ComputedStyle& operator()(std::initializer_list<size_t> path) { return engine.style(at(page, path).ctx); }
    const ComputedStyle* pseudo(std::initializer_list<size_t> path, Pseudo which) { return engine.pseudo(at(page, path).ctx, which); }
    int errors() const {
        return (int)std::count_if(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) { return d.level == Diagnostic::Error; });
    }
};

void paperTests() {
    Themed t("paper-light.css", Media());
    check(t.errors() == 0 && hasDiagnostic(t.diagnostics, "`border-collapse` has no effect") && t.diagnostics.size() == 1,
          "Paper Light has no errors\n" + dump(t.diagnostics));
    const ComputedStyle& body = t({0});
    check(sameColor(body.color, "#24292f") && sameColor(body.backgroundColor, "#ffffff") && body.fontSize == 16 &&
          near(body.lineHeight, 27.2f) && body.fontFamily.size() == 3 && body.fontFamily[0] == "Segoe UI" &&
          body.maxWidth.kind == Length::Px && body.maxWidth.value == 736 && body.margin[1] == 0 && body.padding[0] == 32 &&
          body.padding[3] == 24, "Paper's body");
    check(t({}).colorScheme == ColorScheme::Light && t({}).accentColor && sameColor(*t({}).accentColor, "#3366cc"), "Paper's :root");
    const ComputedStyle& h1 = t({0, 0});
    check(h1.fontSize == 32 && h1.fontWeight == 600 && h1.border[2].width == 1 && h1.border[2].style == LineStyle::Solid &&
          sameColor(h1.border[2].color, "#d0d7de") && h1.lineHeight == 40 && h1.margin[0] == 48 && near(h1.padding[2], 9.6f) &&
          h1.fontFamily[0] == "Segoe UI Semibold", "Paper's h1");
    check(sameColor(t({0, 8}).color, "#57606a") && near(t({0, 8}).fontSize, 13.6f), "Paper's h6");
    const ComputedStyle& a = t({0, 1, 0});
    check(sameColor(a.color, "#0969da") && a.decoration.underline && a.decoration.offset == 2, "Paper's links");
    const ComputedStyle& code = t({0, 1, 2});
    check(near(code.fontSize, 13.6f) && sameColor(code.backgroundColor, 175 / 255.0f, 184 / 255.0f, 193 / 255.0f, 0.2f) &&
          code.radius[0] == 6 && code.fontFamily[0] == "Cascadia Code", "Paper's inline code");
    const ComputedStyle& pre = t({0, 3});
    check(sameColor(pre.backgroundColor, "#f6f8fa") && pre.radius[2] == 6 && pre.padding[1] == 16 && near(pre.lineHeight, 23.2f),
          "Paper's code block");
    const ComputedStyle& preCode = t({0, 3, 0});
    check(preCode.backgroundColor.a == 0 && near(preCode.fontSize, 13.6f) && preCode.padding[0] == 0, "Paper's pre > code");
    check(sameColor(t({0, 3, 0, 0}).color, "#cf222e") && t({0, 3, 0, 1}).fontStyle == FontStyle::Italic, "Paper's tokens");
    check(t({0, 4, 0, 0, 0}).backgroundColor.a == 0 && sameColor(t({0, 4, 0, 1, 0}).backgroundColor, "#f6f8fa") &&
          t({0, 4, 0, 1, 0}).border[3].width == 1 && t({0, 4, 0, 1, 0}).padding[1] == 13, "Paper's table");
    const ComputedStyle* marker = t.pseudo({0, 5, 0, 0, 0}, Pseudo::Marker);
    check(marker && marker->content.size() == 1 && marker->content[0].text == "- " && !t.pseudo({0, 5, 0}, Pseudo::Marker),
          "Paper's nested list markers");
    check(t({0, 5, 1}).margin[0] == 4 && t({0, 5, 0}).margin[0] == 0, "li + li");
    check(t({0, 6, 0, 0}).listStyle == ListStyle::LowerAlpha && t({0, 6, 0, 0, 0}).listStyle == ListStyle::LowerAlpha &&
          t({0, 6}).listStyle == ListStyle::Decimal, "ol ol");
    check(sameColor(t({0, 7}).border[3].color, "#0969da") && t({0, 7}).border[3].width == 4 && t({0, 7, 0}).fontWeight == 600,
          "Paper's callout");
    const ComputedStyle* selection = t.pseudo({0, 1}, Pseudo::Selection);
    check(selection && sameColor(selection->backgroundColor, "#b6d7ff"), "Paper's selection");
    Themed print("paper-light.css", Media{false, true, 800});
    check(near(print({0}).fontSize, 14.667f) && print({0}).maxWidth.kind == Length::None &&
          sameColor(print({0, 1, 0}).color, "#24292f"), "Paper in print");
}

void nightTests() {
    Themed t("night-dark.css", Media{true, false, 800});
    check(t.errors() == 0 && t.diagnostics.empty(), "Night has no problems\n" + dump(t.diagnostics));
    auto colorOf = [](const char* text) { return *parseColor(text, true); };
    auto same = [](const Color& a, const Color& b) { return sameColor(a, b.r, b.g, b.b, b.a); };
    check(t({}).colorScheme == ColorScheme::Dark && t({}).accentColor && same(*t({}).accentColor, colorOf("oklch(72% 0.14 250)")) &&
          t({}).caretColor, "Night's :root");
    const ComputedStyle& body = t({0});
    check(same(body.backgroundColor, colorOf("oklch(18% 0.02 260)")) && same(body.color, colorOf("oklch(88% 0.02 260)")) &&
          body.fontSize == 17 && near(body.lineHeight, 27.2f) && body.counterReset.size() == 1, "Night's body");
    check(near(t({0, 0}).fontSize, 37.25f) && near(t({0, 2}).fontSize, 23.8f) && near(t({0, 12}).fontSize, 18.7f) &&
          near(t({0, 0}).letterSpacing, -0.3725f), "calc(), clamp() and max() font sizes");
    const ComputedStyle* chapter = t.pseudo({0, 0}, Pseudo::Before);
    check(chapter && chapter->content.size() == 2 && chapter->content[0].kind == ContentPart::Counter &&
          chapter->content[0].style == ListStyle::UpperRoman && chapter->counterIncrement.size() == 1, "chapter numbers");
    check(t({0, 1, 0}).decoration.underline && !t({0, 10, 1}).decoration.underline, "a:not([href^=\"#\"])");
    check(hasFeature(t({0, 1, 2}).fontFeatures, "liga", 0) && near(t({0, 1, 2}).backgroundColor.a, 0.8f), "Night's code");
    const ComputedStyle& pre = t({0, 3});
    check(pre.backgroundImage && pre.backgroundImage->angle == 180 && pre.backgroundImage->stops.size() == 2 &&
          pre.boxShadow.size() == 1 && near(pre.boxShadow[0].color.a, 0.4f) && pre.border[0].width == 1, "Night's code block");
    check(same(t({0, 11}).color, colorOf("color-mix(in oklab, oklch(88% 0.02 260) 60%, oklch(18% 0.02 260))")) &&
          t({0, 11}).fontStyle == FontStyle::Italic, "color-mix() through custom properties");
    check(near(t({0, 4, 0, 0}).backgroundColor.a, 0.03f) && t({0, 4, 0, 1}).backgroundColor.a == 0, "odd rows");
    check(same(t({0, 9}).backgroundColor, colorOf("hwb(50 10% 40%)")) && same(t({0, 3, 0, 0}).color, colorOf("lch(70% 60 300)")),
          "hwb() and lch()");
    const ComputedStyle* selection = t.pseudo({0, 1}, Pseudo::Selection);
    check(selection && near(selection->backgroundColor.a, 0.35f), "Night's selection");
}

void inkTests() {
    Themed light("ink-light-dark.css", Media());
    check(light.errors() == 0 && light.diagnostics.empty(), "Ink has no problems\n" + dump(light.diagnostics));
    const std::vector<FontFace>& faces = fontFaces(*light.sheet);
    check(faces.size() == 1 && faces[0].family == "Literata" && faces[0].sources.size() == 2 &&
          faces[0].sources[0] == fixtureDir + "/fonts/Literata.ttf" && faces[0].weightMin == 200 && faces[0].weightMax == 900,
          "Ink's font face");
    const ComputedStyle& body = light({0});
    check(sameColor(body.color, "#1f2328") && sameColor(body.backgroundColor, "#fbf8f1") && body.fontSize == 17 &&
          near(body.lineHeight, 28.05f) && body.fontFamily == std::vector<std::string>{"Literata", "Source Han Serif SC", "serif"} &&
          body.maxWidth.value == 748 && body.padding[0] == 24, "Ink's body, with its imported base");
    check(near(light({0, 0}).fontSize, 37.4f) && light({0, 0}).fontWeight == 700 && light({0, 0}).fontFamily[0] == "Inter",
          "Ink's h1 from the base and the theme");
    check(sameColor(light({0, 2}).border[2].color, 0x1f / 255.0f, 0x23 / 255.0f, 0x28 / 255.0f, 0.15f, 2.0f / 255),
          "a rule colour mixed from a variable");
    check(sameColor(light({0, 1, 0}).color, "#3366cc") && light({0, 1, 0}).decoration.offset == 3, "var() fallback for links");
    check(sameColor(light({0, 1, 2}).color, "#b35900") && light({0, 1, 2}).backgroundColor.a == 0, "Ink's code");
    check(near(light({0, 4, 0, 1, 0}).backgroundColor.a, 0.04f) && light({0, 4, 0, 0, 0}).backgroundColor.a == 0, "even rows");
    check(sameColor(light({0, 9}).backgroundColor, "oklch(92% 0.08 95)") && sameColor(light({0, 4}).color, "#1f2328"),
          "@supports what Tinta draws, not what it does not");
    check(light({0, 10, 0}).opacity == 1 && !light.pseudo({0, 1, 0}, Pseudo::After), "light, on screen");
    Themed dark("ink-light-dark.css", Media{true, false, 800});
    check(sameColor(dark({0}).color, "#d8dee9") && sameColor(dark({0}).backgroundColor, "#1b1d22") &&
          sameColor(dark({0, 1, 2}).color, "#f0a35e") && near(dark({0, 10, 0}).opacity, 0.9f), "Ink in dark");
    Themed narrow("ink-light-dark.css", Media{false, false, 500});
    check(narrow({0}).fontSize == 15, "Ink in a narrow column");
    Themed print("ink-light-dark.css", Media{false, true, 800});
    const ComputedStyle* href = print.pseudo({0, 1, 0}, Pseudo::After);
    check(near(print({0}).fontSize, 14.667f) && href && href->content.size() == 3 && href->content[1].kind == ContentPart::Attr,
          "Ink's print import");
    std::string spec = "@import \"Paper\";\n@font-face { font-family: \"Literata\"; src: url(\"fonts/Literata.ttf\"); }\n"
                       ":root {\n  color-scheme: light dark;\n  accent-color: oklch(55% 0.15 250);\n  --ink: light-dark(#1f2328, #d8dee9);\n}\n"
                       "body {\n  color: var(--ink);\n  background: light-dark(#fbf8f1, #1b1d22);\n"
                       "  font: 17px/1.65 \"Literata\", \"Source Han Serif SC\", serif;\n  max-width: 44em;\n}\n"
                       "h1, h2 { font-family: \"Inter\", system-ui; letter-spacing: -0.01em; }\n"
                       "h2 { border-bottom: 1px solid color-mix(in oklab, var(--ink) 15%, transparent); }\n"
                       "a { color: var(--accent, #3366cc); text-underline-offset: 3px; }\ncode { background: none; color: #b35900; }\n"
                       "pre { border-radius: 8px; padding: 12px 16px; }\nblockquote { border-left: 3px solid #b35900; font-style: italic; }\n"
                       "ul ul > li::marker { content: \"- \"; }\n"
                       "tr:nth-child(even) td { background: color-mix(in oklab, var(--ink) 4%, transparent); }\n"
                       ".callout[data-callout=\"warning\"] { border-color: #d97706; }\n.token.keyword { color: #a626a4; }\n"
                       "@media print { body { background: white; font-size: 11pt; } }\n";
    std::map<std::string, std::string> files = {{"Paper", readFile(fixtureDir + "/paper-light.css")}};
    std::vector<Diagnostic> d;
    auto sheet = sheetOf(spec, &d, Origin::Theme, memorySource(files), "Ink.css");
    std::vector<Diagnostic> mine;
    for (const Diagnostic& x : d) {
        if (x.file == "Ink.css") mine.push_back(x);
    }
    check(mine.empty() && sheet->rules.size() > 40, "the spec's example theme reads cleanly\n" + dump(d));
}

// --- Speed ----------------------------------------------------------------------------------------

// --- Hostile input --------------------------------------------------------------------------------

void robustnessTests() {
    std::vector<Diagnostic> d;
    auto parens = sheetOf("p { color: " + std::string(5000, '(') + " }", &d);
    check(parens->rules.empty(), "5,000 open brackets end in nothing, not a crash");
    std::string media;
    for (int i = 0; i < 200; ++i) media += "@media all { ";
    media += "p { color: red }";
    for (int i = 0; i < 200; ++i) media += " }";
    sheetOf(media, &d);
    check(hasDiagnostic(d, "at-rules nest too deeply"), "200 nested @media blocks are reported");
    std::string pseudo = "p:is(";
    for (int i = 0; i < 300; ++i) pseudo += ":is(";
    sheetOf(pseudo + std::string(301, ')') + " { color: red }", &d);
    std::string garbage("\x00\xff{{}};;@@@{\"\\", 15);
    garbage += "url(\x01) #- -- <!-- @media { ) ] } \\";
    sheetOf(garbage, &d);
    sheetOf("", &d);
    sheetOf("/* only a comment", &d);
    check(true, "garbage, empty and comment-only sheets parse");
    Engine engine;
    engine.load({sheetOf("blockquote { margin-left: 1px } :root::selection { color: #ff0000 } blockquote::before { content: \"|\" }")},
                Media());
    Element quote;
    quote.tag = "blockquote";
    quote.index = quote.count = quote.typeIndex = quote.typeCount = 1;
    Engine::Context ctx = engine.root();
    for (int i = 0; i < 3000; ++i) ctx = engine.child(ctx, quote);
    const ComputedStyle* selection = engine.pseudo(ctx, Pseudo::Selection);
    check(engine.style(ctx).margin[3] == 1 && selection && sameColor(selection->color, "#ff0000") &&
          engine.pseudo(ctx, Pseudo::Before), "a page 3,000 elements deep styles without recursing");
}

// Milliseconds to parse a sheet, the sheets kept so their freeing is not counted
double parseTime(const std::string& text, std::shared_ptr<const Sheet>& sheet) {
    const int runs = 40;
    std::vector<std::shared_ptr<const Sheet>> kept;
    std::vector<Diagnostic> d;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < runs; ++i) kept.push_back(parse(text, "speed.css", Origin::Theme, nullptr, d));
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / runs;
    sheet = kept.back();
    return ms;
}

void performanceTests() {
    // A typical theme, Paper Light over and over, and a heavy one: every
    // fixture with rules full of color-mix(), calc() and :not() after them
    std::string typical, paper = readFile(fixtureDir + "/paper-light.css");
    while (std::count(typical.begin(), typical.end(), '\n') < 500) typical += paper;
    std::string heavy;
    for (const char* name : {"paper-light.css", "night-dark.css", "ink-light-dark.css"}) {
        std::istringstream in(readFile(fixtureDir + "/" + name));
        std::string line;
        while (std::getline(in, line)) {
            if (line.compare(0, 7, "@import") != 0) heavy += line + "\n";
        }
    }
    int lines = (int)std::count(heavy.begin(), heavy.end(), '\n');
    for (int i = 0; lines < 500; ++i, lines += 3) {
        heavy += ".token.kind" + std::to_string(i) + ", h" + std::to_string(i % 6 + 1) + " > code:not(.x" + std::to_string(i) +
                 ") {\n  color: color-mix(in oklab, #3366cc " + std::to_string(i % 100) + "%, white);\n  margin: 0 0 calc(1em + " +
                 std::to_string(i) + "px); }\n";
    }
    std::shared_ptr<const Sheet> plain, sheet;
    double typicalMs = parseTime(typical, plain), heavyMs = parseTime(heavy, sheet);
#ifdef NDEBUG
    const double slack = 1;
#else
    const double slack = 50;  // a debug build checks every iterator
#endif
    check(plain->rules.size() > 200 && sheet->rules.size() > 150 && typicalMs < 20 * slack && heavyMs < 20 * slack,
          "500-line sheets parse quickly: " + std::to_string(typicalMs) + " and " + std::to_string(heavyMs) + " ms");
    El page = el("html", {el("body", {
        el("h1"), el("p", {el("a"), el("strong", {el("em")}), el("code"), el("mark")}), el("h2"),
        el("pre", {el("code", {el("span.token.keyword"), el("span.token.string"), el("span.token.comment")})}),
        el("table", {el("tbody", {el("tr", {el("td"), el("td")}), el("tr", {el("td"), el("td")})})}),
        el("ul", {el("li", {el("ul", {el("li"), el("li")})}), el("li"), el("li")}),
        el("blockquote.alert-note", {el("p.alert-title"), el("p", {el("strong")})}), el("hr"), el("h3"), el("h4")})});
    Engine engine;
    engine.load({sheet}, Media());
    layout(engine, page);
    std::vector<Engine::Context> all;
    std::function<void(const El&)> collect = [&](const El& e) {
        all.push_back(e.ctx);
        for (const El& k : e.kids) collect(k);
    };
    collect(page);
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end()), all.end());
    const int rounds = 50;
    auto start = std::chrono::steady_clock::now();
    for (int r = 0; r < rounds; ++r) {
        engine.setMedia(Media{r % 2 == 1, false, 800});
        for (Engine::Context c : all) engine.style(c);
    }
    double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / (rounds * all.size());
    check(us < 200 * slack, "a context's style computes quickly: " + std::to_string(us) + " us");
    std::cout << "  speed: 500-line sheets parse in " << typicalMs << " ms (typical) and " << heavyMs
              << " ms (heavy); a style computes in " << us << " us, over " << all.size() << " contexts\n";
}

}  // namespace

int main() {
    tokenizerTests();
    recoveryTests();
    specificityTests();
    matchingTests();
    cascadeTests();
    inheritanceTests();
    varTests();
    calcTests();
    colorTests();
    lightDarkTests();
    mediaTests();
    importTests();
    fontFaceTests();
    supportsTests();
    shorthandTests();
    pseudoTests();
    serializeTests();
    diagnosticTests();
    internTests();
    paperTests();
    nightTests();
    inkTests();
    robustnessTests();
    performanceTests();
    std::cout << "css engine: " << checks - failures << " of " << checks << " checks passed\n";
    return failures ? 1 : 0;
}
