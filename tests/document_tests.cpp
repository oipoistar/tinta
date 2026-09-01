#include "document.h"

#include <functional>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    failures++;
}

} // namespace

int main() {
    check(isSupportedDocumentPath("notes.md"), ".md is supported");
    check(isSupportedDocumentPath("notes.MARKDOWN"), ".markdown is case-insensitive");
    check(isSupportedDocumentPath(L"diagram.MMD"), ".mmd is case-insensitive");
    check(isMermaidDocumentPath("diagram.mmd"), ".mmd is detected as Mermaid");
    check(!isMermaidDocumentPath("notes.md"), ".md is not detected as Mermaid");
    check(!isSupportedDocumentPath("diagram.mmdd"), "similar extensions are rejected");
    check(isSupportedDocumentPath("notes.txt"), ".txt opens as a plain-text document");
    check(isSupportedDocumentPath("data.JSON"), ".json is case-insensitive");
    check(isPlainTextDocumentPath(L"config.yaml"), ".yaml is plain text");
    check(!isPlainTextDocumentPath("notes.md"), ".md is not plain text");
    check(isSupportedDropPath(L"notes.txt"), "existing .txt drag-and-drop remains supported");

    qmd::MarkdownParser parser;
    auto mermaid = parseDocument(
        parser, "flowchart LR\nA --> B\n", "diagram.mmd");
    check(mermaid.success, "Mermaid document is created");
    check(mermaid.root && mermaid.root->children.size() == 1,
          "Mermaid document has one diagram element");
    if (mermaid.root && mermaid.root->children.size() == 1) {
        check(mermaid.root->children[0]->type == qmd::ElementType::MermaidDiagram,
              ".mmd content becomes a Mermaid diagram element");
    }

    // Obsidian/Typora inline extensions
    auto ext = parseDocument(parser,
        "before ==mark 中文== mid x^2^ and H~2~O ~~gone~~ `==not this==`\n", "notes.md");
    check(ext.success, "extension test parses");
    if (ext.success && !ext.root->children.empty()) {
        const auto& para = ext.root->children[0];
        int highlights = 0, sups = 0, subs = 0, codeIntact = 0, strikes = 0;
        for (const auto& child : para->children) {
            if (child->type == qmd::ElementType::Highlight) {
                highlights++;
                check(!child->children.empty() &&
                      child->children[0]->text == "mark \xe4\xb8\xad\xe6\x96\x87",
                      "highlight content preserved incl. CJK");
            }
            if (child->type == qmd::ElementType::Superscript) {
                sups++;
                check(!child->children.empty() && child->children[0]->text == "2",
                      "superscript content preserved");
            }
            if (child->type == qmd::ElementType::Subscript) subs++;
            if (child->type == qmd::ElementType::Strikethrough) {
                strikes++;
                check(!child->children.empty() && child->children[0]->text == "gone",
                      "strikethrough content preserved");
            }
            if (child->type == qmd::ElementType::Code) {
                codeIntact++;
                check(!child->children.empty() &&
                      child->children[0]->text == "==not this==",
                      "code spans are not transformed");
            }
        }
        check(highlights == 1, "one ==highlight== parsed");
        check(sups == 1, "one ^sup^ parsed");
        check(subs == 1, "one ~sub~ parsed");
        check(strikes == 1, "one ~~strike~~ parsed");
        check(codeIntact == 1, "inline code untouched");
    }

    // GitHub alerts
    auto alert = parseDocument(parser, "> [!NOTE]\n> Body text here.\n", "notes.md");
    check(alert.success, "alert blockquote parses");
    if (alert.success && !alert.root->children.empty()) {
        const auto& quote = alert.root->children[0];
        check(quote->type == qmd::ElementType::BlockQuote, "alert stays a blockquote");
        check(quote->alertKind == 1, "[!NOTE] detected as alert kind 1");
        check(!quote->children.empty() &&
              !quote->children[0]->children.empty() &&
              quote->children[0]->children[0]->text == "Body text here.",
              "alert marker stripped, body preserved");
    }
    auto caution = parseDocument(parser, "> [!caution]\n> Careful.\n", "notes.md");
    check(caution.success && !caution.root->children.empty() &&
          caution.root->children[0]->alertKind == 5,
          "alert markers match case-insensitively");
    auto notAlert = parseDocument(parser, "> [!NOTE] trailing words\n", "notes.md");
    check(notAlert.success && !notAlert.root->children.empty() &&
          notAlert.root->children[0]->alertKind == 0,
          "marker with trailing text on the same line stays a plain quote");

    // Inline <br> becomes a hard break (#45)
    auto br = parseDocument(parser, "line one<br>line two<BR/>line three<br />end\n", "notes.md");
    check(br.success, "inline br document parses");
    if (br.success && !br.root->children.empty()) {
        int hardBreaks = 0;
        int literalBr = 0;
        for (const auto& child : br.root->children[0]->children) {
            if (child->type == qmd::ElementType::HardBreak) hardBreaks++;
            if (child->type == qmd::ElementType::Text &&
                child->text.find("<br") != std::string::npos) literalBr++;
        }
        check(hardBreaks == 3, "all three <br> variants become hard breaks");
        check(literalBr == 0, "no literal <br> text remains");
    }

    // Exported documents from office/AI tools often wrap all visible content
    // in presentational <font> tags. They must not become literal table text.
    {
        auto styledTable = parseDocument(parser,
            "# 数智润德预案管理后台系统功能测试用例表\n"
            "**<font style=\"color:#000000;\">文档版本</font>**：V1.0\n\n"
            "| **<font style=\"color:rgb(0, 0, 0);\">序号</font>** | "
            "**<font style=\"color:rgb(0, 0, 0);\">功能项</font>** | "
            "**<font style=\"color:rgb(0, 0, 0);\">测试结果（正常/异常）</font>** | "
            "**<font style=\"color:rgb(0, 0, 0);\">异常描述</font>** |\n"
            "| --- | --- | --- | --- |\n"
            "| <font style=\"color:rgb(0, 0, 0);\">1</font> | "
            "<font style=\"color:#000000;\">登录：输入正确账号和密码。</font> | "
            "<font style=\"color:#000000;\">正常</font> |  |\n"
            "| <font style=\"color:rgb(0, 0, 0);\">2</font> | "
            "<font style=\"color:#000000;\">提交后检查异常描述。</font> | "
            "<font style=\"color:#000000;\">异常</font> | "
            "<font style=\"color:#000000;\">截图</font>![](https://cdn.example.test/error.png) |\n"
            "| <font style=\"color:rgb(0, 0, 0);\">3</font> | "
            "<font style=\"color:#000000;\">空结果占位。</font> | "
            "<font style=\"color:#000000;\">正常</font> | <br/> |\n",
            "数智润德预案管理后台系统功能测试用例表.md");
        check(styledTable.success, "font-wrapped Chinese table parses");
        bool sawTable = false;
        bool sawLiteralFont = false;
        bool sawHeader = false;
        bool sawImage = false;
        bool sawHardBreak = false;
        std::function<void(const qmd::ElementPtr&)> walk =
            [&](const qmd::ElementPtr& node) {
            if (node->type == qmd::ElementType::Table) sawTable = true;
            if (node->type == qmd::ElementType::Image) sawImage = true;
            if (node->type == qmd::ElementType::HardBreak) sawHardBreak = true;
            if (node->type == qmd::ElementType::Text) {
                if (node->text.find("<font") != std::string::npos ||
                    node->text.find("</font>") != std::string::npos) {
                    sawLiteralFont = true;
                }
                if (node->text == "序号") sawHeader = true;
            }
            for (const auto& child : node->children) walk(child);
        };
        if (styledTable.root) walk(styledTable.root);
        check(sawTable, "font-wrapped content remains a Markdown table");
        check(sawHeader, "font-wrapped table header text is preserved");
        check(sawImage, "remote images inside table cells are preserved");
        check(sawHardBreak, "HTML line breaks inside table cells are preserved");
        check(!sawLiteralFont, "font tags are hidden from rendered text");
    }

    // YAML frontmatter is hidden instead of rendering as a setext heading (#61)
    auto fm = parseDocument(parser,
        "---\ntitle: 'Barometer'\naliases:\ntags: [Barometer]\n---\n\n"
        "[Link](https://example.com)\n", "notes.md");
    check(fm.success, "frontmatter document parses");
    if (fm.success) {
        bool sawHeading = false, sawTitleText = false;
        std::function<void(const qmd::ElementPtr&)> walk = [&](const qmd::ElementPtr& e) {
            if (e->type == qmd::ElementType::Heading) sawHeading = true;
            if (e->type == qmd::ElementType::Text &&
                e->text.find("title:") != std::string::npos) sawTitleText = true;
            for (const auto& ch : e->children) walk(ch);
        };
        walk(fm.root);
        check(!sawHeading, "frontmatter does not become a heading");
        check(!sawTitleText, "frontmatter keys are not rendered");
        bool sawLink = false;
        std::function<void(const qmd::ElementPtr&)> findLink = [&](const qmd::ElementPtr& e) {
            if (e->type == qmd::ElementType::Link) sawLink = true;
            for (const auto& ch : e->children) findLink(ch);
        };
        findLink(fm.root);
        check(sawLink, "content after frontmatter renders");
    }
    auto unclosed = parseDocument(parser, "---\ntitle: x\nno closing fence\n", "notes.md");
    check(unclosed.success && !unclosed.root->children.empty(),
          "unclosed fence renders as ordinary markdown");
    auto hrDoc = parseDocument(parser, "para\n\n---\n\nafter\n", "notes.md");
    bool hrSurvives = false;
    if (hrDoc.success) {
        for (const auto& ch : hrDoc.root->children) {
            if (ch->type == qmd::ElementType::HorizontalRule) hrSurvives = true;
        }
    }
    check(hrSurvives, "a mid-document --- is still a horizontal rule");

    auto markdown = parseDocument(parser, "# Heading\n", "notes.md");
    check(markdown.success, "Markdown document still parses");
    check(markdown.root && !markdown.root->children.empty() &&
          markdown.root->children[0]->type == qmd::ElementType::Heading,
          ".md content keeps Markdown parsing");

    // Plain-text .md references become fileref: links (#127)
    {
        auto refs = parseDocument(parser,
            "See docs/auth.md and ./setup.md plus C:\\notes\\a.md here.\n"
            "Not these: `code.md` or https://x.com/y.md or file.mdx or "
            "bare .md or v2.md.bak\n"
            "Sentence ends with index.md.\n",
            "notes.md");
        check(refs.success, "fileref test parses");
        std::vector<std::string> found;
        std::function<void(const qmd::ElementPtr&)> walk =
            [&](const qmd::ElementPtr& node) {
            if (node->type == qmd::ElementType::Link &&
                node->url.rfind("fileref:", 0) == 0) {
                found.push_back(node->url.substr(8));
            }
            if (node->type != qmd::ElementType::Code &&
                node->type != qmd::ElementType::CodeBlock) {
                for (const auto& child : node->children) walk(child);
            }
        };
        if (refs.root) walk(refs.root);
        check(found.size() == 4, "exactly the four real references detected");
        auto has = [&](const char* token) {
            for (const auto& item : found) {
                if (item == token) return true;
            }
            return false;
        };
        check(has("docs/auth.md"), "bare relative path detected");
        check(has("./setup.md"), "dot-relative path detected");
        check(has("C:\\notes\\a.md"), "absolute path detected");
        check(has("index.md"), "sentence-ending path detected sans period");
        check(!has("code.md"), "code spans are left alone");
        check(!has("y.md"), "URLs are left alone");
        check(!has("file.mdx"), "longer extensions are not references");
        check(!has(".md"), "a bare extension is not a reference");
        check(!has("v2.md"), "dotted archive names are not references");
    }

    // Plain-text documents become one highlighted code block
    {
        auto json = parseDocument(
            parser, "{\n  \"key\": true\n}\n", "data.json");
        check(json.success, "json document parses");
        check(json.root && json.root->children.size() == 1,
              "json document is a single block");
        if (json.root && json.root->children.size() == 1) {
            const auto& block = json.root->children[0];
            check(block->type == qmd::ElementType::CodeBlock,
                  "json content becomes a code block");
            check(block->language == "json",
                  "json block carries its language tag");
            check(!block->children.empty() &&
                      block->children[0]->text.find("\"key\"") !=
                          std::string::npos,
                  "json content is preserved verbatim");
        }
        auto txt = parseDocument(parser, "# not a heading\n", "notes.txt");
        check(txt.success && txt.root && !txt.root->children.empty() &&
                  txt.root->children[0]->type ==
                      qmd::ElementType::CodeBlock,
              ".txt content stays literal instead of parsing as markdown");
    }

    // Text-file references are detected like .md ones (#7)
    {
        auto refs = parseDocument(parser,
            "Check config.yaml and data/out.json plus error.log here.\n",
            "notes.md");
        check(refs.success, "text-file ref test parses");
        std::vector<std::string> found;
        std::function<void(const qmd::ElementPtr&)> walk =
            [&](const qmd::ElementPtr& node) {
            if (node->type == qmd::ElementType::Link &&
                node->url.rfind("fileref:", 0) == 0) {
                found.push_back(node->url.substr(8));
            }
            if (node->type != qmd::ElementType::Code &&
                node->type != qmd::ElementType::CodeBlock) {
                for (const auto& child : node->children) walk(child);
            }
        };
        if (refs.root) walk(refs.root);
        check(found.size() == 3, "all three text-file references detected");
    }

    // Emoji shortcodes become their emoji, code stays literal
    {
        auto emoji = parseDocument(parser,
            "Ship it :rocket: with :+1: but `:tada:` stays and "
            ":not_a_real_code: survives\n"
            "```\n:fire: in a fence\n```\n",
            "notes.md");
        check(emoji.success, "emoji test parses");
        std::string flat;
        std::string codeFlat;
        std::function<void(const qmd::ElementPtr&)> walk =
            [&](const qmd::ElementPtr& node) {
            if (node->type == qmd::ElementType::Code ||
                node->type == qmd::ElementType::CodeBlock) {
                codeFlat += node->text;
                for (const auto& child : node->children)
                    codeFlat += child->text;
                return;
            }
            flat += node->text;
            for (const auto& child : node->children) walk(child);
        };
        if (emoji.root) walk(emoji.root);
        check(flat.find("\xF0\x9F\x9A\x80") != std::string::npos,
              ":rocket: replaced with the emoji");
        check(flat.find("\xF0\x9F\x91\x8D") != std::string::npos,
              ":+1: replaced with the emoji");
        check(flat.find(":rocket:") == std::string::npos,
              "no literal :rocket: remains");
        check(flat.find(":not_a_real_code:") != std::string::npos,
              "unknown shortcodes stay literal");
        check(codeFlat.find(":tada:") != std::string::npos,
              "inline code shortcodes untouched");
        check(codeFlat.find(":fire:") != std::string::npos,
              "fenced code shortcodes untouched");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All document tests passed\n";
    return 0;
}
