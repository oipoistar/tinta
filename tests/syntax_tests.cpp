#include "syntax.h"
#include "d2d_init.h"
#include "render.h"
#include "utils.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <random>

namespace {
using Type = SyntaxTokenType;
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { if (failures < 40) std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
std::vector<SyntaxToken> lex(const std::wstring& line, int language, SyntaxState& state) {
    auto tokens = tokenizeLine(line, language, state);
    size_t offset = 0;
    for (const auto& token : tokens) {
        check(!token.text.empty(), "tokens are nonempty");
        check(token.text.data() == line.data() + offset, "token views are contiguous slices of original source");
        offset += token.text.size();
    }
    check(offset == line.size(), "tokenization preserves every source character");
    return tokens;
}
void expect(int language, SyntaxState& state, const std::wstring& line,
            const std::wstring& text, Type type) {
    auto tokens = lex(line, language, state);
    size_t start = line.find(text);
    check(start != std::wstring::npos, "expected text exists in case");
    size_t end = start + text.size(), offset = 0, covered = 0;
    for (const auto& token : tokens) {
        size_t next = offset + token.text.size();
        if (offset < end && next > start) {
            if (token.tokenType != type && failures < 40)
                std::wcerr << L"  language " << language << L", text [" << text << L"], token [" << token.text << L"]\n";
            check(token.tokenType == type, "requested span has expected category");
            covered += std::min(end, next) - std::max(start, offset);
        }
        offset = next;
    }
    check(covered == text.size(), "expected span is fully covered");
}
void single(int language, const std::wstring& line, const std::wstring& text, Type type) {
    SyntaxState state;
    expect(language, state, line, text, type);
}

void tokenTests() {
    struct Alias { const wchar_t* name; int id; };
    for (auto a : {Alias{L"SQL", LanguageSql}, {L"powershell", LanguagePowerShell},
                  {L"PS", LanguagePowerShell}, {L"pwsh", LanguagePowerShell}, {L"java", LanguageJava},
                  {L"php", LanguagePhp}, {L"html", LanguageHtml}, {L"htm", LanguageHtml},
                  {L"XML", LanguageXml}, {L"CSS", LanguageCss}, {L"yaml", LanguageYaml},
                  {L"yml", LanguageYaml}, {L"markdown", LanguageMarkdown}, {L"MD", LanguageMarkdown},
                  {L"c++", 1}, {L"py", 2}, {L"tsx", 3}, {L"json", 3}, {L"rs", 4},
                  {L"golang", 5}, {L"zsh", 6}, {L"c#", 7}, {L"unknown-language", 0}, {L"", 0}})
        check(detectLanguage(a.name) == a.id, "language label / alias maps correctly");

    single(LanguageSql, L"sElEcT id FROM records", L"sElEcT", Type::Keyword);
    single(LanguageSql, L"CASE WHEN x = 1 THEN 2 END", L"WHEN", Type::ControlFlow);
    single(LanguageSql, L"price DECIMAL(10, 2)", L"DECIMAL", Type::TypeName);
    single(LanguageSql, L"COUNT(*)", L"COUNT", Type::Function);
    single(LanguageSql, L"1.25e-3 + 7", L"1.25e-3", Type::Number);
    single(LanguageSql, L"'O''Brien' -- real comment", L"'O''Brien'", Type::String);
    single(LanguageSql, L"'O''Brien' -- real comment", L"-- real comment", Type::Comment);
    single(LanguageSql, L"[a]]b]", L"[a]]b]", Type::String);
    single(LanguageSql, L"SELECT 1 // 2", L"//", Type::Operator);
    SyntaxState sql;
    expect(LanguageSql, sql, L"/* outer /* inner */", L"/* outer /* inner */", Type::Comment);
    expect(LanguageSql, sql, L"still comment */ SELECT 1", L"still comment */", Type::Comment);
    expect(LanguageSql, sql, L"SELECT 'first", L"'first", Type::String);
    expect(LanguageSql, sql, L"second'; SELECT 2", L"second'", Type::String);
    expect(LanguageSql, sql, L"$tag$ -- literal", L"$tag$ -- literal", Type::String);
    expect(LanguageSql, sql, L"/* literal */$tag$ SELECT 3", L"SELECT", Type::Keyword);

    single(LanguagePowerShell, L"Get-ChildItem -Path .", L"Get-ChildItem", Type::Function);
    single(LanguagePowerShell, L"Get-ChildItem -Path .", L"-Path", Type::Keyword);
    single(LanguagePowerShell, L"$env:Path", L"$env:Path", Type::TypeName);
    single(LanguagePowerShell, L"${my value}", L"${my value}", Type::TypeName);
    single(LanguagePowerShell, L"$TRUE", L"$TRUE", Type::Keyword);
    single(LanguagePowerShell, L"[System.String] $s", L"[System.String]", Type::TypeName);
    single(LanguagePowerShell, L"IF ($true) {}", L"IF", Type::ControlFlow);
    single(LanguagePowerShell, L"'don''t'", L"'don''t'", Type::String);
    single(LanguagePowerShell, L"\"say `\"hi`\"\" # end", L"# end", Type::Comment);
    SyntaxState ps;
    expect(LanguagePowerShell, ps, L"<# outer <# inner #>", L"<# outer <# inner #>", Type::Comment);
    expect(LanguagePowerShell, ps, L"#> Get-Date", L"Get-Date", Type::Function);
    expect(LanguagePowerShell, ps, L"$s = @\"", L"@\"", Type::String);
    expect(LanguagePowerShell, ps, L"  \"@ # not a terminator", L"  \"@ # not a terminator", Type::String);
    expect(LanguagePowerShell, ps, L"\"@; Get-Date", L"Get-Date", Type::Function);
    expect(LanguagePowerShell, ps, L"'ordinary", L"'ordinary", Type::String);
    expect(LanguagePowerShell, ps, L"multiline'; Get-Date", L"multiline'", Type::String);

    single(LanguageJava, L"@Override", L"@Override", Type::TypeName);
    single(LanguageJava, L"int x = 0xFF + 1_000L;", L"int", Type::TypeName);
    single(LanguageJava, L"int x = 0xFF + 1_000L;", L"1_000L", Type::Number);
    single(LanguageJava, L"return greet(name);", L"greet", Type::Function);
    single(LanguageJava, L"RETURN", L"RETURN", Type::TypeName); // Java is case-sensitive.
    SyntaxState java;
    expect(LanguageJava, java, L"String text = \"\"\"", L"\"\"\"", Type::String);
    expect(LanguageJava, java, L"// not a comment", L"// not a comment", Type::String);
    expect(LanguageJava, java, L"\"\"\"; return text;", L"return", Type::ControlFlow);
    expect(LanguageJava, java, L"\"unterminated", L"\"unterminated", Type::String);
    expect(LanguageJava, java, L"return 1;", L"return", Type::ControlFlow);

    single(LanguagePhp, L"<?php echo strlen($name); ?>", L"<?php", Type::Keyword);
    single(LanguagePhp, L"<?php echo strlen($name); ?>", L"strlen", Type::Function);
    single(LanguagePhp, L"<?php echo strlen($name); ?>", L"$name", Type::TypeName);
    single(LanguagePhp, L"#[Example]", L"Example", Type::TypeName);
    single(LanguagePhp, L"function run(string $s)", L"string", Type::TypeName);
    SyntaxState php;
    expect(LanguagePhp, php, L"$s = <<<'TEXT'", L"<<<'TEXT'", Type::String);
    expect(LanguagePhp, php, L"TEXT_more # not a terminator", L"TEXT_more # not a terminator", Type::String);
    expect(LanguagePhp, php, L"  TEXT; echo 1;", L"echo", Type::Keyword);
    expect(LanguagePhp, php, L"\"multiline", L"\"multiline", Type::String);
    expect(LanguagePhp, php, L"string\"; return 1;", L"return", Type::ControlFlow);

    for (int language : {LanguageHtml, LanguageXml}) {
        single(language, L"<doc:item id=\"2\">&amp;</doc:item>", L"doc:item", Type::Keyword);
        single(language, L"<doc:item id=\"2\">&amp;</doc:item>", L"id", Type::TypeName);
        single(language, L"<doc:item id=\"2\">&amp;</doc:item>", L"\"2\"", Type::String);
        single(language, L"<doc:item id=\"2\">&amp;</doc:item>", L"&amp;", Type::Number);
        SyntaxState markup;
        expect(language, markup, L"<!-- comment", L"<!-- comment", Type::Comment);
        expect(language, markup, L"end --> <a title=\"first", L"\"first", Type::String);
        expect(language, markup, L"second\">text</a>", L"second\"", Type::String);
        expect(language, markup, L"<![CDATA[<raw>", L"<![CDATA[<raw>", Type::String);
        expect(language, markup, L"raw]]><b/>", L"b", Type::Keyword);
    }
    single(LanguageHtml, L"<script>if (a < b) run();</script><b/>", L"if (a < b) run();", Type::Plain);
    single(LanguageHtml, L"<script>if (a < b) run();</script><b/>", L"<b/>", Type::Keyword);

    single(LanguageCss, L".card { color: #ffaa00; }", L"color", Type::TypeName);
    single(LanguageCss, L".card { color: #ffaa00; }", L"#ffaa00", Type::Number);
    single(LanguageCss, L"a { padding: calc(1.5rem + 2px); }", L"calc", Type::Function);
    single(LanguageCss, L"a { padding: calc(1.5rem + 2px); }", L"1.5rem", Type::Number);
    single(LanguageCss, L"@media (min-width: 40rem)", L"@media", Type::Keyword);
    single(LanguageCss, L"a { background: url(https://host/#x); }", L"https://host/#x", Type::String);
    single(LanguageCss, L"a { background: url(\"https://host/#x\"); }", L"\"https://host/#x\"", Type::String);

    single(LanguageYaml, L"site: https://host/#part # comment", L"https://host/#part", Type::Plain);
    single(LanguageYaml, L"site: https://host/#part # comment", L"# comment", Type::Comment);
    single(LanguageYaml, L"enabled: true", L"enabled", Type::TypeName);
    single(LanguageYaml, L"enabled: true", L"true", Type::Keyword);
    single(LanguageYaml, L"count: -2.5e+3", L"-2.5e+3", Type::Number);
    single(LanguageYaml, L"copy: *defaults", L"*defaults", Type::TypeName);
    single(LanguageYaml, L"\"key\": 'don''t'", L"\"key\"", Type::TypeName);
    SyntaxState yaml;
    expect(LanguageYaml, yaml, L"message: |2- # header", L"# header", Type::Comment);
    expect(LanguageYaml, yaml, L"  # literal text", L"  # literal text", Type::String);
    expect(LanguageYaml, yaml, L"next: true", L"next", Type::TypeName);
    expect(LanguageYaml, yaml, L"quoted: 'first", L"'first", Type::String);
    expect(LanguageYaml, yaml, L"  second'", L"  second'", Type::String);
    expect(LanguageYaml, yaml, L"- message: |", L"|", Type::Keyword);
    expect(LanguageYaml, yaml, L"    scalar contents", L"    scalar contents", Type::String);
    expect(LanguageYaml, yaml, L"  sibling: true", L"sibling", Type::TypeName);
    expect(LanguageYaml, yaml, L"- message: |2", L"|2", Type::Keyword);
    expect(LanguageYaml, yaml, L"    scalar contents", L"    scalar contents", Type::String);
    expect(LanguageYaml, yaml, L"  sibling: true", L"sibling", Type::TypeName);
    expect(LanguageYaml, yaml, L"- |2", L"|2", Type::Keyword);
    expect(LanguageYaml, yaml, L"  sequence scalar", L"  sequence scalar", Type::String);
    expect(LanguageYaml, yaml, L"- true", L"true", Type::Keyword);

    single(LanguageMarkdown, L"## Heading", L"## Heading", Type::Keyword);
    single(LanguageMarkdown, L"**bold** and `code`", L"**bold**", Type::TypeName);
    single(LanguageMarkdown, L"**bold** and `code`", L"`code`", Type::String);
    single(LanguageMarkdown, L"[link](https://host/path_(one))", L"https://host/path_(one)", Type::String);
    single(LanguageMarkdown, L"1. Item", L"1.", Type::Keyword);
    SyntaxState md;
    expect(LanguageMarkdown, md, L"````sql", L"````sql", Type::Keyword);
    expect(LanguageMarkdown, md, L"```", L"```", Type::String);
    expect(LanguageMarkdown, md, L"# inside", L"# inside", Type::String);
    expect(LanguageMarkdown, md, L"````", L"````", Type::Keyword);
    expect(LanguageMarkdown, md, L"## outside", L"## outside", Type::Keyword);

    single(1, L"int sum(int x) { return x; }", L"int", Type::Keyword);
    single(2, L"def greet(name):", L"def", Type::Keyword);
    single(3, L"const count = 42;", L"const", Type::Keyword);
    single(4, L"fn main() {}", L"fn", Type::Keyword);
    single(5, L"func main() {}", L"func", Type::Keyword);
    single(6, L"if test -f file; then", L"if", Type::Keyword);
    single(7, L"if (true) return;", L"if", Type::ControlFlow);

    // Malformed fragments must always terminate and preserve UTF-16 source,
    // including when a prior line left the scanner inside a string/comment.
    std::mt19937 random(207);
    std::wstring alphabet = L"ab09 _-+/$'\"`#<>[]{}():;.!&*\\\t\u03bb\u4e2d";
    for (int language = LanguageSql; language <= LanguageMarkdown; ++language) {
        SyntaxState state;
        for (int n = 0; n < 1200; ++n) {
            if (n % 20 == 0) state = {};
            std::wstring line;
            size_t length = random() % 120;
            while (line.size() < length) line += alphabet[random() % alphabet.size()];
            lex(line, language, state);
        }
    }
}

bool sameColor(D2D1_COLOR_F a, D2D1_COLOR_F b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
void layoutTests(App& app) {
    // Native layout must use the existing seven configurable theme colours.
    struct Sample { const char* language; const char* text; Type type; };
    for (auto sample : {Sample{"sql", "SELECT", Type::Keyword}, {"ps", "Get-Date", Type::Function},
                       {"java", "return", Type::ControlFlow}, {"php", "string", Type::TypeName},
                       {"html", "<!-- comment -->", Type::Comment}, {"xml", "<![CDATA[text]]>", Type::String},
                       {"css", "1.5rem", Type::Number}, {"yml", "true", Type::Keyword},
                       {"md", "# Heading", Type::Keyword}, {"unknown-language", "SELECT 42;", Type::Plain}}) {
        std::string source = "```" + std::string(sample.language) + "\n" + sample.text + "\n```\n";
        app.root = app.parser.parse(source).root;
        layoutDocument(app);
        check(!app.layoutTextRuns.empty(), "native syntax sample creates text runs");
        for (const auto& run : app.layoutTextRuns)
            check(sameColor(run.color, getTokenColor(app.theme, sample.type)), "native layout uses requested theme category");
        check(app.codeBlocks.size() == 1 && app.codeBlocks[0].codeText == toWide(std::string(sample.text) + "\n"),
              "native highlighting preserves copy source");
    }
    // Unfinished constructs must not colour the next independent fence.
    for (auto sample : {Sample{"sql", "/* unfinished", Type::Keyword}, {"ps", "$s = @'", Type::Function},
                       {"yaml", "message: |\n  text", Type::Keyword}, {"html", "<!-- unfinished", Type::Keyword}}) {
        const char* next = std::string(sample.language) == "ps" ? "Get-Date" :
                           std::string(sample.language) == "yaml" ? "true" :
                           std::string(sample.language) == "html" ? "<b/>" : "SELECT";
        std::string source = "```" + std::string(sample.language) + "\n" + sample.text + "\n```\n\n```" + sample.language + "\n" + next + "\n```\n";
        app.root = app.parser.parse(source).root;
        layoutDocument(app);
        check(app.codeBlocks.size() == 2, "adjacent independent fences render");
        if (app.codeBlocks.size() == 2)
            for (const auto& run : app.layoutTextRuns)
                if (run.pos.y >= app.codeBlocks[1].bounds.top)
                    check(sameColor(run.color, getTokenColor(app.theme, sample.type)), "syntax state resets between fenced blocks");
    }

    for (const char* fixture : {"syntax-languages", "syntax-multiline", "syntax-legacy-control"}) {
        std::ifstream file(std::string(TINTA_SYNTAX_FIXTURES) + "/" + fixture + ".md", std::ios::binary);
        check(file.good(), "runnable syntax fixture is available");
        std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        auto doc = app.parser.parse(source);
        check(doc.success, "mixed fixture parses");
        std::map<qmd::ElementType, int> counts;
        std::vector<std::wstring> code;
        std::function<void(const qmd::ElementPtr&)> visit = [&](const qmd::ElementPtr& e) {
            ++counts[e->type];
            if (e->type == qmd::ElementType::CodeBlock) {
                std::string contents;
                for (const auto& child : e->children) contents += child->text;
                code.push_back(toWide(contents));
            }
            for (const auto& child : e->children) visit(child);
        };
        visit(doc.root);
        for (auto type : {qmd::ElementType::Heading, qmd::ElementType::Table, qmd::ElementType::ListItem,
                         qmd::ElementType::Strong, qmd::ElementType::Emphasis, qmd::ElementType::Link,
                         qmd::ElementType::BlockQuote, qmd::ElementType::MathInline, qmd::ElementType::MathDisplay})
            check(counts[type] > 0, "surrounding Markdown and math survive parsing");
        app.root = doc.root;
        layoutDocument(app);
        check(app.codeBlocks.size() == code.size(), "every mixed code block renders");
        float scale = app.contentScale * app.zoomFactor;
        for (size_t b = 0; b < code.size() && b < app.codeBlocks.size(); ++b) {
            const auto& block = app.codeBlocks[b];
            check(block.codeText == code[b], "all mixed code copy text is unchanged");
            check(app.docText.find(code[b]) != std::wstring::npos, "mixed code remains searchable/selectable");
            size_t rows = std::count(code[b].begin(), code[b].end(), L'\n');
            if (!code[b].empty() && code[b].back() != L'\n') ++rows;
            float height = (std::max(size_t(1), rows) * 20.0f + 24.0f) * scale;
            check(std::abs(block.bounds.bottom - block.bounds.top - height) < .1f, "syntax colours do not change row heights");
            if (b) check(block.bounds.top > app.codeBlocks[b - 1].bounds.bottom, "mixed code blocks do not overlap");
        }
        check(!app.tableRects.empty(), "mixed table still lays out");
        check(app.docText.find(L"Final syntax fixture paragraph") != std::wstring::npos, "content after fences renders");
        auto completeText = app.docText;
        auto completeBlocks = app.codeBlocks;
        std::vector<D2D1_COLOR_F> completeColors;
        for (const auto& run : app.layoutTextRuns) completeColors.push_back(run.color);
        layoutDocumentViewportFirst(app);
        ensureLayoutComplete(app);
        check(app.docText == completeText, "incremental layout preserves the complete document text");
        check(app.codeBlocks.size() == completeBlocks.size(), "incremental layout retains every code block");
        check(app.layoutTextRuns.size() == completeColors.size(), "incremental syntax run count matches full layout");
        for (size_t r = 0; r < app.layoutTextRuns.size() && r < completeColors.size(); ++r)
            check(sameColor(app.layoutTextRuns[r].color, completeColors[r]), "incremental and full syntax colours match");
    }
}
}

int main() {
    tokenTests();
    {
        auto app = std::make_unique<App>();
        if (!initD2D(*app)) return 2;
        app->height = 900;
        app->readingWidthPct = 100;
        for (int theme : {0, 5, -1}) {
            applyTheme(*app, theme < 0 ? 0 : theme);
            if (theme == -1) {
                app->theme.syntaxKeyword = D2D1::ColorF(.11f, .12f, .13f);
                app->theme.syntaxString = D2D1::ColorF(.21f, .22f, .23f);
                app->theme.syntaxComment = D2D1::ColorF(.31f, .32f, .33f);
                app->theme.syntaxNumber = D2D1::ColorF(.41f, .42f, .43f);
                app->theme.syntaxFunction = D2D1::ColorF(.51f, .52f, .53f);
                app->theme.syntaxType = D2D1::ColorF(.61f, .62f, .63f);
                app->theme.syntaxControlFlow = D2D1::ColorF(.71f, .72f, .73f);
            }
            for (float scale : {1.0f, 1.5f}) {
                app->contentScale = scale;
                updateTextFormats(*app);
                for (int width : {1050, 650}) {
                    app->width = width;
                    layoutTests(*app);
                }
            }
        }
    }
    CoUninitialize();
    std::cout << "Syntax highlighting: " << failures << " failures\n";
    return failures ? 1 : 0;
}
