// Live text (live_text.h): the editing engine of Tinta's Live editor and of
// sheet cards. The text stays Markdown; the engine edits it the way
// Markdown reads, and draws it live.

// Its drawing is DirectWrite's and its edits are small: compiled for size
#pragma optimize("s", on)

#include "live_text.h"
#include "d2d_init.h"
#include "editor.h"
#include "i18n.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace live {

// --- Text --------------------------------------------------------------------

bool isWordChar(wchar_t c) { return iswalnum(c) || c == L'_' || c > 0x2E7F; }

size_t wordLeft(const std::wstring& t, size_t at) {
    at = std::min(at, t.size());
    while (at > 0 && !isWordChar(t[at - 1])) --at;
    while (at > 0 && isWordChar(t[at - 1])) --at;
    return at;
}

size_t wordRight(const std::wstring& t, size_t at) {
    while (at < t.size() && !isWordChar(t[at])) ++at;
    while (at < t.size() && isWordChar(t[at])) ++at;
    return at;
}

size_t lineStart(const std::wstring& t, size_t at) {
    at = std::min(at, t.size());
    size_t nl = at == 0 ? std::wstring::npos : t.rfind(L'\n', at - 1);
    return nl == std::wstring::npos ? 0 : nl + 1;
}

size_t lineEnd(const std::wstring& t, size_t at) {
    size_t nl = t.find(L'\n', at);
    return nl == std::wstring::npos ? t.size() : nl;
}

namespace {

std::wstring_view lineView(const std::wstring& t, size_t begin) {
    return std::wstring_view(t.data() + begin, lineEnd(t, begin) - begin);
}

bool blank(std::wstring_view s) { return s.find_first_not_of(L" \t") == std::wstring_view::npos; }

std::wstring lowered(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

// ``` or ~~~, three or more after at most three spaces: how many
size_t fenceRun(std::wstring_view line, wchar_t* ch = nullptr) {
    size_t i = 0;
    while (i < line.size() && i < 3 && line[i] == L' ') ++i;
    if (i >= line.size() || (line[i] != L'`' && line[i] != L'~')) return 0;
    wchar_t c = line[i];
    size_t n = 0;
    while (i + n < line.size() && line[i + n] == c) ++n;
    if (n < 3) return 0;
    if (ch) *ch = c;
    return n;
}

bool closesFence(std::wstring_view line, wchar_t ch, size_t run) {
    wchar_t c = 0;
    size_t n = fenceRun(line, &c);
    return n >= run && c == ch && blank(line.substr(line.find(ch) + n));
}

bool tableRow(std::wstring_view line) {
    size_t i = line.find_first_not_of(L' ');
    return i != std::wstring_view::npos && line[i] == L'|';
}

// The unescaped pipes of a table row, and the row's end when it has no last pipe
std::vector<size_t> cellBounds(std::wstring_view line) {
    std::vector<size_t> pipes;
    bool code = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == L'`') code = !code;
        if (line[i] == L'|' && !code && (i == 0 || line[i - 1] != L'\\')) pipes.push_back(i);
    }
    size_t last = line.find_last_not_of(L' ');
    if (!pipes.empty() && last != std::wstring_view::npos && pipes.back() != last) pipes.push_back(line.size());
    return pipes;
}

bool delimiterRow(std::wstring_view line) {
    return tableRow(line) && line.find(L'-') != std::wstring_view::npos &&
           line.find_first_not_of(L" |:-") == std::wstring_view::npos;
}

bool listKind(LineMark::Kind k) { return k == LineMark::Bullet || k == LineMark::Number || k == LineMark::Task; }

// Where a list item's text starts, past its indent and mark (a task's box is text)
size_t contentColumn(const LineMark& m) { return m.indent + (m.kind == LineMark::Task ? 2 : m.length); }

bool looksLikeAddress(const std::wstring& s) {
    if (s.empty() || s.find_first_of(L" \t\n") != std::wstring::npos) return false;
    std::wstring l = lowered(s.substr(0, 8));
    return l.rfind(L"http://", 0) == 0 || l.rfind(L"https://", 0) == 0 || l.rfind(L"mailto:", 0) == 0 ||
           l.rfind(L"www.", 0) == 0;
}

}  // namespace

int fenceState(const std::wstring& t, size_t begin) {
    wchar_t open = 0;
    size_t run = 0;
    for (size_t at = 0; at < begin;) {
        size_t end = lineEnd(t, at);
        std::wstring_view line(t.data() + at, end - at);
        if (!run) {
            run = fenceRun(line, &open);
        } else if (closesFence(line, open, run)) {
            run = 0;
        }
        at = end + 1;
    }
    std::wstring_view here = lineView(t, std::min(begin, t.size()));
    if (run) return closesFence(here, open, run) ? 3 : 2;
    return fenceRun(here) ? 1 : 0;
}

void fenceLines(const std::wstring& t, const std::vector<size_t>& starts, std::vector<char>& states) {
    states.assign(starts.size(), 0);
    wchar_t open = 0;
    size_t run = 0;
    for (size_t i = 0; i < starts.size(); ++i) {
        size_t end = i + 1 < starts.size() ? starts[i + 1] - 1 : t.size();
        std::wstring_view line(t.data() + starts[i], end - starts[i]);
        if (!run) {
            run = fenceRun(line, &open);
            states[i] = run ? 1 : 0;
        } else if (closesFence(line, open, run)) {
            states[i] = 3;
            run = 0;
        } else {
            states[i] = 2;
        }
    }
}

LineMark lineMark(const std::wstring& t, size_t begin) {
    LineMark m;
    std::wstring_view line = lineView(t, std::min(begin, t.size()));
    size_t i = 0;
    while (i < line.size() && line[i] == L' ') ++i;
    m.indent = i;
    auto at = [&](size_t k) { return i + k < line.size() ? line[i + k] : L'\0'; };
    wchar_t c = at(0);
    if (c == L'#') {
        size_t h = 0;
        while (at(h) == L'#') ++h;
        if (h <= 6 && at(h) == L' ') {
            m.kind = LineMark::Heading;
            m.number = (int)h;
            m.length = h + 1;
            m.symbol = L'#';
        }
    } else if (c == L'>') {
        m.kind = LineMark::Quote;
        m.symbol = L'>';
        m.length = at(1) == L' ' ? 2 : 1;
    } else if ((c == L'-' || c == L'*' || c == L'+') && at(1) == L' ') {
        m.symbol = c;
        wchar_t box = at(3);
        if (at(2) == L'[' && (box == L' ' || box == L'x' || box == L'X') && at(4) == L']' &&
            (at(5) == L' ' || at(5) == L'\0')) {
            m.kind = LineMark::Task;
            m.done = box != L' ';
            m.length = at(5) == L' ' ? 6 : 5;
        } else {
            m.kind = LineMark::Bullet;
            m.length = 2;
        }
    } else if (iswdigit(c)) {
        size_t d = 0;
        while (d < 9 && iswdigit(at(d))) ++d;
        if ((at(d) == L'.' || at(d) == L')') && (at(d + 1) == L' ' || at(d + 1) == L'\0')) {
            m.kind = LineMark::Number;
            m.number = _wtoi(std::wstring(line.substr(i, d)).c_str());
            m.symbol = at(d);
            m.length = at(d + 1) == L' ' ? d + 2 : d + 1;
        }
    }
    return m;
}

size_t quoteLength(const std::wstring& t, size_t ls) {
    size_t end = lineEnd(t, ls), i = ls;
    while (i < end && t[i] == L' ') ++i;
    size_t q = i;
    while (q < end && t[q] == L'>') {
        ++q;
        if (q < end && t[q] == L' ') ++q;
    }
    return q > i ? q - ls : 0;
}

LinkSpan linkAt(const std::wstring& t, size_t at) {
    const size_t npos = std::wstring_view::npos;
    at = std::min(at, t.size());
    size_t ls = lineStart(t, at);
    std::wstring_view line = lineView(t, ls);
    size_t col = at - ls;
    LinkSpan l;
    // [[name]] and [[name|words]]
    for (size_t open = line.find(L"[["); open != npos;) {
        size_t close = line.find(L"]]", open + 2);
        if (close == npos) break;
        if (col >= open && col <= close + 2) {
            size_t bar = std::min(line.find(L'|', open + 2), close);
            l.kind = LinkSpan::Wiki;
            l.from = ls + open;
            l.to = ls + close + 2;
            l.addressFrom = ls + open + 2;
            l.addressTo = ls + bar;
            l.textFrom = bar < close ? ls + bar + 1 : l.addressFrom;
            l.textTo = ls + close;
            return l;
        }
        open = line.find(L"[[", close + 2);
    }
    // [words](address) and ![words](picture); a title after the address is not part of it
    for (size_t open = line.find(L'['); open != npos; open = line.find(L'[', open + 1)) {
        size_t close = line.find(L']', open + 1);
        if (close == npos) break;
        if (close + 1 >= line.size() || line[close + 1] != L'(') continue;
        size_t end = line.find(L')', close + 2);
        if (end == npos) continue;
        bool image = open > 0 && line[open - 1] == L'!';
        size_t from = image ? open - 1 : open;
        if (col < from || col > end + 1) continue;
        size_t space = line.find(L' ', close + 2);
        l.kind = image ? LinkSpan::Image : LinkSpan::Inline;
        l.from = ls + from;
        l.to = ls + end + 1;
        l.textFrom = ls + open + 1;
        l.textTo = ls + close;
        l.addressFrom = ls + close + 2;
        l.addressTo = ls + std::min(space, end);
        return l;
    }
    // An address written out, without the punctuation that ends a sentence after it
    for (const wchar_t* scheme : {L"https://", L"http://"}) {
        for (size_t s = line.find(scheme); s != npos; s = line.find(scheme, s + 1)) {
            size_t e = line.find_first_of(L" )>\t", s);
            if (e == npos) e = line.size();
            while (e > s && std::wstring_view(L".,;:!?\"'").find(line[e - 1]) != npos) --e;
            if (col < s || col > e) continue;
            l.kind = LinkSpan::Address;
            l.from = l.textFrom = l.addressFrom = ls + s;
            l.to = l.textTo = l.addressTo = ls + e;
            return l;
        }
    }
    return l;
}

TableSpot tableAt(const std::wstring& t, size_t at) {
    const size_t npos = std::wstring_view::npos;
    TableSpot s;
    at = std::min(at, t.size());
    size_t ls = lineStart(t, at);
    if (!tableRow(lineView(t, ls)) || fenceState(t, ls) != 0) return s;
    size_t first = ls, last = ls;
    while (first > 0) {
        size_t above = lineStart(t, first - 1);
        if (!tableRow(lineView(t, above))) break;
        first = above;
    }
    for (size_t end = lineEnd(t, last); end < t.size() && tableRow(lineView(t, end + 1)); end = lineEnd(t, last)) last = end + 1;
    // A table's second row is its delimiter row
    size_t second = lineEnd(t, first) + 1;
    if (first == last || second > t.size() || !delimiterRow(lineView(t, second))) return s;
    s.in = true;
    s.first = first;
    s.last = last;
    s.row = ls;
    for (size_t r = first; r < ls; r = lineEnd(t, r) + 1) ++s.index;
    std::vector<size_t> header = cellBounds(lineView(t, first));
    s.columns = header.size() >= 2 ? (int)header.size() - 1 : 1;
    std::vector<size_t> bounds = cellBounds(lineView(t, ls));
    size_t col = at - ls;
    while (s.column + 2 < (int)bounds.size() && bounds[s.column + 1] < col) ++s.column;
    // The column's alignment, as its delimiter cell has it
    std::wstring_view delimiter = lineView(t, second);
    std::vector<size_t> cells = cellBounds(delimiter);
    if (s.column + 1 < (int)cells.size()) {
        std::wstring_view cell = delimiter.substr(cells[s.column] + 1, cells[s.column + 1] - cells[s.column] - 1);
        size_t a = cell.find_first_not_of(L' '), b = cell.find_last_not_of(L' ');
        if (a != npos) {
            bool left = cell[a] == L':', right = cell[b] == L':';
            s.align = left && right ? 2 : right ? 3 : left ? 1 : 0;
        }
    }
    return s;
}

bool fenceAround(const std::wstring& t, size_t at, size_t& from, size_t& to) {
    std::vector<size_t> starts{0};
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == L'\n') starts.push_back(i + 1);
    }
    std::vector<char> states;
    fenceLines(t, starts, states);
    size_t line = (size_t)(std::upper_bound(starts.begin(), starts.end(), std::min(at, t.size())) - starts.begin()) - 1;
    if (!states[line]) return false;
    size_t open = line;
    while (open > 0 && states[open] != 1) --open;
    size_t close = open + 1;
    while (close < starts.size() && states[close] == 2) ++close;
    from = open + 1 < starts.size() ? starts[open + 1] : t.size();
    to = close < starts.size() ? starts[close] - 1 : t.size();
    if (to < from) to = from;
    return true;
}

int blockAt(const std::wstring& t, size_t at) {
    size_t ls = lineStart(t, std::min(at, t.size()));
    if (fenceState(t, ls) != 0) return BLOCK_CODE;
    size_t q = quoteLength(t, ls);
    LineMark m = lineMark(t, ls + q);
    switch (m.kind) {
        case LineMark::Heading: return m.number <= 3 ? BLOCK_H1 + m.number - 1 : BLOCK_OTHER;
        case LineMark::Bullet: return BLOCK_BULLETS;
        case LineMark::Number: return BLOCK_NUMBERS;
        case LineMark::Task: return BLOCK_TASKS;
        default: return q ? BLOCK_QUOTE : BLOCK_TEXT;
    }
}

void inlineSpans(std::wstring_view line, size_t from, std::vector<InlineSpan>& spans) {
    const size_t npos = std::wstring_view::npos;
    auto add = [&](int kind, size_t at, size_t length) { spans.push_back({kind, at, length}); };
    for (size_t i = from; i < line.size(); ++i) {
        wchar_t c = line[i];
        if (c == L'\\' && i + 1 < line.size()) {
            add(INL_ESCAPE, i, 1);
            ++i;
        } else if (c == L'`') {
            // Code to the same run of backticks; with none, the rest is not read
            size_t ticks = 1;
            while (i + ticks < line.size() && line[i + ticks] == L'`') ++ticks;
            size_t close = line.find(std::wstring(ticks, L'`'), i + ticks);
            if (close == npos) break;
            add(INL_CODE_MARK, i, ticks);
            add(INL_CODE, i + ticks, close - i - ticks);
            add(INL_CODE_MARK, close, ticks);
            i = close + ticks - 1;
        } else if (c == L'$' && i + 1 < line.size() && line[i + 1] != L' ' && line[i + 1] != L'$') {
            size_t close = line.find(L'$', i + 1);
            if (close == npos || line[close - 1] == L' ') continue;
            add(INL_MATH_MARK, i, 1);
            add(INL_MATH, i + 1, close - i - 1);
            add(INL_MATH_MARK, close, 1);
            i = close;
        } else if (c == L'[' && i + 1 < line.size() && line[i + 1] == L'[') {
            // [[a note]] or [[a note|shown as]]
            size_t close = line.find(L"]]", i + 2);
            if (close == npos) continue;
            size_t bar = line.find(L'|', i + 2);
            size_t shown = bar != npos && bar < close ? bar + 1 : i + 2;
            add(INL_WIKI_MARK, i, shown - i);
            add(INL_WIKI, shown, close - shown);
            add(INL_WIKI_MARK, close, 2);
            i = close + 1;
        } else if (c == L'[' && i + 1 < line.size() && line[i + 1] == L'^') {
            size_t close = line.find(L']', i + 2);
            if (close == npos) continue;
            add(INL_FOOTNOTE, i, close - i + 1);
            i = close;
        } else if (c == L'[' || (c == L'!' && i + 1 < line.size() && line[i + 1] == L'[')) {
            // [text](address) and ![alt](address): "](address)" is one mark
            size_t open = c == L'!' ? i + 1 : i;
            size_t middle = line.find(L"](", open + 1);
            if (middle == npos) continue;
            size_t close = line.find(L')', middle + 2);
            if (close == npos) continue;
            int mark = c == L'!' ? INL_IMAGE_MARK : INL_LINK_MARK;
            add(mark, i, open - i + 1);
            add(c == L'!' ? INL_IMAGE : INL_LINK, open + 1, middle - open - 1);
            add(mark, middle, close - middle + 1);
            i = close;
        } else if ((c == L'*' || c == L'_') && i + 1 < line.size() && line[i + 1] == c) {
            std::wstring pair(2, c);
            size_t close = line.find(pair, i + 2);
            if (close == npos || close == i + 2) continue;
            add(INL_STRONG_MARK, i, 2);
            add(INL_STRONG, i + 2, close - i - 2);
            add(INL_STRONG_MARK, close, 2);
            i = close + 1;
        } else if ((c == L'*' || c == L'_') && i + 1 < line.size() && line[i + 1] != L' ' && (i == 0 || !iswalnum(line[i - 1]))) {
            size_t close = line.find(c, i + 1);
            if (close == npos || close == i + 1 || line[close - 1] == L' ') continue;
            add(INL_EM_MARK, i, 1);
            add(INL_EM, i + 1, close - i - 1);
            add(INL_EM_MARK, close, 1);
            i = close;
        } else if ((c == L'~' || c == L'=') && i + 1 < line.size() && line[i + 1] == c) {
            std::wstring pair(2, c);
            size_t close = line.find(pair, i + 2);
            if (close == npos || close == i + 2) continue;
            int mark = c == L'~' ? INL_STRIKE_MARK : INL_HIGHLIGHT_MARK;
            add(mark, i, 2);
            add(c == L'~' ? INL_STRIKE : INL_HIGHLIGHT, i + 2, close - i - 2);
            add(mark, close, 2);
            i = close + 1;
        } else if ((c == L'h' || c == L'w') && (i == 0 || line[i - 1] == L' ' || line[i - 1] == L'(') &&
                   (line.compare(i, 7, L"http://") == 0 || line.compare(i, 8, L"https://") == 0 || line.compare(i, 4, L"www.") == 0)) {
            size_t stop = i;
            while (stop < line.size() && line[stop] != L' ' && line[stop] != L')') ++stop;
            add(INL_ADDRESS, i, stop - i);
            i = stop - 1;
        }
    }
}

// --- Edits -------------------------------------------------------------------

void pushUndo(LiveText& e, bool coalesce) {
    unsigned long long now = GetTickCount64();
    bool goOn = coalesce && !e.undo.empty() && now - e.lastTypeMs < 900 && e.caret == e.coalesceAt && !e.hasSelection();
    e.lastTypeMs = now;
    if (goOn) return;
    LiveText::Step step;
    step.caretBefore = e.caret;
    step.anchorBefore = e.anchor;
    e.undo.push_back(std::move(step));
    // Steps keep only what they changed; a long session drops its oldest
    size_t kept = 0;
    for (const auto& s : e.undo) {
        for (const auto& p : s.pieces) kept += p.removed.size() + p.inserted.size();
    }
    while (e.undo.size() > 1 && (e.undo.size() > 500 || kept > (8u << 20))) {
        for (const auto& p : e.undo.front().pieces) kept -= p.removed.size() + p.inserted.size();
        e.undo.erase(e.undo.begin());
    }
    e.redo.clear();
}

void splice(LiveText& e, size_t at, size_t removed, const std::wstring& with) {
    at = std::min(at, e.text.size());
    removed = std::min(removed, e.text.size() - at);
    if (!removed && with.empty()) return;
    if (!e.undo.empty()) {
        auto& pieces = e.undo.back().pieces;
        // Typing on, or deleting back, grows the last piece
        if (!pieces.empty() && !removed && pieces.back().at + pieces.back().inserted.size() == at) {
            pieces.back().inserted += with;
        } else if (!pieces.empty() && with.empty() && pieces.back().inserted.empty() && at + removed == pieces.back().at) {
            pieces.back().removed.insert(0, e.text, at, removed);
            pieces.back().at = at;
        } else {
            pieces.push_back({at, e.text.substr(at, removed), with});
        }
    }
    e.text.replace(at, removed, with);
    e.changed = true;
    ++e.revision;
    if (e.trackChanges) e.changes.push_back({at, removed, with});
}

namespace {

size_t mapThrough(size_t pos, size_t at, size_t removed, size_t inserted) {
    if (pos <= at) return pos;
    if (pos >= at + removed) return pos - removed + inserted;
    return at + inserted;
}

// A splice that keeps the caret and the selection on the text they were on
void spliceKeep(LiveText& e, size_t at, size_t removed, const std::wstring& with) {
    e.caret = mapThrough(e.caret, at, removed, with.size());
    e.anchor = mapThrough(e.anchor, at, removed, with.size());
    splice(e, at, removed, with);
}

// A line's mark put on, swapped or taken off: the caret and the selection
// keep to the text after it
void spliceMark(LiveText& e, size_t at, size_t removed, const std::wstring& with) {
    auto map = [&](size_t p) {
        if (p < at) return p;
        if (p <= at + removed) return at + with.size();
        return p - removed + with.size();
    };
    e.caret = map(e.caret);
    e.anchor = map(e.anchor);
    splice(e, at, removed, with);
}

void settle(LiveText& e, size_t caret, size_t anchor) {
    e.caret = std::min(caret, e.text.size());
    e.anchor = std::min(anchor, e.text.size());
    e.preferredX = -1;
}

// The lines a selection covers: a selection ending at a line's start leaves that line out
void selectedLines(const LiveText& e, size_t& first, size_t& last) {
    size_t a = e.selStart(), b = e.selEnd();
    if (b > a && b == lineStart(e.text, b)) --b;
    first = lineStart(e.text, a);
    last = lineStart(e.text, b);
}

// Each line from `first` to `last`, last first, so earlier places hold
template <typename F>
void eachLineBack(LiveText& e, size_t first, size_t last, F f) {
    std::vector<size_t> starts;
    for (size_t at = first;; at = lineEnd(e.text, at) + 1) {
        starts.push_back(at);
        if (at >= last || lineEnd(e.text, at) >= e.text.size()) break;
    }
    for (auto it = starts.rbegin(); it != starts.rend(); ++it) f(*it);
}

// A numbered list counts on below `from`: the items at its indent follow on
void renumberFrom(LiveText& e, size_t from) {
    LineMark top = lineMark(e.text, from);
    if (top.kind != LineMark::Number) return;
    int next = top.number + 1;
    for (size_t at = lineEnd(e.text, from); at < e.text.size();) {
        ++at;
        LineMark m = lineMark(e.text, at);
        std::wstring_view line = lineView(e.text, at);
        if (blank(line)) break;
        if (m.indent > top.indent || (m.kind == LineMark::None && m.indent > 0)) {
            at = lineEnd(e.text, at);
            continue;
        }
        if (m.kind != LineMark::Number || m.indent != top.indent) break;
        if (m.number != next) {
            size_t digits = m.length - (m.length > 1 && line[m.indent + m.length - 1] == L' ' ? 2 : 1);
            spliceKeep(e, at + m.indent, digits, std::to_wstring(next));
        }
        ++next;
        at = lineEnd(e.text, at);
    }
}

}  // namespace

void replace(LiveText& e, const std::wstring& with, bool coalesce) {
    std::wstring insert = with;
    if (!e.multiline) {
        for (auto& c : insert) {
            if (c == L'\n' || c == L'\r' || c == L'\t') c = L' ';
        }
    }
    pushUndo(e, coalesce);
    size_t a = e.selStart(), b = e.selEnd();
    splice(e, a, b - a, insert);
    settle(e, a + insert.size(), a + insert.size());
    e.coalesceAt = e.caret;
}

namespace {

void wrapWith(LiveText& e, const std::wstring& open, const std::wstring& close) {
    size_t a = e.selStart(), b = e.selEnd();
    pushUndo(e, false);
    splice(e, b, 0, close);
    splice(e, a, 0, open);
    settle(e, b + open.size(), a + open.size());
}

}  // namespace

void typeCharacter(LiveText& e, wchar_t ch) {
    // A selection on one line is wrapped by a pair
    if (e.multiline && e.hasSelection() && e.text.find(L'\n', e.selStart()) >= e.selEnd()) {
        const wchar_t* open = nullptr;
        const wchar_t* close = nullptr;
        switch (ch) {
            case L'*': open = close = L"*"; break;
            case L'_': open = close = L"_"; break;
            case L'`': open = close = L"`"; break;
            case L'"': open = close = L"\""; break;
            case L'~': open = close = L"~~"; break;
            case L'=': open = close = L"=="; break;
            case L'[': open = L"["; close = L"]"; break;
            case L'(': open = L"("; close = L")"; break;
            default: break;
        }
        if (open) {
            wrapWith(e, open, close);
            return;
        }
    }
    // A space after a word ends an undo step: undo takes typing back a word at a time
    bool coalesce = !(ch == L' ' && e.caret > 0 && e.text[e.caret - 1] != L' ');
    replace(e, std::wstring(1, ch), coalesce);
}

void newline(LiveText& e, bool plain) {
    if (!e.multiline) return;
    size_t a = e.selStart(), b = e.selEnd();
    size_t ls = lineStart(e.text, a);
    std::wstring_view line = lineView(e.text, ls);
    size_t lead = 0;
    while (lead < line.size() && line[lead] == L' ') ++lead;
    std::wstring pad(lead, L' ');
    size_t le = ls + line.size();
    int fence = fenceState(e.text, ls);
    if (fence == 2 || fence == 3) {
        replace(e, L"\n" + pad);
        return;
    }
    if (fence == 1) {
        // An opening fence that nothing closes gets its closing one
        wchar_t ch = 0;
        size_t run = fenceRun(line, &ch);
        size_t after = 0;
        for (size_t at = le; at < e.text.size(); at = lineEnd(e.text, at)) {
            ++at;
            wchar_t c = 0;
            if (fenceRun(lineView(e.text, at), &c) && c == ch) ++after;
        }
        if (after % 2 == 0 && b == le) {
            pushUndo(e, false);
            splice(e, a, b - a, L"\n" + pad + L"\n" + pad + std::wstring(run, ch));
            settle(e, a + 1 + pad.size(), a + 1 + pad.size());
            return;
        }
        replace(e, L"\n" + pad);
        return;
    }
    if (!plain && tableRow(line) && b == le) {
        std::vector<size_t> bounds = cellBounds(line);
        bool empty = true;
        for (size_t k = 0; k + 1 < bounds.size(); ++k) {
            if (!blank(line.substr(bounds[k] + 1, bounds[k + 1] - bounds[k] - 1))) empty = false;
        }
        size_t above = ls > 0 ? lineStart(e.text, ls - 1) : ls;
        size_t below = le < e.text.size() ? le + 1 : std::wstring::npos;
        bool rowBelow = below != std::wstring::npos && tableRow(lineView(e.text, below));
        if (empty && !rowBelow && above < ls && tableRow(lineView(e.text, above))) {
            // An empty last row ends the table
            pushUndo(e, false);
            splice(e, ls, le - ls, L"\n");
            settle(e, ls + 1, ls + 1);
            return;
        }
        if (bounds.size() >= 2) {
            // Under the header, the new row goes past the delimiter row
            size_t at = rowBelow && delimiterRow(lineView(e.text, below)) ? lineEnd(e.text, below) : le;
            std::wstring row = pad + L"|";
            for (size_t k = 0; k + 1 < bounds.size(); ++k) row += L"  |";
            pushUndo(e, false);
            if (b > a) {
                splice(e, a, b - a, L"");
                at -= b - a;
            }
            splice(e, at, 0, L"\n" + row);
            settle(e, at + pad.size() + 3, at + pad.size() + 3);
            return;
        }
    }
    LineMark m = lineMark(e.text, ls);
    bool item = listKind(m.kind) || m.kind == LineMark::Quote;
    size_t content = ls + m.indent + m.length;
    if (!item || a < content) {
        replace(e, a <= ls + lead ? std::wstring(L"\n") : L"\n" + pad);
        return;
    }
    if (plain) {
        // A line of the same item: under its text, or the quote goes on
        replace(e, m.kind == LineMark::Quote ? L"\n" + pad + L"> " : L"\n" + std::wstring(contentColumn(m), L' '));
        return;
    }
    if (blank(line.substr(m.indent + m.length)) && b == le) {
        if (m.kind != LineMark::Quote && m.indent > 0) {
            indent(e, true);
            return;
        }
        // An empty item ends the list, a blank line after it
        pushUndo(e, false);
        splice(e, ls, le - ls, L"\n");
        settle(e, ls + 1, ls + 1);
        return;
    }
    std::wstring next;
    switch (m.kind) {
        case LineMark::Task: next = std::wstring(1, m.symbol) + L" [ ] "; break;
        case LineMark::Bullet: next = std::wstring(1, m.symbol) + L" "; break;
        case LineMark::Number: next = std::to_wstring(m.number + 1) + m.symbol + L" "; break;
        default: next = m.length == 2 ? L"> " : L">"; break;
    }
    replace(e, L"\n" + pad + next);
    if (m.kind == LineMark::Number) renumberFrom(e, lineStart(e.text, e.caret));
}

void backspace(LiveText& e, bool word) {
    if (e.hasSelection()) {
        replace(e, L"");
        return;
    }
    if (e.caret == 0) return;
    if (e.multiline && !word) {
        // A line's mark goes first, all of it
        size_t ls = lineStart(e.text, e.caret);
        LineMark m = lineMark(e.text, ls);
        if (m.kind != LineMark::None && e.caret == ls + m.indent + m.length && fenceState(e.text, ls) == 0) {
            pushUndo(e, false);
            splice(e, ls + m.indent, m.length, L"");
            settle(e, ls + m.indent, ls + m.indent);
            return;
        }
    }
    size_t from = word ? wordLeft(e.text, e.caret) : e.caret - 1;
    if (!word && from > 0 && IS_LOW_SURROGATE(e.text[from]) && IS_HIGH_SURROGATE(e.text[from - 1])) --from;
    pushUndo(e, !word);
    splice(e, from, e.caret - from, L"");
    settle(e, from, from);
    e.coalesceAt = from;
}

void deleteForward(LiveText& e, bool word) {
    if (e.hasSelection()) {
        replace(e, L"");
        return;
    }
    if (e.caret >= e.text.size()) return;
    size_t to = word ? wordRight(e.text, e.caret) : e.caret + 1;
    if (!word && to < e.text.size() && IS_LOW_SURROGATE(e.text[to])) ++to;
    // Joining the next line takes its mark with the break
    if (e.multiline && !word && e.text[e.caret] == L'\n') {
        LineMark m = lineMark(e.text, e.caret + 1);
        if (m.kind != LineMark::None && fenceState(e.text, e.caret + 1) == 0) to = e.caret + 1 + m.indent + m.length;
    }
    pushUndo(e, !word);
    splice(e, e.caret, to - e.caret, L"");
    settle(e, e.caret, e.caret);
    e.coalesceAt = e.caret;
}

void indent(LiveText& e, bool out) {
    size_t first = 0, last = 0;
    selectedLines(e, first, last);
    LineMark top = lineMark(e.text, first);
    size_t step = fenceState(e.text, first) == 2 ? 4 : 2;
    if (listKind(top.kind)) {
        // A list item goes under the item above, or back out to its parent
        size_t parent = 0, above = std::wstring::npos;
        for (size_t at = first; at > 0;) {
            at = lineStart(e.text, at - 1);
            std::wstring_view line = lineView(e.text, at);
            if (blank(line)) continue;
            LineMark p = lineMark(e.text, at);
            if (!listKind(p.kind)) {
                if (p.indent == 0) break;
                continue;
            }
            if (above == std::wstring::npos) above = at;
            if (p.indent < top.indent) {
                parent = p.indent;
                break;
            }
            if (!at) break;
        }
        if (out) {
            step = top.indent - std::min(top.indent, parent);
        } else if (above != std::wstring::npos) {
            LineMark p = lineMark(e.text, above);
            size_t want = p.indent + (p.kind == LineMark::Number ? p.length : 2);
            step = want > top.indent ? want - top.indent : 0;
        } else {
            step = 0;  // the first item of a list cannot go under anything
        }
        if (!step) return;
    }
    pushUndo(e, false);
    eachLineBack(e, first, last, [&](size_t at) {
        std::wstring_view line = lineView(e.text, at);
        if (out) {
            size_t spaces = 0;
            while (spaces < step && spaces < line.size() && line[spaces] == L' ') ++spaces;
            if (spaces) spliceKeep(e, at, spaces, L"");
        } else if (!blank(line) || first == last) {
            spliceKeep(e, at, 0, std::wstring(step, L' '));
        }
    });
    e.preferredX = -1;
}

namespace {

void selectCell(LiveText& e, size_t rowStart, const std::vector<size_t>& bounds, size_t k) {
    std::wstring_view line = lineView(e.text, rowStart);
    size_t from = bounds[k] + 1, to = bounds[k + 1];
    size_t a = from, b = to;
    while (a < b && line[a] == L' ') ++a;
    while (b > a && line[b - 1] == L' ') --b;
    if (a == b) a = b = std::min(from + 1, to);
    settle(e, rowStart + b, rowStart + a);
}

// Tab in a table: the next cell, the next row, or a new row after the last
void moveCell(LiveText& e, bool back) {
    size_t ls = lineStart(e.text, e.caret);
    std::vector<size_t> bounds = cellBounds(lineView(e.text, ls));
    if (bounds.size() < 2) return;
    size_t col = e.caret - ls;
    size_t k = 0;
    while (k + 1 < bounds.size() && bounds[k + 1] < col) ++k;
    if (k + 1 >= bounds.size()) k = bounds.size() - 2;
    if (!back && k + 2 < bounds.size()) {
        selectCell(e, ls, bounds, k + 1);
        return;
    }
    if (back && k > 0) {
        selectCell(e, ls, bounds, k - 1);
        return;
    }
    // The row below or above, past the delimiter row
    size_t at = ls;
    for (;;) {
        if (back) {
            if (!at) return;
            at = lineStart(e.text, at - 1);
        } else {
            size_t end = lineEnd(e.text, at);
            if (end >= e.text.size()) {
                at = std::wstring::npos;
                break;
            }
            at = end + 1;
        }
        std::wstring_view line = lineView(e.text, at);
        if (!tableRow(line)) {
            if (back) return;
            at = std::wstring::npos;
            break;
        }
        if (!delimiterRow(line)) break;
    }
    if (at == std::wstring::npos) {
        // After the last cell: a new row under the table's last
        size_t lastRow = ls;
        for (size_t end = lineEnd(e.text, ls); end < e.text.size() && tableRow(lineView(e.text, end + 1));
             end = lineEnd(e.text, end + 1)) {
            lastRow = end + 1;
        }
        size_t le = lineEnd(e.text, lastRow);
        size_t lead = lineView(e.text, ls).find_first_not_of(L' ');
        std::wstring row = std::wstring(lead == std::wstring::npos ? 0 : lead, L' ') + L"|";
        for (size_t c = 0; c + 1 < bounds.size(); ++c) row += L"  |";
        pushUndo(e, false);
        splice(e, le, 0, L"\n" + row);
        at = le + 1;
    }
    std::vector<size_t> next = cellBounds(lineView(e.text, at));
    if (next.size() >= 2) selectCell(e, at, next, back ? next.size() - 2 : 0);
}

// Tab in a link's text goes to its address
bool linkAddress(LiveText& e) {
    size_t ls = lineStart(e.text, e.caret);
    std::wstring_view line = lineView(e.text, ls);
    size_t col = e.caret - ls;
    size_t close = line.find(L']', col);
    if (close == std::wstring_view::npos || close + 1 >= line.size() || line[close + 1] != L'(') return false;
    size_t open = col ? line.rfind(L'[', col - 1) : std::wstring_view::npos;
    if (open == std::wstring_view::npos || line.substr(open, col - open).find(L']') != std::wstring_view::npos) return false;
    size_t from = close + 2;
    size_t to = line.find(L')', from);
    if (to == std::wstring_view::npos) to = from;
    settle(e, ls + to, ls + from);
    return true;
}

}  // namespace

void tab(LiveText& e, bool back) {
    if (!e.multiline) return;
    size_t ls = lineStart(e.text, e.caret);
    std::wstring_view line = lineView(e.text, ls);
    // In code, four spaces, whatever the line looks like
    if (fenceState(e.text, ls) == 2) {
        if (!back && !e.hasSelection()) replace(e, L"    ");
        else indent(e, back);
        return;
    }
    if (tableRow(line) && lineStart(e.text, e.anchor) == ls) {
        moveCell(e, back);
        return;
    }
    if (!back && !e.hasSelection() && linkAddress(e)) return;
    LineMark m = lineMark(e.text, ls);
    if (e.hasSelection() || back || listKind(m.kind)) {
        indent(e, back);
        return;
    }
    replace(e, L"  ");
}

bool undo(LiveText& e, bool redo) {
    auto& from = redo ? e.redo : e.undo;
    auto& to = redo ? e.undo : e.redo;
    if (from.empty()) return false;
    LiveText::Step step = std::move(from.back());
    from.pop_back();
    if (!redo) {
        step.caretAfter = e.caret;
        step.anchorAfter = e.anchor;
    }
    // Undone last piece first; redone in order
    auto apply = [&](const LiveText::Piece& p, bool back) {
        const std::wstring& out = back ? p.inserted : p.removed;
        const std::wstring& in = back ? p.removed : p.inserted;
        e.text.replace(p.at, out.size(), in);
        e.changed = true;
        ++e.revision;
        if (e.trackChanges) e.changes.push_back({p.at, out.size(), in});
    };
    if (!redo) {
        for (auto it = step.pieces.rbegin(); it != step.pieces.rend(); ++it) apply(*it, true);
        settle(e, step.caretBefore, step.anchorBefore);
    } else {
        for (const auto& p : step.pieces) apply(p, false);
        settle(e, step.caretAfter, step.anchorAfter);
    }
    to.push_back(std::move(step));
    e.lastTypeMs = 0;
    return true;
}

namespace {

size_t runBefore(const std::wstring& t, size_t at, wchar_t ch) {
    size_t n = 0;
    while (at > n && t[at - n - 1] == ch) ++n;
    return n;
}

size_t runAfter(const std::wstring& t, size_t at, wchar_t ch) {
    size_t n = 0;
    while (at + n < t.size() && t[at + n] == ch) ++n;
    return n;
}

// Whether a run of a mark's character holds the mark: * is italic in a
// run of one or three, ** bold in two or more
bool runHolds(size_t run, const std::wstring& mark) {
    if (mark == L"*" || mark == L"_") return run % 2 == 1;
    return run >= mark.size();
}

}  // namespace

void toggleWrap(LiveText& e, const std::wstring& mark) {
    if (mark.empty()) return;
    size_t a = e.selStart(), b = e.selEnd();
    size_t m = mark.size();
    wchar_t ch = mark[0];
    bool same = mark.find_first_not_of(ch) == std::wstring::npos;
    size_t caretIn = std::wstring::npos;
    if (a == b) {
        // The word at the caret, or an empty pair to type into
        size_t l = a, r = a;
        while (l > 0 && isWordChar(e.text[l - 1])) --l;
        while (r < e.text.size() && isWordChar(e.text[r])) ++r;
        if (l == r) {
            if (a >= m && e.text.compare(a - m, m, mark) == 0 && e.text.compare(a, m, mark) == 0) {
                pushUndo(e, false);
                splice(e, a - m, 2 * m, L"");
                settle(e, a - m, a - m);
                return;
            }
            pushUndo(e, false);
            splice(e, a, 0, mark + mark);
            settle(e, a + m, a + m);
            return;
        }
        caretIn = a - l;
        a = l;
        b = r;
    }
    auto outside = [&] {
        if (a < m || e.text.compare(a - m, m, mark) != 0 || e.text.compare(b, m, mark) != 0) return false;
        return !same || (runHolds(runBefore(e.text, a, ch), mark) && runHolds(runAfter(e.text, b, ch), mark));
    };
    auto inside = [&] {
        if (b - a < 2 * m || e.text.compare(a, m, mark) != 0 || e.text.compare(b - m, m, mark) != 0) return false;
        return !same || (runHolds(runAfter(e.text, a, ch), mark) && runHolds(runBefore(e.text, b, ch), mark));
    };
    pushUndo(e, false);
    size_t s = a, t = b;
    if (outside()) {
        splice(e, b, m, L"");
        splice(e, a - m, m, L"");
        s = a - m;
        t = b - m;
    } else if (inside()) {
        splice(e, b - m, m, L"");
        splice(e, a, m, L"");
        t = b - 2 * m;
    } else {
        splice(e, b, 0, mark);
        splice(e, a, 0, mark);
        s = a + m;
        t = b + m;
    }
    if (caretIn != std::wstring::npos) settle(e, s + std::min(caretIn, t - s), s + std::min(caretIn, t - s));
    else settle(e, t, s);
}

void makeLink(LiveText& e) {
    size_t a = e.selStart(), b = e.selEnd();
    std::wstring sel = e.text.substr(a, b - a);
    if (sel.find(L'\n') != std::wstring::npos) return;
    pushUndo(e, false);
    if (looksLikeAddress(sel)) {
        // An address becomes the link's target; its text is typed next
        splice(e, a, b - a, L"[](" + sel + L")");
        settle(e, a + 1, a + 1);
    } else if (!sel.empty()) {
        splice(e, b, 0, L"]()");
        splice(e, a, 0, L"[");
        settle(e, b + 3, b + 3);
    } else {
        splice(e, a, 0, L"[]()");
        settle(e, a + 1, a + 1);
    }
}

void setHeading(LiveText& e, int level) {
    size_t first = 0, last = 0;
    selectedLines(e, first, last);
    bool all = true;
    eachLineBack(e, first, last, [&](size_t at) {
        LineMark m = lineMark(e.text, at);
        if (m.kind != LineMark::Heading || m.number != level) all = false;
    });
    int want = all ? 0 : level;
    pushUndo(e, false);
    eachLineBack(e, first, last, [&](size_t at) {
        LineMark m = lineMark(e.text, at);
        if (blank(lineView(e.text, at)) && first != last) return;
        std::wstring mark = want ? std::wstring((size_t)want, L'#') + L" " : std::wstring();
        // A list or quote mark gives way to the heading's
        size_t removed = m.kind == LineMark::None ? 0 : m.length;
        if (!want && m.kind != LineMark::Heading) removed = 0;
        spliceMark(e, at + m.indent, removed, mark);
    });
    e.preferredX = -1;
}

void toggleLineMark(LiveText& e, LineMark::Kind kind) {
    size_t first = 0, last = 0;
    selectedLines(e, first, last);
    const bool quote = kind == LineMark::Quote;
    // A quote goes around what a line holds, a list mark inside its quote
    auto markAt = [&](size_t ls) { return quote ? ls : ls + quoteLength(e.text, ls); };
    bool all = true;
    eachLineBack(e, first, last, [&](size_t at) {
        if (blank(lineView(e.text, at)) && first != last) return;
        if (quote ? quoteLength(e.text, at) == 0 : lineMark(e.text, markAt(at)).kind != kind) all = false;
    });
    // Marks are worked out top down (numbers count on) and written bottom up
    std::vector<std::pair<size_t, std::wstring>> marks;
    int number = 1;
    for (size_t at = first;; at = lineEnd(e.text, at) + 1) {
        if (!(blank(lineView(e.text, at)) && first != last)) {
            switch (kind) {
                case LineMark::Bullet: marks.push_back({at, L"- "}); break;
                case LineMark::Number: marks.push_back({at, std::to_wstring(number++) + L". "}); break;
                case LineMark::Task: marks.push_back({at, L"- [ ] "}); break;
                default: marks.push_back({at, L"> "}); break;
            }
        }
        if (at >= last || lineEnd(e.text, at) >= e.text.size()) break;
    }
    pushUndo(e, false);
    for (auto it = marks.rbegin(); it != marks.rend(); ++it) {
        size_t at = markAt(it->first);
        LineMark m = lineMark(e.text, at);
        if (quote) {
            // In, or a level out: the first > and the space after it
            size_t mark = it->first + m.indent;
            if (!all) spliceMark(e, mark, 0, it->second);
            else spliceMark(e, mark, mark + 1 < e.text.size() && e.text[mark + 1] == L' ' ? 2 : 1, L"");
            continue;
        }
        // A task stays a task when it turns into a list of tasks
        if (kind == LineMark::Task && m.kind == LineMark::Task && !all) continue;
        spliceMark(e, at + m.indent, listKind(m.kind) ? m.length : 0, all ? std::wstring() : it->second);
    }
    e.preferredX = -1;
}

void toggleTask(LiveText& e, size_t lineBegin) {
    LineMark m = lineMark(e.text, lineBegin);
    if (m.kind != LineMark::Task) return;
    size_t box = lineBegin + m.indent + 3;
    size_t caret = e.caret, anchor = e.anchor;
    pushUndo(e, false);
    splice(e, box, 1, m.done ? L" " : L"x");
    e.caret = caret;
    e.anchor = anchor;
}

void moveLines(LiveText& e, bool up) {
    size_t first = 0, last = 0;
    selectedLines(e, first, last);
    size_t end = lineEnd(e.text, last);
    size_t caret = e.caret - first, anchor = e.anchor - first;
    std::wstring lines = e.text.substr(first, end - first);
    if (up) {
        if (first == 0) return;
        size_t above = lineStart(e.text, first - 1);
        std::wstring other = e.text.substr(above, first - 1 - above);
        pushUndo(e, false);
        splice(e, above, end - above, lines + L"\n" + other);
        settle(e, above + caret, above + anchor);
    } else {
        if (end >= e.text.size()) return;
        size_t belowEnd = lineEnd(e.text, end + 1);
        std::wstring other = e.text.substr(end + 1, belowEnd - end - 1);
        pushUndo(e, false);
        splice(e, first, belowEnd - first, other + L"\n" + lines);
        size_t to = first + other.size() + 1;
        settle(e, to + caret, to + anchor);
    }
}

void turnInto(LiveText& e, int block) {
    if (!e.multiline || block < BLOCK_TEXT || block > BLOCK_CODE) return;
    size_t first = 0, last = 0;
    selectedLines(e, first, last);
    if (block == BLOCK_CODE) {
        // The lines go between fences; lines in code already are left alone
        if (fenceState(e.text, first) != 0 || fenceState(e.text, last) != 0) return;
        size_t caret = e.caret, anchor = e.anchor;
        pushUndo(e, false);
        splice(e, lineEnd(e.text, last), 0, L"\n```");
        splice(e, first, 0, L"```\n");
        settle(e, caret + 4, anchor + 4);
        return;
    }
    // Worked out top down (numbers count on at each indent), written bottom up
    struct Change {
        size_t at, removed;
        std::wstring with;
    };
    std::vector<Change> changes;
    std::vector<std::pair<size_t, int>> counts;  // a numbered list's indents and their next numbers
    for (size_t at = first;; at = lineEnd(e.text, at) + 1) {
        if (!(blank(lineView(e.text, at)) && first != last) && fenceState(e.text, at) == 0) {
            size_t q = quoteLength(e.text, at);
            LineMark m = lineMark(e.text, at + q);
            size_t mark = m.kind == LineMark::None || m.kind == LineMark::Quote ? 0 : m.length;
            switch (block) {
                case BLOCK_TEXT:
                    // A paragraph: its quote, indent and mark all go
                    changes.push_back({at, q + m.indent + mark, L""});
                    break;
                case BLOCK_H1:
                case BLOCK_H2:
                case BLOCK_H3:
                    changes.push_back({at + q, m.indent + mark, std::wstring((size_t)(block - BLOCK_H1 + 1), L'#') + L" "});
                    break;
                case BLOCK_QUOTE:
                    if (!q) changes.push_back({at, 0, L"> "});
                    break;
                default: {
                    // A task keeps its box as ticked; list items keep their indent
                    if (block == BLOCK_TASKS && m.kind == LineMark::Task) break;
                    std::wstring with = block == BLOCK_TASKS ? L"- [ ] " : L"- ";
                    if (block == BLOCK_NUMBERS) {
                        while (!counts.empty() && counts.back().first > m.indent) counts.pop_back();
                        if (counts.empty() || counts.back().first < m.indent) counts.push_back({m.indent, 1});
                        with = std::to_wstring(counts.back().second++) + L". ";
                    }
                    changes.push_back({at + q + m.indent, mark, with});
                    break;
                }
            }
        }
        if (at >= last || lineEnd(e.text, at) >= e.text.size()) break;
    }
    if (changes.empty()) return;
    pushUndo(e, false);
    for (auto it = changes.rbegin(); it != changes.rend(); ++it) spliceMark(e, it->at, it->removed, it->with);
    e.preferredX = -1;
}

namespace {

// The caret at the end of a cell's text: the row's last cell for a short row
void caretInCell(LiveText& e, size_t rowStart, const std::vector<size_t>& bounds, size_t k) {
    if (bounds.size() < 2) {
        settle(e, rowStart, rowStart);
        return;
    }
    selectCell(e, rowStart, bounds, std::min(k, bounds.size() - 2));
    e.anchor = e.caret;
}

// The start of a table's row, counted from its header
size_t tableRowStart(const std::wstring& t, size_t first, int index) {
    size_t row = first;
    for (int i = 0; i < index && row < t.size(); ++i) row = lineEnd(t, row) + 1;
    return std::min(row, t.size());
}

}  // namespace

bool editTable(LiveText& e, size_t at, int edit) {
    TableSpot s = tableAt(e.text, at);
    if (!s.in) return false;
    std::vector<size_t> rows;
    for (size_t r = s.first;; r = lineEnd(e.text, r) + 1) {
        rows.push_back(r);
        if (r >= s.last) break;
    }
    const int k = s.column;
    std::wstring_view header = lineView(e.text, s.first);
    std::wstring pad(std::min(header.find_first_not_of(L' '), header.size()), L' ');
    switch (edit) {
        case TABLE_ROW_ABOVE:
        case TABLE_ROW_BELOW: {
            // Nothing goes above the header; under it, a row goes past the delimiter row
            if (edit == TABLE_ROW_ABOVE && s.index < 2) return false;
            std::wstring row = pad + L"|";
            for (int c = 0; c < s.columns; ++c) row += L"  |";
            pushUndo(e, false);
            size_t start = s.row;
            if (edit == TABLE_ROW_ABOVE) {
                splice(e, s.row, 0, row + L"\n");
            } else {
                size_t end = lineEnd(e.text, s.index < 2 ? rows[1] : s.row);
                splice(e, end, 0, L"\n" + row);
                start = end + 1;
            }
            caretInCell(e, start, cellBounds(lineView(e.text, start)), (size_t)k);
            return true;
        }
        case TABLE_DELETE_ROW: {
            if (s.index < 2) return false;
            const bool lastRow = s.row == s.last;
            size_t end = lineEnd(e.text, s.row);
            pushUndo(e, false);
            // Its line and one line break go
            if (end < e.text.size()) splice(e, s.row, end + 1 - s.row, L"");
            else splice(e, s.row - 1, end + 1 - s.row, L"");
            // The row below takes its place, or the one above when it was the last;
            // with no rows left, the header
            size_t row = lastRow ? lineStart(e.text, s.row - 1) : s.row;
            if (delimiterRow(lineView(e.text, row))) row = s.first;
            caretInCell(e, row, cellBounds(lineView(e.text, row)), (size_t)k);
            return true;
        }
        case TABLE_COLUMN_LEFT:
        case TABLE_COLUMN_RIGHT: {
            const int c = edit == TABLE_COLUMN_LEFT ? k : k + 1;
            pushUndo(e, false);
            for (size_t i = rows.size(); i-- > 0;) {
                size_t r = rows[i];
                std::wstring_view line = lineView(e.text, r);
                std::vector<size_t> bounds = cellBounds(line);
                if (bounds.empty()) continue;
                std::wstring cell = i == 1 ? L" --- |" : L"  |";
                if (c + 1 < (int)bounds.size()) splice(e, r + bounds[c] + 1, 0, cell);  // after the pipe that opens cell c
                else if (bounds.back() >= line.size()) splice(e, r + line.size(), 0, L" |" + cell);  // a row with no last pipe
                else splice(e, r + bounds.back() + 1, 0, cell);
            }
            size_t row = tableRowStart(e.text, s.first, s.index);
            caretInCell(e, row, cellBounds(lineView(e.text, row)), (size_t)c);
            return true;
        }
        case TABLE_DELETE_COLUMN: {
            if (s.columns <= 1) return false;
            pushUndo(e, false);
            for (size_t i = rows.size(); i-- > 0;) {
                std::vector<size_t> bounds = cellBounds(lineView(e.text, rows[i]));
                if (k + 1 < (int)bounds.size()) splice(e, rows[i] + bounds[k], bounds[k + 1] - bounds[k], L"");
            }
            size_t row = tableRowStart(e.text, s.first, s.index);
            caretInCell(e, row, cellBounds(lineView(e.text, row)), (size_t)k);
            return true;
        }
        case TABLE_ALIGN_LEFT:
        case TABLE_ALIGN_CENTER:
        case TABLE_ALIGN_RIGHT: {
            std::vector<size_t> bounds = cellBounds(lineView(e.text, rows[1]));
            if (k + 1 >= (int)bounds.size()) return false;
            // The alignment it has already goes back to none
            int want = edit - TABLE_ALIGN_LEFT + 1;
            const wchar_t* cell = want == s.align ? L" --- " : want == 1 ? L" :--- " : want == 2 ? L" :---: " : L" ---: ";
            pushUndo(e, false);
            spliceKeep(e, rows[1] + bounds[k] + 1, bounds[k + 1] - bounds[k] - 1, cell);
            return true;
        }
        default:
            return false;
    }
}

void unlink(LiveText& e, size_t at) {
    LinkSpan l = linkAt(e.text, at);
    if (l.kind != LinkSpan::Inline && l.kind != LinkSpan::Wiki) return;
    std::wstring words = e.text.substr(l.textFrom, l.textTo - l.textFrom);
    pushUndo(e, false);
    splice(e, l.from, l.to - l.from, words);
    settle(e, l.from + words.size(), l.from + words.size());
}

void relink(LiveText& e, size_t at, const std::wstring& address) {
    LinkSpan l = linkAt(e.text, at);
    if (l.kind == LinkSpan::None || l.kind == LinkSpan::Address || address.empty()) return;
    pushUndo(e, false);
    spliceKeep(e, l.addressFrom, l.addressTo - l.addressFrom, address);
}

std::wstring forEditing(std::string utf8) {
    if (utf8.size() >= 3 && (unsigned char)utf8[0] == 0xEF && (unsigned char)utf8[1] == 0xBB && (unsigned char)utf8[2] == 0xBF) {
        utf8.erase(0, 3);
    }
    std::wstring wide = toWide(utf8);
    std::wstring out;
    out.reserve(wide.size());
    for (wchar_t c : wide) {
        if (c != L'\r') out += c;
    }
    return out;
}

std::string forSaving(const std::wstring& text, const std::string& original) {
    std::string utf8 = toUtf8(text);
    if (original.find("\r\n") != std::string::npos) {
        std::string out;
        out.reserve(utf8.size() + utf8.size() / 20);
        for (char c : utf8) {
            if (c == '\n') out += '\r';
            out += c;
        }
        utf8 = std::move(out);
    }
    if (original.size() >= 3 && (unsigned char)original[0] == 0xEF && (unsigned char)original[1] == 0xBB &&
        (unsigned char)original[2] == 0xBF) {
        utf8.insert(0, "\xEF\xBB\xBF");
    }
    return utf8;
}

// --- Insertions (the slash menu) ----------------------------------------------

namespace {

struct Entry {
    int insert;
    const char* key;        // its name in the interface
    const wchar_t* words;   // English words it is found by in any language
    const wchar_t* markdown;
};

const Entry kEntries[] = {
    {INS_H1, "live.insert.h1", L"h1 heading title", L"#"},
    {INS_H2, "live.insert.h2", L"h2 heading subtitle", L"##"},
    {INS_H3, "live.insert.h3", L"h3 heading", L"###"},
    {INS_QUOTE, "live.insert.quote", L"quote blockquote", L">"},
    {INS_DIVIDER, "live.insert.divider", L"divider rule line hr separator", L"---"},
    {INS_BULLETS, "live.insert.bullets", L"bulleted list unordered ul", L"-"},
    {INS_NUMBERS, "live.insert.numbers", L"numbered list ordered ol", L"1."},
    {INS_TASKS, "live.insert.tasks", L"task todo checklist checkbox", L"- [ ]"},
    {INS_NOTE, "live.insert.note", L"note callout info alert", L"[!NOTE]"},
    {INS_TIP, "live.insert.tip", L"tip callout hint alert", L"[!TIP]"},
    {INS_IMPORTANT, "live.insert.important", L"important callout alert", L"[!IMPORTANT]"},
    {INS_WARNING, "live.insert.warning", L"warning callout alert", L"[!WARNING]"},
    {INS_CAUTION, "live.insert.caution", L"caution callout danger alert", L"[!CAUTION]"},
    {INS_CODE, "live.insert.code", L"code block fence snippet", L"```"},
    {INS_INLINE_MATH, "live.insert.inline_math", L"inline math formula equation latex", L"$x$"},
    {INS_MATH, "live.insert.math", L"math block formula equation latex", L"$$"},
    {INS_FLOWCHART, "live.insert.flowchart", L"flowchart diagram mermaid graph", L"mermaid"},
    {INS_SEQUENCE, "live.insert.sequence", L"sequence diagram mermaid", L"mermaid"},
    {INS_CLASS, "live.insert.class", L"class diagram uml mermaid", L"mermaid"},
    {INS_STATE, "live.insert.state", L"state diagram machine mermaid", L"mermaid"},
    {INS_GANTT, "live.insert.gantt", L"gantt chart timeline schedule diagram mermaid", L"mermaid"},
    {INS_PIE, "live.insert.pie", L"pie chart diagram mermaid", L"mermaid"},
    {INS_MINDMAP, "live.insert.mindmap", L"mind map mindmap diagram mermaid", L"mermaid"},
    {INS_PLANTUML, "live.insert.plantuml", L"plantuml uml diagram", L"plantuml"},
    {INS_TABLE, "live.insert.table", L"table grid columns", L"| |"},
    {INS_IMAGE, "live.insert.image", L"image picture photo", L"![]()"},
    {INS_LINK, "live.insert.link", L"link url address", L"[]()"},
    {INS_FOOTNOTE, "live.insert.footnote", L"footnote reference", L"[^1]"},
    {INS_PROPERTIES, "live.insert.properties", L"properties front matter yaml metadata", L"---"},
};

// How well a name answers what was typed: 0 best, -1 not at all
int matchScore(const std::wstring& name, const std::wstring& words, const std::wstring& q) {
    if (q.empty()) return 0;
    std::wstring n = lowered(name);
    if (n.rfind(q, 0) == 0) return 0;
    auto wordStarts = [&](const std::wstring& s) {
        for (size_t i = 0; i < s.size(); ++i) {
            if ((i == 0 || s[i - 1] == L' ') && s.compare(i, q.size(), q) == 0) return true;
        }
        return false;
    };
    if (wordStarts(n)) return 1;
    if (wordStarts(words)) return 2;
    if (n.find(q) != std::wstring::npos) return 3;
    return -1;
}

}  // namespace

std::vector<LiveText::Choice> slashChoices(App& app, const std::wstring& typed) {
    std::wstring q = lowered(typed);
    while (!q.empty() && q.back() == L' ') q.pop_back();
    std::vector<std::pair<int, LiveText::Choice>> found;
    for (const Entry& entry : kEntries) {
        std::wstring name = tr(app, entry.key);
        int score = matchScore(name, entry.words, q);
        if (score < 0) continue;
        LiveText::Choice c;
        c.label = name;
        c.detail = entry.markdown;
        c.insert = entry.insert;
        found.push_back({score, c});
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<LiveText::Choice> out;
    for (auto& f : found) out.push_back(std::move(f.second));
    return out;
}

void applyInsert(LiveText& e, int insert, const std::wstring& image, int columns, int rows) {
    size_t slash = e.popup == LiveText::Popup::Slash && e.popupFrom > 0 ? e.popupFrom - 1 : e.selStart();
    size_t caret = std::max(e.caret, slash);
    pushUndo(e, false);
    // The / and what was typed after it go; so does a selection
    if (e.popup == LiveText::Popup::Slash && slash < e.text.size() && e.text[slash] == L'/') {
        splice(e, slash, caret - slash, L"");
    } else if (e.hasSelection()) {
        splice(e, e.selStart(), e.selEnd() - e.selStart(), L"");
    }
    settle(e, slash, slash);
    e.popup = LiveText::Popup::None;
    e.choices.clear();
    size_t ls = lineStart(e.text, slash);
    size_t le = lineEnd(e.text, slash);
    bool emptyLine = blank(lineView(e.text, ls));
    const wchar_t kCaret = 1, kEnd = 2;
    // A template at `at`: the caret where it marks one, a selection between two marks
    auto place = [&](std::wstring tpl, size_t at) {
        size_t s = tpl.find(kCaret);
        if (s != std::wstring::npos) tpl.erase(s, 1);
        size_t t = tpl.find(kEnd);
        if (t != std::wstring::npos) tpl.erase(t, 1);
        splice(e, at, 0, tpl);
        if (s == std::wstring::npos) settle(e, at + tpl.size(), at + tpl.size());
        else settle(e, at + (t == std::wstring::npos ? s : t), at + s);
    };
    // A block on its own lines: the empty line it was asked for on, or under the line
    auto block = [&](std::wstring tpl) {
        size_t at;
        std::wstring before, after;
        size_t below = le < e.text.size() ? le + 1 : std::wstring::npos;
        bool textBelow = below != std::wstring::npos && !blank(lineView(e.text, below));
        if (!emptyLine && le > ls && e.text[le - 1] == L' ' && slash == le) {
            // The space the / was typed after goes with it
            splice(e, le - 1, 1, L"");
            --le;
        }
        if (emptyLine) {
            splice(e, ls, le - ls, L"");
            at = ls;
            if (ls > 0 && !blank(lineView(e.text, lineStart(e.text, ls - 1)))) before = L"\n";
        } else {
            at = le;
            before = L"\n\n";
        }
        if (textBelow) after = L"\n";
        if (tpl.find(kCaret) == std::wstring::npos) {
            // The caret goes on below the block
            after.clear();
            tpl += textBelow ? std::wstring(L"\n\n") + kCaret + L"\n" : std::wstring(L"\n\n") + kCaret;
        }
        place(before + tpl + after, at);
    };
    // A line mark: the line's own list, quote or heading mark gives way
    auto lineMarkTo = [&](const std::wstring& mark) {
        LineMark m = lineMark(e.text, ls);
        size_t removed = m.kind == LineMark::None ? 0 : m.length;
        spliceMark(e, ls + m.indent, removed, mark);
        e.preferredX = -1;
    };
    std::wstring fence = L"```";
    // A diagram's template, the caret at the end of its body
    auto diagram = [&](int kind) {
        std::wstring tpl = diagramTemplate(kind);
        while (!tpl.empty() && tpl.back() == L'\n') tpl.pop_back();
        size_t close = tpl.rfind(L'\n');
        if (close != std::wstring::npos) tpl.insert(close, 1, kCaret);
        block(tpl);
    };
    switch (insert) {
        case INS_H1: lineMarkTo(L"# "); break;
        case INS_H2: lineMarkTo(L"## "); break;
        case INS_H3: lineMarkTo(L"### "); break;
        case INS_QUOTE: lineMarkTo(L"> "); break;
        case INS_BULLETS: lineMarkTo(L"- "); break;
        case INS_NUMBERS: lineMarkTo(L"1. "); break;
        case INS_TASKS: lineMarkTo(L"- [ ] "); break;
        case INS_DIVIDER: block(L"---"); break;
        case INS_NOTE: block(std::wstring(L"> [!NOTE]\n> ") + kCaret); break;
        case INS_TIP: block(std::wstring(L"> [!TIP]\n> ") + kCaret); break;
        case INS_IMPORTANT: block(std::wstring(L"> [!IMPORTANT]\n> ") + kCaret); break;
        case INS_WARNING: block(std::wstring(L"> [!WARNING]\n> ") + kCaret); break;
        case INS_CAUTION: block(std::wstring(L"> [!CAUTION]\n> ") + kCaret); break;
        case INS_CODE: block(fence + L"\n" + kCaret + L"\n" + fence); break;
        case INS_MATH: block(std::wstring(L"$$\n") + kCaret + L"\n$$"); break;
        case INS_FLOWCHART: diagram(0); break;
        case INS_SEQUENCE: diagram(1); break;
        case INS_CLASS: diagram(2); break;
        case INS_STATE: diagram(3); break;
        case INS_GANTT: diagram(4); break;
        case INS_PIE: diagram(5); break;
        case INS_PLANTUML: diagram(7); break;
        case INS_MINDMAP: diagram(10); break;
        case INS_TABLE: {
            // Its header to type first, the first cell's word selected
            columns = std::clamp(columns, 1, 12);
            rows = std::clamp(rows, 0, 50);
            std::wstring tpl = std::wstring(L"| ") + kCaret + L"Column" + kEnd + L" |";
            for (int c = 1; c < columns; ++c) tpl += L" Column |";
            tpl += L"\n|";
            for (int c = 0; c < columns; ++c) tpl += L" --- |";
            for (int r = 0; r < rows; ++r) {
                tpl += L"\n|";
                for (int c = 0; c < columns; ++c) tpl += L"  |";
            }
            block(tpl);
            break;
        }
        case INS_INLINE_MATH: place(std::wstring(L"$") + kCaret + L"$", slash); break;
        case INS_LINK: place(std::wstring(L"[") + kCaret + L"]()", slash); break;
        case INS_IMAGE:
            if (!image.empty()) {
                size_t cut = image.find_last_of(L"\\/");
                std::wstring name = image.substr(cut == std::wstring::npos ? 0 : cut + 1);
                size_t dot = name.find_last_of(L'.');
                if (dot != std::wstring::npos && dot > 0) name.erase(dot);
                place(L"![" + name + L"](" + image + L")", slash);
            } else {
                place(std::wstring(L"![](") + kCaret + L")", slash);
            }
            break;
        case INS_FOOTNOTE: {
            int n = 1;
            while (e.text.find(L"[^" + std::to_wstring(n) + L"]") != std::wstring::npos) ++n;
            std::wstring ref = L"[^" + std::to_wstring(n) + L"]";
            splice(e, slash, 0, ref);
            // The note itself at the end, where the caret goes to write it
            std::wstring tail;
            size_t end = e.text.size();
            if (end && e.text[end - 1] != L'\n') tail += L"\n";
            if (!(end >= 2 && e.text[end - 1] == L'\n' && e.text[end - 2] == L'\n')) tail += L"\n";
            tail += ref + L": ";
            splice(e, end, 0, tail);
            settle(e, e.text.size(), e.text.size());
            break;
        }
        case INS_PROPERTIES:
            if (e.text.compare(0, 4, L"---\n") == 0 && e.text.find(L"\n---", 3) != std::wstring::npos) {
                size_t close = e.text.find(L"\n---", 3);
                settle(e, close, close);
            } else {
                splice(e, 0, 0, L"---\ntitle: \n---\n\n");
                settle(e, 11, 11);
            }
            break;
        default: break;
    }
}

// --- The popup ------------------------------------------------------------------

namespace {

void openPopup(LiveText& e, LiveText::Popup kind, LiveText::Popup was, size_t from, std::vector<LiveText::Choice> list) {
    // Typing on keeps the place in the list while its rows stay the same
    bool same = kind == was && from == e.popupFrom && list.size() == e.choices.size() &&
                std::equal(list.begin(), list.end(), e.choices.begin(),
                           [](const LiveText::Choice& a, const LiveText::Choice& b) { return a.label == b.label; });
    if (!same) {
        e.choice = 0;
        e.popupTop = 0;
    }
    e.popup = kind;
    e.popupFrom = from;
    e.choices = std::move(list);
    e.choice = std::clamp(e.choice, 0, (int)e.choices.size() - 1);
}

}  // namespace

void updatePopup(App& app, LiveText& e, const Host& host) {
    const LiveText::Popup was = e.popup;
    e.popup = LiveText::Popup::None;
    if (!e.multiline || e.hasSelection()) {
        e.choices.clear();
        return;
    }
    size_t caret = std::min(e.caret, e.text.size());
    size_t ls = lineStart(e.text, caret);
    // [[ and a name
    if (host.names) {
        for (size_t i = caret; i >= ls + 2 && caret - i <= 80; --i) {
            wchar_t c = e.text[i - 1];
            if (c == L'[' && e.text[i - 2] == L'[') {
                std::wstring query = e.text.substr(i, caret - i);
                std::vector<LiveText::Choice> list = host.names(query);
                if (!list.empty()) {
                    if (list.size() > 8) list.resize(8);
                    openPopup(e, LiveText::Popup::Names, was, i, std::move(list));
                    return;
                }
                break;
            }
            if (c == L']' || c == L'|' || c == L'#' || c == L'\n') break;
        }
    }
    // A / at a line's start or after a space, with a few letters after it
    size_t slash = std::wstring::npos;
    for (size_t i = caret; i > ls && caret - i <= 24; --i) {
        wchar_t c = e.text[i - 1];
        if (c == L'/') {
            slash = i - 1;
            break;
        }
        if (!iswalnum(c) && c != L' ' && c != L'-') break;
    }
    if (slash == std::wstring::npos || slash == e.dismissedSlash) {
        if (slash == std::wstring::npos) e.dismissedSlash = std::wstring::npos;
        e.choices.clear();
        return;
    }
    LineMark m = lineMark(e.text, ls);
    bool startsLine = slash == ls + m.indent + m.length || slash == ls;
    bool afterSpace = slash > ls && (e.text[slash - 1] == L' ' || e.text[slash - 1] == L'\t');
    std::wstring query = e.text.substr(slash + 1, caret - slash - 1);
    if ((!startsLine && !afterSpace) || (!query.empty() && query[0] == L' ') || query.find(L"  ") != std::wstring::npos) {
        e.choices.clear();
        return;
    }
    // Not in code: a fence, or a span of backticks before it on the line
    size_t ticks = 0;
    for (size_t i = ls; i < slash; ++i) ticks += e.text[i] == L'`';
    if (ticks % 2 == 1 || fenceState(e.text, ls) >= 2) {
        e.choices.clear();
        return;
    }
    std::vector<LiveText::Choice> list = slashChoices(app, query);
    if (list.empty()) {
        e.choices.clear();
        return;
    }
    openPopup(e, LiveText::Popup::Slash, was, slash + 1, std::move(list));
}

void closePopup(LiveText& e) {
    if (e.popup == LiveText::Popup::Slash && e.popupFrom > 0) e.dismissedSlash = e.popupFrom - 1;
    e.popup = LiveText::Popup::None;
    e.choices.clear();
}

void pickChoice(App& app, LiveText& e, int index, bool ctrl, const Host& host) {
    (void)app;
    if (e.popup == LiveText::Popup::None || index < 0 || index >= (int)e.choices.size()) return;
    LiveText::Choice pick = e.choices[index];
    if (e.popup == LiveText::Popup::Slash) {
        std::wstring image;
        if (pick.insert == INS_IMAGE && host.pickImage) {
            image = host.pickImage();
            if (image.empty()) {
                closePopup(e);
                return;
            }
        }
        applyInsert(e, pick.insert, image);
    } else {
        // What was typed after [[ becomes the name; the brackets close unless they follow
        size_t caret = std::min(e.caret, e.text.size());
        size_t from = std::min(e.popupFrom, caret);
        bool closed = e.text.compare(caret, 2, L"]]") == 0;
        pushUndo(e, false);
        splice(e, from, caret - from, pick.label + (closed ? std::wstring() : std::wstring(L"]]")));
        size_t after = from + pick.label.size() + 2;
        settle(e, after, after);
        e.popup = LiveText::Popup::None;
        e.choices.clear();
        if (host.picked) host.picked(pick, ctrl);
    }
    e.popup = LiveText::Popup::None;
    e.choices.clear();
}

// --- Keys ----------------------------------------------------------------------

namespace {

// A character back or on, a surrogate pair as one
size_t charBefore(const std::wstring& t, size_t at) {
    if (at == 0) return 0;
    --at;
    if (at > 0 && IS_LOW_SURROGATE(t[at]) && IS_HIGH_SURROGATE(t[at - 1])) --at;
    return at;
}

size_t charAfter(const std::wstring& t, size_t at) {
    if (at >= t.size()) return t.size();
    ++at;
    if (at < t.size() && IS_LOW_SURROGATE(t[at]) && IS_HIGH_SURROGATE(t[at - 1])) ++at;
    return at;
}

}  // namespace

Result key(App& app, HWND hwnd, LiveText& e, WPARAM vk, bool ctrl, bool shift, const Host& host) {
    if (e.popup != LiveText::Popup::None && !e.choices.empty()) {
        int count = (int)e.choices.size();
        switch (vk) {
            case VK_UP:
            case VK_DOWN:
                e.choice = (e.choice + (vk == VK_DOWN ? 1 : count - 1)) % count;
                return Result::Handled;
            case VK_RETURN:
            case VK_TAB:
                pickChoice(app, e, e.choice, ctrl, host);
                return Result::Handled;
            case VK_ESCAPE:
                closePopup(e);
                return Result::Handled;
            default:
                break;
        }
    }
    auto move = [&](size_t to) {
        e.caret = std::min(to, e.text.size());
        if (!shift) e.anchor = e.caret;
    };
    bool md = e.multiline;
    switch (vk) {
        case VK_ESCAPE:
            return Result::Cancel;
        case VK_RETURN:
            if (!md || ctrl) return Result::Commit;
            newline(e, shift);
            break;
        case VK_TAB:
            if (ctrl) return Result::Unhandled;
            tab(e, shift);
            break;
        case VK_BACK:
            backspace(e, ctrl);
            break;
        case VK_DELETE:
            deleteForward(e, ctrl);
            break;
        case VK_LEFT:
            if (!shift && e.hasSelection()) move(e.selStart());
            else move(ctrl ? wordLeft(e.text, e.caret) : charBefore(e.text, e.caret));
            e.preferredX = -1;
            break;
        case VK_RIGHT:
            if (!shift && e.hasSelection()) move(e.selEnd());
            else move(ctrl ? wordRight(e.text, e.caret) : charAfter(e.text, e.caret));
            e.preferredX = -1;
            break;
        case VK_HOME: {
            if (ctrl) {
                move(0);
            } else {
                // Where the line's text starts first, past its mark; then the line's start
                size_t ls = lineStart(e.text, e.caret);
                LineMark m = lineMark(e.text, ls);
                size_t text = ls + (md ? m.indent + m.length : 0);
                move(e.caret == text || text > lineEnd(e.text, ls) ? ls : text);
            }
            e.preferredX = -1;
            break;
        }
        case VK_END:
            move(ctrl ? e.text.size() : lineEnd(e.text, e.caret));
            e.preferredX = -1;
            break;
        case VK_UP:
        case VK_DOWN: {
            if (!md) return Result::Handled;
            float x = 0, y = 0, h = 0;
            if (!host.caretPoint || !host.positionAt || !host.caretPoint(x, y, h)) break;
            if (e.preferredX < 0) e.preferredX = x;
            float keep = e.preferredX;
            move(host.positionAt(e.preferredX, vk == VK_UP ? y - h * 0.5f : y + h * 1.5f, vk == VK_UP ? -1 : 1));
            e.preferredX = keep;
            break;
        }
        case 'A':
            if (!ctrl) return Result::Unhandled;
            e.anchor = 0;
            e.caret = e.text.size();
            break;
        case 'C':
        case 'X':
        case VK_INSERT:
            if (!ctrl && !(vk == VK_INSERT && shift)) return Result::Unhandled;
            if (vk == VK_INSERT && shift) {
                paste(e, readClipboardText(hwnd));
            } else if (e.hasSelection()) {
                copyToClipboard(hwnd, e.text.substr(e.selStart(), e.selEnd() - e.selStart()));
                if (vk == 'X') replace(e, L"");
            }
            break;
        case 'V':
            if (!ctrl) return Result::Unhandled;
            paste(e, readClipboardText(hwnd));
            break;
        case 'Z':
        case 'Y':
            if (!ctrl) return Result::Unhandled;
            if (host.undo) host.undo(vk == 'Y' || shift);
            else undo(e, vk == 'Y' || shift);
            break;
        case 'B':
            if (!ctrl || !md) return Result::Unhandled;
            toggleWrap(e, L"**");
            break;
        case 'I':
            if (!ctrl || !md) return Result::Unhandled;
            toggleWrap(e, L"*");
            break;
        default:
            // Typing keys come back as characters
            return Result::Unhandled;
    }
    updatePopup(app, e, host);
    return Result::Handled;
}

void paste(LiveText& e, const std::wstring& clip) {
    std::wstring clean;
    clean.reserve(clip.size());
    for (wchar_t c : clip) {
        if (c != L'\r') clean += c;
    }
    if (clean.empty()) return;
    // An address pasted over words makes them its link
    std::wstring trimmed = clean;
    while (!trimmed.empty() && (trimmed.back() == L'\n' || trimmed.back() == L' ')) trimmed.pop_back();
    if (e.multiline && e.hasSelection() && looksLikeAddress(trimmed)) {
        std::wstring sel = e.text.substr(e.selStart(), e.selEnd() - e.selStart());
        if (sel.find(L'\n') == std::wstring::npos && !looksLikeAddress(sel)) {
            size_t a = e.selStart(), b = e.selEnd();
            pushUndo(e, false);
            splice(e, b, 0, L"](" + trimmed + L")");
            splice(e, a, 0, L"[");
            size_t end = b + 3 + trimmed.size() + 1;
            settle(e, end, end);
            return;
        }
    }
    replace(e, clean);
}

void character(App& app, LiveText& e, wchar_t ch, const Host& host) {
    // Enter, Tab and Backspace are keys; Ctrl and a letter arrive as control characters
    if (ch < 32 || ch == 127) return;
    typeCharacter(e, ch);
    updatePopup(app, e, host);
}

}  // namespace live
