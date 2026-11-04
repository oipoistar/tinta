// Typora and Obsidian themes imported as Tinta sheets (css.h,
// docs/css-themes.md): the fixtures in tests/fixtures/theme-css/import
// rewritten, read back without errors, and styled through the engine as the
// apps style them: the body, headings, code, quotes and callouts, light and
// dark; and the notes for what was dropped, with their lines.

#include "css.h"

#include <fstream>
#include <iostream>
#include <map>
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

const std::string fixtureDir = TINTA_CSS_IMPORT_FIXTURE_DIR;

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

int lineOf(const std::string& text, const std::string& needle) {
    size_t at = text.find(needle);
    if (at == std::string::npos) return 0;
    int line = 1;
    for (size_t i = 0; i < at; ++i) line += text[i] == '\n';
    return line;
}

bool sameColor(const Color& c, const char* text, float eps = 1.5f / 255) {
    std::optional<Color> want = parseColor(text);
    return want && std::fabs(c.r - want->r) <= eps && std::fabs(c.g - want->g) <= eps && std::fabs(c.b - want->b) <= eps &&
           std::fabs(c.a - want->a) <= eps;
}

bool smallCaps(const ComputedStyle& s) {
    const uint32_t smcp = 's' | 'm' << 8 | 'c' << 16 | (uint32_t)'p' << 24;
    for (const FontFeature& f : s.fontFeatures) {
        if (f.tag == smcp && f.value == 1) return true;
    }
    return false;
}

std::string dump(const std::vector<Diagnostic>& list) {
    std::string out;
    for (const Diagnostic& d : list) {
        out += "  " + d.file + ":" + std::to_string(d.line) + ":" + std::to_string(d.column) + " [" + "EWN"[d.level] + "] " +
               d.message + "\n";
    }
    return out;
}

// A theme imported, its notes, and the result read back as a sheet
struct Imported {
    std::string source, css;
    std::vector<Diagnostic> notes, diagnostics;
    std::shared_ptr<const Sheet> sheet;
    Imported(const std::string& text, const std::string& name, Flavor flavor = Flavor::Detect,
             const ImportSource& imports = nullptr)
        : source(text) {
        css = importTheme(text, flavor, notes, name, imports);
        sheet = parse(css, "imported.css", Origin::Theme, nullptr, diagnostics);
    }
    static Imported fixture(const std::string& name) { return Imported(readFile(fixtureDir + "/" + name), name); }
    int errors() const {
        int n = 0;
        for (const Diagnostic& d : diagnostics) n += d.level == Diagnostic::Error;
        return n;
    }
    // A note holding `part`, on the line where `at` is in the source
    bool noted(const std::string& part, const std::string& at = std::string()) const {
        int line = at.empty() ? 0 : lineOf(source, at);
        for (const Diagnostic& d : notes) {
            if (d.message.find(part) != std::string::npos && (!line || d.line == line)) return true;
        }
        return false;
    }
};

// Styles of the imported sheet on a page as Tinta lays it out
struct Styled {
    Engine engine;
    Styled(const Imported& imported, bool dark = false, bool print = false) {
        engine.load({imported.sheet}, Media{dark, print, 800});
    }
    // An element by its path: "p", "blockquote.alert-warning", "li.task:checked", "input"
    Engine::Context at(std::initializer_list<const char*> path) {
        Engine::Context c = engine.root();
        for (const char* spec : path) {
            std::string s = spec;
            bool checked = false;
            size_t colon = s.find(":checked");
            if (colon != std::string::npos) {
                checked = true;
                s.erase(colon);
            }
            std::vector<std::string> parts;
            size_t from = 0;
            for (size_t dot = s.find('.'); dot != std::string::npos; dot = s.find('.', from)) {
                parts.push_back(s.substr(from, dot - from));
                from = dot + 1;
            }
            parts.push_back(s.substr(from));
            Element e;
            e.tag = parts[0];
            for (size_t k = 1; k < parts.size(); ++k) e.classes.push_back(parts[k]);
            if (parts[0] == "input") e.attributes.emplace_back("type", "checkbox");
            e.checked = checked;
            e.index = e.count = e.typeIndex = e.typeCount = 1;
            c = engine.child(c, e);
        }
        return c;
    }
    const ComputedStyle& operator()(std::initializer_list<const char*> path) { return engine.style(at(path)); }
};

// --- Typora ---------------------------------------------------------------------------

void typora() {
    Imported t = Imported::fixture("typora-night.css");
    check(detectFlavor(t.source) == Flavor::Typora, "Night Ink is a Typora theme");
    check(t.errors() == 0, "the imported Typora theme reads without errors\n" + dump(t.diagnostics) + t.css);
    check(t.css.find("MIT licence; this comment is kept") != std::string::npos, "its licence comment is kept");
    Styled s(t);
    const ComputedStyle& root = s({});
    check(root.colorScheme == ColorScheme::Dark, "a dark --bg-color makes a dark theme");
    check(root.accentColor && sameColor(*root.accentColor, "#58a6ff") && root.custom("--ui-panel") == "#16181b",
          "--primary-color is the accent, --side-bar-bg-color the panels'");
    const ComputedStyle& body = s({"body"});
    check(sameColor(body.backgroundColor, "#1e2125") && sameColor(body.color, "#c9d1d9") && !body.fontFamily.empty() &&
              body.fontFamily[0] == "Inter" && body.maxWidth.value == 860,
          "#write and the variables style the body\n" + t.css);
    check(sameColor(s({"body", "h1"}).color, "#ffa657") && std::fabs(s({"body", "h1"}).fontSize - 35.2f) < 0.01f,
          "#write h1 is h1");
    check(sameColor(s({"body", "h2"}).color, "#d2a8ff") && sameColor(s({"body", "h3"}).color, "#7ee787"),
          "#write > h2 is body > h2; h3 and #write h3 are one");
    check(sameColor(s({"body", "p", "a"}).color, "#58a6ff"), "links");
    const ComputedStyle& code = s({"body", "p", "code"});
    check(sameColor(code.color, "#ff7b72") && sameColor(code.backgroundColor, "#2d333b") && !code.fontFamily.empty() &&
              code.fontFamily[0] == "JetBrains Mono",
          "inline code, and --monospace");
    const ComputedStyle& pre = s({"body", "pre"});
    const ComputedStyle& preCode = s({"body", "pre", "code"});
    check(sameColor(pre.backgroundColor, "#161b22") && sameColor(preCode.color, "#e6edf3") && preCode.backgroundColor.a == 0 &&
              pre.padding[0] == 8,
          ".md-fences is pre, and Typora's code { } stays off code blocks");
    check(sameColor(s({"body", "pre", "code", "span.token.keyword"}).color, "#ff7b72") &&
              sameColor(s({"body", "pre", "code", "span.token.comment"}).color, "#8b949e") &&
              s({"body", "pre", "code", "span.token.comment"}).fontStyle == FontStyle::Italic &&
              sameColor(s({"body", "pre", "code", "span.token.string"}).color, "#a5d6ff"),
          "CodeMirror's tokens are Tinta's");
    check(sameColor(s({"body", "blockquote"}).border[3].color, "#3b434b"), "the quote's bar");
    check(sameColor(s({"body", "blockquote.alert-warning"}).border[3].color, "#d29922") &&
              sameColor(s({"body", "blockquote.alert-warning", "p.alert-title"}).color, "#d29922") &&
              !sameColor(s({"body", "blockquote.alert-note", "p.alert-title"}).color, "#d29922"),
          "a warning alert, its title only its own");
    check(sameColor(s({"body", "p", "mark"}).backgroundColor, "#bb800966"), "highlights");
    const ComputedStyle* selection = s.engine.pseudo(s.at({"body", "p"}), Pseudo::Selection);
    check(selection && sameColor(selection->backgroundColor, "#3b5070"), "--select-text-bg-color");
    check(sameColor(s({"body", "ul", "li.task:checked"}).color, "#8b949e") && s({"body", "ul", "li.task", "input"}).accentColor,
          "tasks, done and their boxes");
    check(sameColor(s({"body", "div.frontmatter"}).color, "#8b949e"), "the front matter block");
    check(std::fabs(s({"body", "h4"}).letterSpacing - 0.16f) < 0.01f, "[mdtype=heading] is every heading");
    Styled print(t, false, true);
    check(print({"body"}).padding[0] == 0 && s({"body"}).padding[0] == 30 && print({}).fontSize == 12,
          ".typora-export #write holds for print only");

    for (const char* dropped : {"`#typora-sidebar`", "`.file-list-item`", "`.file-tree-node`", "`footer.ty-footer`",
                                "`.megamenu-menu`", "`#megamenu-content`"}) {
        check(t.noted(std::string(dropped) + " is dropped: it styles the app's window"), std::string(dropped) + " is dropped with a note");
    }
    check(t.noted("`#typora-sidebar` is dropped", "#typora-sidebar {"), "the note is on the rule's line\n" + dump(t.notes));
    check(t.noted("is dropped: it styles scrollbars", "::-webkit-scrollbar") &&
              t.noted("`#write .md-focus` is dropped: it styles what Typora shows while editing") &&
              t.noted("`.cm-s-inner .CodeMirror-gutters` is dropped: it styles the app's editor") &&
              t.noted("`@keyframes` is dropped: Tinta does not animate"),
          "scrollbars, editing marks, the editor and animations");
    check(t.noted("`@import` of `https://fonts.googleapis.com", "@import url(\"https") &&
              t.noted("`@include-when-export` is dropped", "@include-when-export"),
          "web fonts are dropped with a note");
    check(t.css.find("googleapis") == std::string::npos && t.css.find("@charset") == std::string::npos &&
              t.css.find("sidebar") == std::string::npos,
          "and are gone from the sheet");
    check(t.css.find("body > h2") != std::string::npos && t.css.find("blockquote.alert-warning > p.alert-title") != std::string::npos &&
              t.css.find("@media print {\nbody {") != std::string::npos,
          "the rewritten selectors read as Tinta's\n" + t.css);

    // A light theme, and one that follows Windows
    Imported light(":root { --bg-color: #fdfdfd; --text-color: #333 }\n#write { line-height: 1.6 }\n", "light.css", Flavor::Typora);
    Styled l(light);
    check(l({}).colorScheme == ColorScheme::Light && sameColor(l({"body"}).color, "#333333") && light.errors() == 0,
          "a light --bg-color makes a light theme");
    Imported follows(":root { --bg-color: #fff }\n@media (prefers-color-scheme: dark) { :root { --bg-color: #111 } }\n#write { }",
                     "follows.css", Flavor::Typora);
    Styled night(follows, true);
    check(night({}).colorScheme == ColorScheme::LightDark && sameColor(night({"body"}).backgroundColor, "#111111"),
          "a prefers-color-scheme query makes it follow Windows");
}

// --- Obsidian ----------------------------------------------------------------------------

void obsidian() {
    Imported t = Imported::fixture("obsidian-dusk.css");
    check(detectFlavor(t.source) == Flavor::Obsidian, "Dusk is an Obsidian theme");
    check(t.errors() == 0, "the imported Obsidian theme reads without errors\n" + dump(t.diagnostics) + t.css);
    check(t.css.find("color-scheme: light dark") != std::string::npos &&
              t.css.find("--background-primary: light-dark(#fbf8f1, #1b1d22)") != std::string::npos &&
              t.css.find("--h1-color: var(--text-accent)") != std::string::npos,
          ".theme-light and .theme-dark meet in :root with light-dark()\n" + t.css);
    for (bool dark : {false, true}) {
        Styled s(t, dark);
        std::string side = dark ? " (dark)" : " (light)";
        check(s({}).colorScheme == ColorScheme::LightDark, "it follows Windows" + side);
        const ComputedStyle& body = s({"body"});
        check(sameColor(body.backgroundColor, dark ? "#1b1d22" : "#fbf8f1") && sameColor(body.color, dark ? "#d8dee9" : "#2e3338") &&
                  !body.fontFamily.empty() && body.fontFamily[0] == "Literata",
              "the body from --background-primary, --text-normal and --font-text-theme" + side);
        const ComputedStyle& h1 = s({"body", "h1"});
        check(sameColor(h1.color, dark ? "#b48ead" : "#8a5cf5") && std::fabs(h1.fontSize - 33.6f) < 0.01f && h1.fontWeight == 700,
              "h1 from --h1-color through --text-accent, --h1-size and --h1-weight" + side);
        check(sameColor(s({"body", "h2"}).color, dark ? "#ebcb8b" : "#b35900") && s({"body", "h2"}).fontWeight == 600,
              "h2, its weight from Obsidian's --font-semibold" + side);
        check(sameColor(s({"body", "h4"}).color, dark ? "#b48ead" : "#8a5cf5") && smallCaps(s({"body", "h3"})) &&
                  sameColor(s({"body", "h3"}).color, dark ? "#d8dee9" : "#2e3338"),
              "the reading view's own rules; h3's --color-orange is Obsidian's, so unset" + side);
        check(sameColor(s({"body", "p", "em"}).color, dark ? "#88c0d0" : "#2e3338"), ".theme-dark em only in dark" + side);
        const ComputedStyle& code = s({"body", "p", "code"});
        check(sameColor(code.backgroundColor, dark ? "#23262d" : "#f0ebe0") && sameColor(code.color, dark ? "#c0c5ce" : "#4a4f55") &&
                  std::fabs(code.fontSize - 14) < 0.01f && !code.fontFamily.empty() && code.fontFamily[0] == "Cascadia Code",
              "inline code from --code-background, --code-normal, --code-size and --font-monospace-theme" + side);
        check(sameColor(s({"body", "pre"}).backgroundColor, dark ? "#23262d" : "#f0ebe0") &&
                  sameColor(s({"body", "pre", "code"}).color, dark ? "#c0c5ce" : "#4a4f55") &&
                  std::fabs(s({"body", "pre", "code"}).fontSize - 14) < 0.01f,
              "the code block" + side);
        check(sameColor(s({"body", "pre", "code", "span.token.keyword"}).color, dark ? "#c678dd" : "#a626a4") &&
                  sameColor(s({"body", "pre", "code", "span.token.string"}).color, dark ? "#98c379" : "#50a14f") &&
                  sameColor(s({"body", "pre", "code", "span.token.comment"}).color, dark ? "#5c6370" : "#a0a1a7"),
              "--code-keyword, --code-string and --code-comment" + side);
        const ComputedStyle& quote = s({"body", "blockquote"});
        check(sameColor(quote.border[3].color, dark ? "#4c566a" : "#d7cfbf") && quote.fontStyle == FontStyle::Italic,
              "the quote" + side);
        check(sameColor(s({"body", "p", "mark"}).backgroundColor, dark ? "rgba(235, 203, 139, 0.3)" : "rgba(255, 208, 0, 0.4)"),
              "--text-highlight-bg" + side);
        check(sameColor(s({"body", "blockquote.alert-warning"}).border[3].color, "rgb(236, 117, 0)") &&
                  sameColor(s({"body", "blockquote.alert-warning", "p.alert-title"}).color, "rgb(236, 117, 0)") &&
                  s({"body", "blockquote.alert-tip", "p.alert-title"}).fontWeight == 600,
              "callouts: --callout-color draws the warning's bar and title" + side);
        check(s({}).accentColor && sameColor(*s({}).accentColor, dark ? "#b48ead" : "#8a5cf5"), "--interactive-accent" + side);
        check(s({}).custom("--line-width") == (dark ? "46rem" : "44rem"), "a length on each side, by media" + side);
        check(sameColor(s({"body", "ul", "li.task:checked"}).color, dark ? "#d8dee9" : "#2e3338"),
              "a done task, its colour unset where the theme reads Obsidian's --text-faint" + side);
    }
    for (const char* dropped : {"`.workspace-leaf-content`", "`.nav-file-title`", "`.nav-folder-title`", "`.theme-dark .titlebar`",
                                "`body.dusk-wide-lines .markdown-preview-view`", "`.status-bar`", "`.modal`"}) {
        check(t.noted(std::string(dropped) + " is dropped: it styles the app's window"), std::string(dropped) + " is dropped");
    }
    check(t.noted("`.markdown-source-view .HyperMD-header-3` is dropped: it styles the app's editor") &&
              t.noted("`.markdown-source-view.mod-cm6 .cm-line` is dropped: it styles the app's editor"),
          "the editor's rules, one of them beside a selector that stays\n" + dump(t.notes));
    check(t.noted("`.callout[data-callout=\"info\"]` is dropped: it styles a callout Tinta has not", ".callout[data-callout=\"info\"]") &&
              t.noted("`.callout .callout-icon` is dropped"),
          "callouts Tinta has not");
    check(t.noted("`--color-orange` is read but the theme never sets it", "var(--color-orange)") &&
              t.noted("`--text-faint` is read but the theme never sets it") && !t.noted("`--font-semibold`") &&
              !t.noted("`--font-smaller`"),
          "what the theme leaves to Obsidian is noted, except the sizes and weights Tinta gives");
    check(t.css.find("ul > li {") != std::string::npos && t.css.find("li.task:checked") != std::string::npos &&
              t.css.find("span.wikilink") != std::string::npos && t.css.find("blockquote.alert-tip p.alert-title") != std::string::npos &&
              t.css.find("@media (max-width: 600px) {\nh1 {") != std::string::npos && t.css.find(".modal") == std::string::npos,
          ":not(.is-collapsed) always holds; tasks, wiki links, callout titles; a media block keeps what it can\n" + t.css);

    // As in Obsidian: the body's variables beat :root's, a theme class's the body's
    Imported v(":root { --text-normal: #111111; --h1-color: #aa0000 }\nbody { --h1-color: #bb0000 }\n"
               ".theme-dark { --h1-color: #0000cc; --text-normal: #eeeeee }\n.theme-light { --text-normal: #222222 }\n",
               "precedence.css", Flavor::Obsidian);
    Styled vl(v), vd(v, true);
    check(sameColor(vl({"body", "h1"}).color, "#bb0000") && sameColor(vd({"body", "h1"}).color, "#0000cc") &&
              sameColor(vl({"body"}).color, "#222222") && sameColor(vd({"body"}).color, "#eeeeee"),
          "which side's variable wins\n" + v.css);
    // A selector list over both sides, and a callout's colour on one
    Imported mixed(".theme-light h1, .theme-dark h2, h3 { color: #123456 }\n"
                   ".theme-dark .callout[data-callout=\"note\"] { --callout-color: 1, 2, 3 }\n"
                   ".theme-light { --a: 1 }\n",
                   "mixed.css", Flavor::Obsidian);
    Styled ml(mixed), md(mixed, true);
    check(mixed.errors() == 0 && sameColor(ml({"body", "h1"}).color, "#123456") && !sameColor(ml({"body", "h2"}).color, "#123456") &&
              sameColor(md({"body", "h2"}).color, "#123456") && !sameColor(md({"body", "h1"}).color, "#123456") &&
              sameColor(ml({"body", "h3"}).color, "#123456") && sameColor(md({"body", "h3"}).color, "#123456"),
          "each selector keeps its side\n" + mixed.css);
    check(sameColor(md({"body", "blockquote.alert-note"}).border[3].color, "rgb(1, 2, 3)") &&
              !sameColor(ml({"body", "blockquote.alert-note"}).border[3].color, "rgb(1, 2, 3)"),
          "a dark callout colour stays dark");

    Imported m = Imported::fixture("obsidian-midnight.css");
    check(m.errors() == 0 && m.css.find("prefers-color-scheme") == std::string::npos,
          "a dark-only theme needs no light-dark()\n" + dump(m.diagnostics) + m.css);
    Styled s(m);
    check(s({}).colorScheme == ColorScheme::Dark && sameColor(s({"body"}).backgroundColor, "#0f1419") &&
              sameColor(s({"body"}).color, "#bfbdb6"),
          "Midnight is dark, whatever Windows is");
    bool headings = true;
    for (const char* h : {"h1", "h2", "h3", "h4", "h5", "h6"}) headings &= sameColor(s({"body", h}).color, "#ffb454");
    check(headings && sameColor(s({"body", "p", "a"}).color, "#39bae6") && sameColor(s({"body", "p", "strong"}).color, "#f29668") &&
              sameColor(s({"body", "p", "em"}).color, "#d2a6ff"),
          "headings, links, bold and italic");
    check(sameColor(s({"body", "p", "code"}).color, "#aad94c") && sameColor(s({"body", "pre"}).backgroundColor, "#131721") &&
              s({"body", "pre"}).border[0].width == 1 && sameColor(s({"body", "hr"}).border[0].color, "#2d3640"),
          "code, the code block's border from .theme-dark .markdown-preview-view pre, and rules");
    const ComputedStyle* selection = s.engine.pseudo(s.at({"body", "p"}), Pseudo::Selection);
    check(selection && sameColor(selection->backgroundColor, "#273747"), "body.theme-dark's --text-selection");
    check(sameColor(s({"body", "blockquote.alert-tip"}).border[3].color, "rgb(149, 230, 203)"), "--callout-tip, an RGB triplet");
}

// --- Imports, other sheets, hostile input --------------------------------------------------

void imports() {
    std::map<std::string, std::string> files = {
        {"night/base.css", ":root { --bg-color: #ffffff; --text-color: #333333 }\n#write p { color: #222222 }\n#typora-sidebar { color: red }"},
        {"night/print.css", "#write { font-size: 11pt }"},
        {"night/a.css", "@import \"b.css\";\nh5 { color: #00ff00 }"},
        {"night/b.css", "@import \"a.css\";\nh6 { color: #0000ff }"},
    };
    ImportSource source = [&files](std::string_view name, std::string_view from, std::string& text, std::string& file) {
        size_t slash = from.find_last_of('/');
        std::string key = std::string(from.substr(0, slash == std::string_view::npos ? 0 : slash + 1)) + std::string(name);
        if (!files.count(key)) return false;
        text = files[key];
        file = key;
        return true;
    };
    std::string main = "@import \"night/base.css\";\n@import url(night/print.css) print;\n@import \"missing.css\";\n"
                       "@import \"night/a.css\";\n#write h1 { color: #ff0000 }\n";
    Imported t(main, "night.css", Flavor::Typora, source);
    check(t.errors() == 0 && t.css.find("@import") == std::string::npos, "imports are rewritten in place\n" + t.css);
    Styled s(t), print(t, false, true);
    check(sameColor(s({"body"}).backgroundColor, "#ffffff") && s({}).colorScheme == ColorScheme::Light &&
              sameColor(s({"body", "p"}).color, "#222222") && sameColor(s({"body", "h1"}).color, "#ff0000"),
          "an imported sheet's variables and rules");
    check(std::fabs(print({"body"}).fontSize - 14.667f) < 0.01f && std::fabs(s({"body"}).fontSize - 16) < 0.01f,
          "an import for print keeps its media");
    check(sameColor(s({"body", "h5"}).color, "#00ff00") && sameColor(s({"body", "h6"}).color, "#0000ff"), "a cycle reads each sheet once");
    bool sidebar = false;
    for (const Diagnostic& d : t.notes) sidebar |= d.file == "night/base.css" && d.line == 3 && d.message.find("#typora-sidebar") == 1;
    check(sidebar, "a note names the imported file and its line\n" + dump(t.notes));
    check(t.noted("cannot read `missing.css`") && t.noted("imports a sheet already read"), "a missing import and a cycle");

    Imported kept(main, "night.css", Flavor::Typora);
    size_t at = kept.css.find("@import \"night/base.css\";");
    check(at != std::string::npos && at < kept.css.find("h1 {") && kept.noted("is imported as it is, not rewritten"),
          "without a reader, imports stay, before every rule\n" + kept.css);

    // Neither app's: read as CSS, the web fonts and animations still out
    Imported plain("@import url(https://example.com/x.css);\np { color: #ff0000 }\n@keyframes spin { to { opacity: 0 } }\n",
                   "plain.css");
    check(plain.noted("neither a Typora nor an Obsidian theme") && plain.css.find("p { color: #ff0000 }") != std::string::npos &&
              plain.css.find("keyframes") == std::string::npos && plain.errors() == 0,
          "plain CSS\n" + plain.css);
    check(detectFlavor("p { color: red }") == Flavor::Detect, "no app's names: Detect");

    // Selectors the rewriting cannot read stay as written
    Imported odd(".theme-dark .a\\:b, #write .x::part(y) { color: #123456 }\n.theme-dark { --background-primary: #000 }", "odd.css",
                 Flavor::Obsidian);
    check(odd.css.find(".a\\:b") != std::string::npos, "an escaped class keeps its escape\n" + odd.css);

    // Hostile input never crashes, and the result still parses
    const char alphabet[] = "{}()[];:,.#*>+~@!\"'/\\ \n-_abcdw0189%&=|^$";
    uint32_t seed = 20261104;
    for (int round = 0; round < 3000; ++round) {
        static const char* const starts[] = {"", ".theme-dark ", "#write ", "@media print { ", ".callout[data-callout=\""};
        std::string text = starts[round % 5];
        seed = seed * 1103515245u + 12345u;
        size_t length = (seed >> 16) % 100;
        for (size_t i = 0; i < length; ++i) {
            seed = seed * 1103515245u + 12345u;
            text += alphabet[(seed >> 16) % (sizeof alphabet - 1)];
        }
        std::vector<Diagnostic> notes, diagnostics;
        std::string out = importTheme(text, round % 3 == 0 ? Flavor::Typora : round % 3 == 1 ? Flavor::Obsidian : Flavor::Detect, notes);
        parse(out, "fuzz.css", Origin::Theme, nullptr, diagnostics);
    }
    std::string deep;
    for (int i = 0; i < 300; ++i) deep += "@media all { ";
    deep += "#write { color: red }";
    std::vector<Diagnostic> notes;
    importTheme(deep, Flavor::Typora, notes);
    check(true, "random and deeply nested sheets import");
}

}  // namespace

int main(int argc, char** argv) {
    // --print writes each fixture as imported, and its notes
    if (argc > 1 && std::string(argv[1]) == "--print") {
        for (const char* name : {"typora-night.css", "obsidian-dusk.css", "obsidian-midnight.css"}) {
            Imported t = Imported::fixture(name);
            std::cout << "==== " << name << "\n" << t.css << "---- notes\n" << dump(t.notes) << "---- read back\n"
                      << dump(t.diagnostics);
        }
        return 0;
    }
    typora();
    obsidian();
    imports();
    std::cout << "css import: " << checks - failures << " of " << checks << " checks passed\n";
    return failures ? 1 : 0;
}
