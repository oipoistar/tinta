#include "document.h"
#include "wordcount.h"

#include <fstream>
#include <iostream>
#include <sstream>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

bool counts(std::wstring_view text, size_t words, size_t characters) {
    TextCounts result = countPlainText(text);
    return result.words == words && result.characters == characters;
}

TextCounts markdown(const std::string& source) {
    qmd::MarkdownParser parser;
    auto result = parseDocument(parser, source, std::string_view("note.md"));
    std::wstring visible;
    if (result.success) appendCountableText(result.root, visible);
    return countPlainText(visible);
}

void plainText() {
    check(counts(L"", 0, 0), "empty text has nothing to count");
    check(counts(L"hello world", 2, 11), "English words and characters with spaces");
    check(counts(L"don't stop \x2014 e-mail", 3, 19),
          "apostrophes and hyphens stay inside words, a lone dash is no word");
    check(counts(L"a\nb\r\nc", 3, 3), "line breaks separate words and are not characters");
    check(counts(L"\x4f60\x597d\xff0c\x4e16\x754c\x3002", 4, 6),
          "each Chinese character is a word, full-width punctuation only a character");
    check(counts(L"\x65e5\x672c\x8a9e\x30c6\x30ad\x30b9\x30c8", 7, 7),
          "kanji and katakana count one word each");
    check(counts(L"\xd55c\xad6d\xc5b4 \xb2e8\xc5b4", 2, 6), "Korean words are separated by spaces");
    check(counts(L"\x4e2d\x6587\x3000\x5b57", 3, 4), "the ideographic space is a space");
    check(counts(L"I \xd83d\xde00 it", 2, 6), "an emoji is one character and no word");
    check(counts(L"\xd840\xdc00", 1, 1), "an extension-B ideograph is one word and one character");
    check(counts(L"3.14 42 \xff11\xff12\xff13", 3, 11), "numbers, full-width digits included, are words");
    check(counts(L"mixed\x4e2d\x6587words", 4, 12), "CJK inside a Latin run splits it");
}

void markdownText() {
    TextCounts bold = markdown("**bold** and _italic_ text\n");
    check(bold.words == 4 && bold.characters == 20, "emphasis markers are not characters");
    TextCounts link = markdown("[link](https://example.com/a/very/long/path)\n");
    check(link.words == 1 && link.characters == 4, "a link counts its text, not its URL");
    TextCounts code = markdown("Use `printf` here.\n\n```c\nint main(void) { return 0; }\n```\n");
    check(code.words == 3, "inline code counts, code blocks do not");
    TextCounts front = markdown("---\ntitle: Many words in the title\n---\n\nBody text\n");
    check(front.words == 2, "front matter is not counted");
    TextCounts table = markdown("| a | b |\n| --- | --- |\n| c | d |\n");
    check(table.words == 4 && table.characters == 4, "table cells count, pipes and rules do not");
}

void fixture() {
    std::ifstream file(TINTA_COUNT_FIXTURE, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    check(!buffer.str().empty(), "the word count fixture loads");
    TextCounts total = markdown(buffer.str());
    if (total.words != 69) {
        std::cerr << "fixture words: " << total.words << '\n';
    }
    check(total.words == 69, "the mixed fixture counts 69 visible words");
}
}  // namespace

int main() {
    plainText();
    markdownText();
    fixture();
    std::cout << "Word count: " << failures << " failures\n";
    return failures ? 1 : 0;
}
