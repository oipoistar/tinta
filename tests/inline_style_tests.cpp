#include "inline_style.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    failures++;
}

// Sentinel format handles: emphasisFormat/inlineCodeFormat only ever select
// between them, so they are never dereferenced and DirectWrite is not needed
IDWriteTextFormat* fake(int id) {
    return reinterpret_cast<IDWriteTextFormat*>(static_cast<intptr_t>(id));
}

IDWriteTextFormat* const kBase   = fake(1);
IDWriteTextFormat* const kBold   = fake(2);
IDWriteTextFormat* const kItalic = fake(3);
IDWriteTextFormat* const kBoldIt = fake(4);
IDWriteTextFormat* const kSupSub = fake(5);
IDWriteTextFormat* const kCode   = fake(6);
IDWriteTextFormat* const kCodeB  = fake(7);
IDWriteTextFormat* const kCodeI  = fake(8);
IDWriteTextFormat* const kCodeBI = fake(9);

// App::~App releases every text format it holds, and the sentinels are not
// real COM objects, so they have to be gone before the App is
struct FormatGuard {
    App& app;
    explicit FormatGuard(App& a) : app(a) {
        app.boldFormat = kBold;
        app.italicFormat = kItalic;
        app.boldItalicFormat = kBoldIt;
        app.supSubFormat = kSupSub;
        app.codeFormat = kCode;
        app.codeBoldFormat = kCodeB;
        app.codeItalicFormat = kCodeI;
        app.codeBoldItalicFormat = kCodeBI;
    }
    ~FormatGuard() {
        app.boldFormat = nullptr;
        app.italicFormat = nullptr;
        app.boldItalicFormat = nullptr;
        app.supSubFormat = nullptr;
        app.codeFormat = nullptr;
        app.codeBoldFormat = nullptr;
        app.codeItalicFormat = nullptr;
        app.codeBoldItalicFormat = nullptr;
    }
};

// Flattens the first block's inline children the way layoutInlineContent does
std::vector<StyledRun> flattenFirstBlock(App& app, qmd::MarkdownParser& parser,
                                         const std::string& markdown) {
    std::vector<StyledRun> runs;
    auto result = parser.parse(markdown);
    if (!result.success || !result.root || result.root->children.empty()) {
        check(false, "markdown parses into a block");
        return runs;
    }
    InlineStyle root;
    root.format = kBase;
    flattenInline(app, result.root->children[0]->children, root, 20.0f, runs);
    return runs;
}

const StyledRun* firstOfType(const std::vector<StyledRun>& runs, ElementType type) {
    for (const auto& run : runs) {
        if (run.elem->type == type) return &run;
    }
    return nullptr;
}

// Text of a leaf: Text carries it directly, Code keeps it in its children
std::string textOf(const StyledRun& run) {
    if (run.elem->type == ElementType::Text) return run.elem->text;
    std::string text;
    for (const auto& child : run.elem->children) {
        if (child->type == ElementType::Text) text += child->text;
    }
    return text;
}

} // namespace

int main() {
    auto appPtr = std::make_unique<App>();
    App& app = *appPtr;
    FormatGuard formats(app);
    app.theme.link = D2D1::ColorF(0.2f, 0.5f, 0.9f);
    qmd::MarkdownParser parser;

    // A code span inside emphasis used to be dropped entirely: the renderer
    // only read direct Text children of a span
    {
        auto runs = flattenFirstBlock(app, parser, "**on branch `main`, not pushed**\n");
        check(runs.size() == 3, "bold span keeps text, code and text as separate runs");
        const StyledRun* code = firstOfType(runs, ElementType::Code);
        check(code != nullptr, "code inside bold survives flattening");
        if (code) {
            check(textOf(*code) == "main", "code content preserved");
            check(code->style.bold && !code->style.italic, "code inherits bold");
            check(inlineCodeFormat(app, code->style.bold, code->style.italic) == kCodeB,
                  "bold code uses the bold monospace face");
        }
        for (const auto& run : runs) {
            check(run.style.bold, "every run inside the bold span is bold");
            if (run.elem->type == ElementType::Text) {
                check(run.style.format == kBold, "bold text uses the bold face");
            }
        }
    }

    // Emphasis, and both nested together
    {
        auto runs = flattenFirstBlock(app, parser, "*italic `code` here*\n");
        const StyledRun* code = firstOfType(runs, ElementType::Code);
        check(code != nullptr && code->style.italic && !code->style.bold,
              "code inside emphasis inherits italic");
        if (code) {
            check(inlineCodeFormat(app, code->style.bold, code->style.italic) == kCodeI,
                  "italic code uses the italic monospace face");
        }
    }
    {
        auto runs = flattenFirstBlock(app, parser, "**bold *nested `code`* rest**\n");
        const StyledRun* code = firstOfType(runs, ElementType::Code);
        check(code != nullptr && code->style.bold && code->style.italic,
              "two levels of emphasis both reach the code span");
        if (code) {
            check(inlineCodeFormat(app, code->style.bold, code->style.italic) == kCodeBI,
                  "bold italic code uses the bold italic monospace face");
        }
        const StyledRun* text = firstOfType(runs, ElementType::Text);
        check(text != nullptr && text->style.format == kBold,
              "text directly inside the outer span stays bold only");
    }
    {
        auto runs = flattenFirstBlock(app, parser, "***both `code`***\n");
        const StyledRun* code = firstOfType(runs, ElementType::Code);
        check(code != nullptr && code->style.bold && code->style.italic,
              "*** applies bold and italic to the nested code");
        check(emphasisFormat(app, kBase, true, true) == kBoldIt,
              "bold italic text uses the bold italic face");
    }

    // Links carry their url and colour down to nested spans
    {
        auto runs = flattenFirstBlock(app, parser, "[link with `code`](https://example.com)\n");
        const StyledRun* code = firstOfType(runs, ElementType::Code);
        check(code != nullptr, "code inside a link survives flattening");
        if (code) {
            check(code->style.isLink && code->style.linkUrl == "https://example.com",
                  "code inside a link stays clickable");
        }
        const StyledRun* text = firstOfType(runs, ElementType::Text);
        check(text != nullptr && text->style.color.b == app.theme.link.b,
              "link text keeps the link colour");
    }
    {
        auto runs = flattenFirstBlock(app, parser, "[![alt](img.png)](https://example.com)\n");
        const StyledRun* image = firstOfType(runs, ElementType::Image);
        check(image != nullptr && image->style.isLink, "an image inside a link is reached");
    }

    // Obsidian/Typora spans
    {
        auto runs = flattenFirstBlock(app, parser, "**bold ~~gone~~ back**\n");
        bool struckAndBold = false;
        for (const auto& run : runs) {
            if (run.style.hasStrike) struckAndBold = run.style.bold;
        }
        check(struckAndBold, "strikethrough nested in bold keeps both");
    }
    {
        auto runs = flattenFirstBlock(app, parser, "**bold ==mark== back**\n");
        bool markedAndBold = false;
        for (const auto& run : runs) {
            if (run.style.hasBg && run.style.bold) markedAndBold = true;
        }
        check(markedAndBold, "highlight nested in bold keeps both");
    }
    {
        // Strikethrough and highlight around md4c's spans: each word keeps
        // its bold, italic, code or link style and takes the line or the
        // marker as well; no delimiter is left as text
        auto runs = flattenFirstBlock(app, parser,
            "~~**bold**~~ ==*italic*== ~~`code`~~ ==[link](https://example.com)== after\n");
        const StyledRun *bold = nullptr, *italic = nullptr, *link = nullptr;
        for (const auto& run : runs) {
            check(textOf(run).find("~~") == std::string::npos && textOf(run).find("==") == std::string::npos,
                  "delimiters around spans are consumed");
            if (textOf(run) == "bold") bold = &run;
            if (textOf(run) == "italic") italic = &run;
            if (textOf(run) == "link") link = &run;
        }
        check(bold && bold->style.bold && bold->style.format == kBold && bold->style.hasStrike &&
                  !bold->style.hasBg,
              "bold inside strikethrough is bold and struck");
        check(italic && italic->style.italic && italic->style.format == kItalic && italic->style.hasBg &&
                  !italic->style.hasStrike,
              "italic inside a highlight is italic and marked");
        const StyledRun* code = firstOfType(runs, ElementType::Code);
        check(code && code->style.hasStrike && textOf(*code) == "code", "code inside strikethrough is struck");
        check(link && link->style.hasBg && link->style.isLink && link->style.linkUrl == "https://example.com" &&
                  link->style.color.b == app.theme.link.b,
              "a link inside a highlight is marked and stays a link");
        check(!runs.empty() && textOf(runs.back()) == " after" && !runs.back().style.hasBg &&
                  !runs.back().style.hasStrike && !runs.back().style.bold && !runs.back().style.italic,
              "text after them is plain again");
    }
    {
        auto runs = flattenFirstBlock(app, parser, "x^2^ and H~2~O\n");
        const StyledRun* sup = nullptr;
        const StyledRun* sub = nullptr;
        for (const auto& run : runs) {
            if (run.style.format != kSupSub) continue;
            if (run.style.drawYOffset > 0.0f) sub = &run;
            else sup = &run;
        }
        check(sup != nullptr && sup->style.fixedSize, "superscript keeps its own size");
        check(sub != nullptr && sub->style.drawYOffset > 0.0f, "subscript is pushed down");
    }
    {
        // Scripts nested in an extension span get the small face and keep
        // the span's decoration; none of the inner delimiters stay literal
        auto runs = flattenFirstBlock(app, parser, "~~struck x^2^~~ and ==mark H~2~O== after\n");
        const StyledRun* struckSup = nullptr;
        const StyledRun* markedSub = nullptr;
        for (const auto& run : runs) {
            check(textOf(run).find_first_of("^~=") == std::string::npos,
                  "nested script delimiters are consumed");
            if (run.style.format != kSupSub) continue;
            if (run.style.hasStrike && run.style.drawYOffset == 0.0f) struckSup = &run;
            if (run.style.hasBg && run.style.drawYOffset > 0.0f) markedSub = &run;
        }
        check(struckSup && textOf(*struckSup) == "2" && !struckSup->style.hasBg,
              "superscript inside strikethrough is small and struck");
        check(markedSub && textOf(*markedSub) == "2" && !markedSub->style.hasStrike,
              "subscript inside a highlight is small, lowered and marked");
        check(!runs.empty() && !runs.back().style.hasBg && !runs.back().style.fixedSize,
              "text after the nested spans is back to plain body text");
    }
    {
        // The small face wins over emphasis inside it, instead of jumping
        // back up to body size
        auto runs = flattenFirstBlock(app, parser, "x^**2**^\n");
        const StyledRun* text = firstOfType(runs, ElementType::Text);
        check(text != nullptr, "superscript content is reached");
        for (const auto& run : runs) {
            if (run.style.fixedSize) {
                check(run.style.format == kSupSub, "bold inside superscript stays small");
            }
        }
    }

    // Math stays opaque to Markdown extensions while inheriting wrappers.
    {
        auto runs = flattenFirstBlock(app, parser,
            "==before $\\text{a==b}+x^2$ after== and ~~old $x_1$ result~~\n");
        size_t equations = 0;
        for (const auto& run : runs) {
            if (run.elem->type != ElementType::MathInline) continue;
            ++equations;
            if (equations == 1) {
                check(run.style.hasBg && !run.style.hasStrike, "highlight includes math");
                check(run.elem->text == "\\text{a==b}+x^2", "TeX delimiters remain literal within math");
            } else {
                check(run.style.hasStrike && !run.style.hasBg, "strikethrough includes math");
            }
        }
        check(equations == 2, "both styled equations remain math nodes");
        check(!runs.empty() && !runs.back().style.hasBg, "highlight does not leak into later text");
    }
    {
        auto runs = flattenFirstBlock(app, parser, "[$x^2$](https://example.com)\n");
        const auto* math = firstOfType(runs, ElementType::MathInline);
        check(math && math->style.isLink && math->style.linkUrl == "https://example.com",
              "math-only link retains its target");
    }
    {
        auto runs = flattenFirstBlock(app, parser, "==unclosed $x$ and `~~$y$~~`\n");
        for (const auto& run : runs)
            check(!run.style.hasBg && !run.style.hasStrike, "unclosed markers and literal code stay untouched");
    }

    // Leaves are emitted in document order, none dropped
    {
        auto runs = flattenFirstBlock(app, parser, "a **b `c` d** e\n");
        check(runs.size() == 5, "five leaves: text, text, code, text, text");
        if (runs.size() == 5) {
            check(textOf(runs[0]) == "a " && textOf(runs[2]) == "c" && textOf(runs[4]) == " e",
                  "leaves keep document order");
            check(!runs[0].style.bold && runs[2].style.bold && !runs[4].style.bold,
                  "style applies only inside the span it came from");
        }
    }

    if (failures == 0) {
        std::cout << "All inline style tests passed\n";
        return 0;
    }
    std::cerr << failures << " inline style test(s) failed\n";
    return 1;
}
