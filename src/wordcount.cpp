#include "wordcount.h"

#include <windows.h>

using namespace qmd;

namespace {

std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
    std::wstring wide(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), wide.data(), length);
    return wide;
}

// Han ideographs (with the extension blocks), hiragana and katakana: each
// one is a word, as in Word's count for Chinese and Japanese
bool isCjkWordChar(char32_t c) {
    return (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) ||
           (c >= 0xF900 && c <= 0xFAFF) || (c >= 0x20000 && c <= 0x3134F) ||
           (c >= 0x3041 && c <= 0x309F) ||
           (c >= 0x30A0 && c <= 0x30FF && c != 0x30FB) ||  // not the middle dot
           (c >= 0x31F0 && c <= 0x31FF);
}

bool isSpaceChar(char32_t c) {
    return c == L' ' || c == L'\t' || c == L'\v' || c == L'\f' || c == 0xA0 ||
           c == 0x1680 || (c >= 0x2000 && c <= 0x200B) || c == 0x202F ||
           c == 0x205F || c == 0x3000 || c == 0xFEFF;
}

bool isLetterOrDigit(char32_t c) {
    if (c > 0xFFFF) return false;
    wchar_t unit = static_cast<wchar_t>(c);
    WORD type = 0;
    return GetStringTypeW(CT_CTYPE1, &unit, 1, &type) && (type & (C1_ALPHA | C1_DIGIT));
}

void appendChildren(const ElementPtr& element, std::wstring& out) {
    for (const auto& child : element->children) appendCountableText(child, out);
}

}  // namespace

TextCounts countPlainText(std::wstring_view text) {
    TextCounts counts;
    bool inWord = false, wordHasLetter = false;
    auto endWord = [&] {
        if (inWord && wordHasLetter) ++counts.words;
        inWord = wordHasLetter = false;
    };
    for (size_t i = 0; i < text.size(); ++i) {
        char32_t c = text[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() &&
            text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            ++i;
        }
        if (c == L'\n' || c == L'\r') {
            endWord();
            continue;
        }
        ++counts.characters;
        if (isSpaceChar(c)) {
            endWord();
        } else if (isCjkWordChar(c)) {
            endWord();
            ++counts.words;
        } else {
            inWord = true;
            if (isLetterOrDigit(c)) wordHasLetter = true;
        }
    }
    endWord();
    return counts;
}

void appendCountableText(const ElementPtr& element, std::wstring& out) {
    if (!element) return;
    switch (element->type) {
        case ElementType::Text:
            out += widen(element->text);
            break;
        case ElementType::SoftBreak:
            out += L' ';
            break;
        case ElementType::HardBreak:
            out += L'\n';
            break;
        // Not prose: these keep their source out of the count
        case ElementType::CodeBlock:
        case ElementType::MermaidDiagram:
        case ElementType::MathInline:
        case ElementType::MathDisplay:
        case ElementType::Image:
        case ElementType::Properties:
        case ElementType::RubyText:
        case ElementType::FootnoteReference:
        case ElementType::FootnoteBacklink:
        case ElementType::HorizontalRule:
            break;
        // Blocks and table cells end with a break, so words never join
        // across them
        case ElementType::Paragraph:
        case ElementType::Heading:
        case ElementType::ListItem:
        case ElementType::TableCell:
        case ElementType::HtmlBlock:
            appendChildren(element, out);
            out += L'\n';
            break;
        default:
            appendChildren(element, out);
            break;
    }
}
