// Live (live_editor.h, docs/live-editor.md): the page as it reads, typed in.
//
// The page is the reading view's own layout of the document. Each of its
// top-level blocks knows its stretch of the page (App::blockSpans) and,
// through its first text, its lines in the text the layout was made from.
// The blocks the caret and the selection are in are drawn by the live text
// engine instead, at the page's faces and sizes, and the page below them
// moves by however much taller or shorter they came out. Typing changes
// only that stretch; the page is laid out again when the caret leaves a
// block it changed, or after a pause, as the source's preview is.

#include "live_editor.h"
#include "d2d_init.h"
#include "editor.h"
#include "i18n.h"
#include "editrail.h"
#include "input.h"
#include "live_text.h"
#include "print.h"
#include "render.h"
#include "settings.h"
#include "signals.h"
#include "ui_paint.h"
#include "utils.h"

#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>

namespace {

bool keyDown(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }

// A top-level block of the page: its lines in the text the page was laid
// out from, the stretch of the page it takes and what it draws there
struct Block {
    size_t from = 0, to = 0;       // its first line's start, its last line's end
    float top = 0, bottom = 0;     // its stretch, spacing included
    float inkTop = 0, inkBottom = 0;
    float baseline = -1;           // its first line's baseline, when that is text
    float runTop = 0, runX = 0;
    bool code = false;             // a fenced block, drawn on its background
    bool pageOnly = false;         // drawn from text elsewhere (footnotes gathered at the end)
    // Stretches after it with no text of their own (a divider), last first
    std::vector<std::pair<float, float>> after;
};

struct LiveEditor {
    LiveText e;
    live::LayoutCache region, probe;
    // The text the page was laid out from and, since then, one stretch of
    // today's text that holds every change
    std::wstring parsed;
    std::vector<size_t> parsedLines, parsedBytes;
    const void* parsedRoot = nullptr;
    bool dirty = false;
    size_t dirtyFrom = 0, dirtyTo = 0;
    std::ptrdiff_t delta = 0;
    // The page's blocks, made again with each layout
    std::vector<Block> blocks;
    unsigned layoutSeen = 0;
    size_t spansSeen = (size_t)-1;
    // This frame's live stretch: its text, the blocks it stands for, the
    // stretch of the page it replaces and where it draws
    bool placed = false;
    size_t from = 0, to = 0;
    int first = -1, last = -1;     // -1: a line between blocks
    float top = 0, bottom = 0;
    float textX = 0, textY = 0, width = 0, height = 0, liveBottom = 0, shift = 0;
    IDWriteTextLayout* layout = nullptr;  // owned by `region`
    // A diagram, a formula or a picture: what the page drew for it, under its Markdown
    bool preview = false;
    float previewTop = 0, previewBottom = 0, previewY = 0;
    long long regionKey = -2;      // its first block, or below -1 the line between blocks
    // After a switch, the caret goes back to its height in the window
    float wantCaretY = -1;
    bool reveal = false;
    size_t caretSeen = (size_t)-1;  // a caret moved elsewhere (find, go to line) is brought into view
    // The pointer
    bool dragging = false;
    float pressX = 0, pressY = 0;
    // [[ names: the notes beside the document, listed every few seconds
    std::wstring namesDir;
    std::vector<std::wstring> names;
    unsigned long long namesMs = 0;
    // The menu: the document it was opened on, and a link it asked to
    // follow once its edit is in the source
    std::string menuFile;
    std::string followLink;
};

LiveEditor& state(const App& app) {
    static std::map<const App*, LiveEditor> editors;
    LiveEditor& L = editors[&app];
    L.e.trackChanges = true;
    L.e.multiline = true;
    return L;
}

// ---, *** or ___ alone on a line
bool dividerLine(std::wstring_view line) {
    wchar_t mark = 0;
    int count = 0;
    for (wchar_t c : line) {
        if (c == L' ') continue;
        if ((c != L'-' && c != L'*' && c != L'_') || (mark && c != mark)) return false;
        mark = c;
        ++count;
    }
    return count >= 3;
}

bool blank(const std::wstring& t, size_t from, size_t to) {
    for (size_t i = from; i < to; ++i) {
        if (t[i] != L' ' && t[i] != L'\t') return false;
    }
    return true;
}

// --- Today's text and the page's ---------------------------------------------

void noteChange(LiveEditor& L, size_t at, size_t removed, size_t inserted) {
    if (!L.dirty) {
        L.dirty = true;
        L.dirtyFrom = at;
        L.dirtyTo = at + inserted;
        L.delta = (std::ptrdiff_t)inserted - (std::ptrdiff_t)removed;
        return;
    }
    size_t to = std::max(L.dirtyTo, at + removed);
    L.dirtyFrom = std::min(L.dirtyFrom, at);
    L.dirtyTo = to - removed + inserted;
    L.delta += (std::ptrdiff_t)inserted - (std::ptrdiff_t)removed;
}

// A place in the page's text, in today's: `after` for a block's end,
// which grows with typing at it
size_t today(const LiveEditor& L, size_t p, bool after) {
    if (!L.dirty) return p;
    size_t parsedEnd = (size_t)((std::ptrdiff_t)L.dirtyTo - L.delta);
    if (p < L.dirtyFrom || (p == L.dirtyFrom && !after)) return p;
    if (p > parsedEnd || (p == parsedEnd && after)) return (size_t)((std::ptrdiff_t)p + L.delta);
    return after ? L.dirtyTo : L.dirtyFrom;
}

// A source offset of the layout (UTF-8) in the page's text
size_t wideAt(const LiveEditor& L, size_t offset) {
    if (L.parsedBytes.empty() || L.parsedBytes.size() != L.parsedLines.size()) return std::min(offset, L.parsed.size());
    size_t line = (size_t)(std::upper_bound(L.parsedBytes.begin(), L.parsedBytes.end(), offset) - L.parsedBytes.begin());
    line = line ? line - 1 : 0;
    size_t w = L.parsedLines[line], bytes = L.parsedBytes[line];
    while (w < L.parsed.size() && bytes < offset) {
        wchar_t c = L.parsed[w];
        if (c < 0x80) {
            bytes += 1;
        } else if (c < 0x800) {
            bytes += 2;
        } else if (IS_HIGH_SURROGATE(c) && w + 1 < L.parsed.size()) {
            bytes += 4;
            ++w;
        } else {
            bytes += 3;
        }
        ++w;
    }
    return w;
}

// The page's text follows the source: its own edits, undo, a reload
void sync(App& app, LiveEditor& L) {
    const std::wstring& t = app.editorText;
    if (L.e.text.size() != t.size() || L.e.text != t) {
        size_t n = std::min(L.e.text.size(), t.size());
        size_t a = 0;
        while (a < n && L.e.text[a] == t[a]) ++a;
        size_t b = 0;
        while (b < n - a && L.e.text[L.e.text.size() - 1 - b] == t[t.size() - 1 - b]) ++b;
        noteChange(L, a, L.e.text.size() - a - b, t.size() - a - b);
        L.e.text = t;
        live::closePopup(L.e);
        live::closeMenu(L.e);
    }
    L.e.caret = std::min(app.editorCursorPos, t.size());
    L.e.anchor = app.editorHasSelection ? std::min(app.editorSelStart, t.size()) : L.e.caret;
}

// --- The page's blocks ---------------------------------------------------------

void measureInk(App& app, LiveEditor& L) {
    for (Block& b : L.blocks) {
        b.inkTop = 1e30f;
        b.inkBottom = -1e30f;
        b.baseline = -1;
        b.runTop = 1e30f;
    }
    if (L.blocks.empty()) return;
    auto blockAt = [&](float y) -> Block* {
        auto it = std::upper_bound(L.blocks.begin(), L.blocks.end(), y, [](float v, const Block& b) { return v < b.top; });
        if (it == L.blocks.begin()) return nullptr;
        Block* b = &*(it - 1);
        return y < b->bottom ? b : nullptr;
    };
    auto ink = [&](float top, float bottom) {
        if (Block* b = blockAt(top)) {
            b->inkTop = std::min(b->inkTop, top);
            b->inkBottom = std::max(b->inkBottom, bottom);
        }
    };
    for (const auto& r : app.layoutRects) ink(r.rect.top, r.rect.bottom);
    for (const auto& l : app.layoutLines) ink(std::min(l.p1.y, l.p2.y), std::max(l.p1.y, l.p2.y));
    for (const auto& s : app.layoutShapes) ink(s.rect.top, s.rect.bottom);
    for (const auto& c : app.layoutConnectors) ink(c.bounds.top, c.bounds.bottom);
    for (const auto& bmp : app.layoutBitmaps) ink(bmp.destRect.top, bmp.destRect.bottom);
    for (const auto& run : app.layoutTextRuns) {
        Block* b = blockAt(run.bounds.top);
        if (!b) continue;
        b->inkTop = std::min(b->inkTop, run.bounds.top);
        b->inkBottom = std::max(b->inkBottom, run.bounds.bottom);
        // Its first line's baseline: the topmost run, the leftmost of those
        bool earlier = run.bounds.top < b->runTop - 0.5f || (std::fabs(run.bounds.top - b->runTop) <= 0.5f && run.pos.x < b->runX);
        if (!run.layout || !earlier) continue;
        UINT32 count = 0;
        run.layout->GetLineMetrics(nullptr, 0, &count);
        if (!count) continue;
        std::vector<DWRITE_LINE_METRICS> lines(count);
        if (FAILED(run.layout->GetLineMetrics(lines.data(), count, &count))) continue;
        b->runTop = run.bounds.top;
        b->runX = run.pos.x;
        b->baseline = run.pos.y + lines[0].baseline;
    }
    for (Block& b : L.blocks) {
        if (b.inkTop > b.inkBottom) b.inkTop = b.inkBottom = b.top;
    }
}

void buildBlocks(App& app, LiveEditor& L) {
    L.blocks.clear();
    L.layoutSeen = app.layoutSerial;
    L.spansSeen = app.blockSpans.size();
    const std::wstring& t = L.parsed;
    const std::vector<size_t>& lines = L.parsedLines;
    if (lines.empty()) return;
    std::vector<char> fence;
    live::fenceLines(t, lines, fence);
    auto lineOf = [&](size_t at) {
        size_t i = (size_t)(std::upper_bound(lines.begin(), lines.end(), at) - lines.begin());
        return i ? i - 1 : 0;
    };
    auto lineText = [&](size_t i) {
        size_t end = i + 1 < lines.size() ? lines[i + 1] - 1 : t.size();
        return std::wstring_view(t.data() + lines[i], end - lines[i]);
    };
    auto trimmed = [](std::wstring_view s) {
        size_t a = s.find_first_not_of(L" \t");
        return a == std::wstring_view::npos ? std::wstring_view() : s.substr(a);
    };
    for (const auto& span : app.blockSpans) {
        if (span.sourceOffset == SIZE_MAX) {
            // No text of its own (a divider, the properties): the stretch goes
            // with the block before; at the top, a block from the first line
            if (L.blocks.empty()) {
                Block b;
                b.top = span.top;
                b.bottom = span.bottom;
                L.blocks.push_back(b);
            } else {
                L.blocks.back().after.push_back({span.top, span.bottom});
                L.blocks.back().bottom = span.bottom;
            }
            continue;
        }
        size_t line = lineOf(wideAt(L, span.sourceOffset));
        // A fence, a $$ block or a quote starts above its first text
        while (line > 0 && (fence[line] == 2 || fence[line] == 3)) --line;
        if (line > 0 && fence[line] == 0 && trimmed(lineText(line - 1)) == L"$$") --line;
        while (line > 0 && trimmed(lineText(line)).substr(0, 1) == L">" && trimmed(lineText(line - 1)).substr(0, 1) == L">") --line;
        size_t from = lines[line];
        if (!L.blocks.empty() && from <= L.blocks.back().from) {
            // Its text comes earlier (footnotes gathered at the page's end):
            // a stretch of the page alone, never opened
            Block b;
            b.from = b.to = L.blocks.back().from;
            b.top = span.top;
            b.bottom = span.bottom;
            b.pageOnly = true;
            L.blocks.push_back(b);
            continue;
        }
        Block b;
        b.from = from;
        b.top = span.top;
        b.bottom = span.bottom;
        b.code = fence[line] == 1;
        L.blocks.push_back(b);
    }
    // Each runs to the end of its last line with text before the next
    for (size_t i = 0; i < L.blocks.size(); ++i) {
        if (L.blocks[i].pageOnly) continue;
        size_t next = t.size();
        for (size_t j = i + 1; j < L.blocks.size(); ++j) {
            if (!L.blocks[j].pageOnly) {
                next = L.blocks[j].from;
                break;
            }
        }
        size_t to = L.blocks[i].from;
        for (size_t at = L.blocks[i].from; at < next || at == t.size();) {
            size_t end = std::min(live::lineEnd(t, at), next);
            if (!blank(t, at, end)) to = end;
            if (end >= t.size()) break;
            at = end + 1;
        }
        L.blocks[i].to = to;
    }
    // A divider after a block is a block of its own: its line, and the
    // stretch the page gave it
    for (size_t i = 0; i < L.blocks.size(); ++i) {
        while (!L.blocks[i].pageOnly && !L.blocks[i].after.empty()) {
            Block& b = L.blocks[i];
            size_t ls = live::lineStart(t, b.to);
            if (ls <= b.from || !dividerLine(std::wstring_view(t.data() + ls, b.to - ls))) break;
            Block rule;
            rule.from = ls;
            rule.to = b.to;
            rule.top = b.after.back().first;
            rule.bottom = b.bottom;
            b.bottom = rule.top;
            b.after.pop_back();
            // The block ends at its last line with text above the divider
            size_t to = b.from;
            for (size_t at = b.from; at < ls;) {
                size_t end = std::min(live::lineEnd(t, at), ls);
                if (!blank(t, at, end)) to = end;
                at = end + 1;
            }
            b.to = to;
            L.blocks.insert(L.blocks.begin() + (std::ptrdiff_t)i + 1, rule);
        }
    }
    measureInk(app, L);
}

void refreshBlocks(App& app, LiveEditor& L) {
    // A document parsed past this editor (a reload, another tab) is parsed again
    if (app.root.get() != L.parsedRoot) {
        editorReparse(app, true);
        if (app.root.get() != L.parsedRoot) liveTextParsed(app);
    }
    if (app.layoutDirty) {
        // The stretch in view first, the rest in the background, as render() lays out
        layoutDocumentViewportFirst(app);
        if (!app.layoutComplete && app.hwnd) PostMessage(app.hwnd, WM_APP_LAYOUT_CHUNK, 0, 0);
    }
    if (app.layoutSerial != L.layoutSeen || app.blockSpans.size() != L.spansSeen) buildBlocks(app, L);
}

// --- The live stretch ------------------------------------------------------------

float firstBaseline(IDWriteTextLayout* layout) {
    if (!layout) return 0;
    UINT32 count = 0;
    layout->GetLineMetrics(nullptr, 0, &count);
    if (!count) return 0;
    std::vector<DWRITE_LINE_METRICS> lines(count);
    if (FAILED(layout->GetLineMetrics(lines.data(), count, &count))) return 0;
    return lines[0].baseline;
}

// Where a block's text goes on the page: its first line on the page's
// first baseline, a fenced block from its background's top
float blockTextY(const Block& b, IDWriteTextLayout* layout) {
    if (b.baseline >= 0 && !b.code) return std::max(b.top, b.baseline - firstBaseline(layout));
    return b.inkTop;
}

void columnOf(App& app, const live::Look& look, bool code, float& x, float& width) {
    x = app.layoutIndent;
    width = std::max(dpi(app, 40.0f), app.layoutMaxWidth);
    if (code) {
        x += look.codeInset;
        width = std::max(dpi(app, 40.0f), width - look.codeInset * 2);
    }
}

// The blocks today's lines [a, b] touch: the first and the last, or none
// and the block before them
void blocksTouching(const LiveEditor& L, size_t a, size_t b, int& first, int& last, int& before) {
    first = last = before = -1;
    for (int i = 0; i < (int)L.blocks.size(); ++i) {
        if (L.blocks[i].pageOnly) continue;
        size_t f = today(L, L.blocks[i].from, false), e = today(L, L.blocks[i].to, true);
        if (e < a) {
            before = i;
            continue;
        }
        if (f > b) break;
        if (first < 0) first = i;
        last = i;
    }
}

// A block the page draws as a picture: a diagram's or a formula's fence,
// or a line with an image
bool drawsSomething(const std::wstring& t, size_t from, size_t to) {
    std::wstring_view text(t.data() + from, to - from);
    size_t lead = text.find_first_not_of(L" ");
    if (lead == std::wstring_view::npos) return false;
    std::wstring_view first = text.substr(lead, live::lineEnd(t, from) - from - lead);
    for (const wchar_t* fence : {L"```mermaid", L"~~~mermaid", L"```plantuml", L"~~~plantuml", L"```puml", L"$$"}) {
        if (first.substr(0, wcslen(fence)) == fence) return true;
    }
    size_t image = text.find(L"![");
    return image != std::wstring_view::npos && text.find(L"](", image) != std::wstring_view::npos;
}

void place(App& app, LiveEditor& L) {
    refreshBlocks(app, L);
    const std::wstring& t = L.e.text;
    size_t a = live::lineStart(t, L.e.selStart()), b = live::lineEnd(t, L.e.selEnd());
    int first = -1, last = -1, before = -1;
    if (L.dirty) {
        // What changed since the page was laid out has no drawing of its own:
        // it is drawn live with the caret's blocks, or, once the caret has
        // left it, the page catches up with it
        size_t ha = live::lineStart(t, std::min(L.dirtyFrom, t.size()));
        size_t hb = live::lineEnd(t, std::min(L.dirtyTo, t.size()));
        blocksTouching(L, ha, hb, first, last, before);
        size_t from = first >= 0 ? std::min(ha, today(L, L.blocks[first].from, false)) : ha;
        size_t to = first >= 0 ? std::max(hb, today(L, L.blocks[last].to, true)) : hb;
        if (b < from || a > to) {
            editorReparse(app, true);
            refreshBlocks(app, L);
        } else {
            a = std::min(a, from);
            b = std::max(b, to);
        }
    }
    auto find = [&] { blocksTouching(L, a, b, first, last, before); };
    find();
    // Past what is laid out so far: the rest first
    if (!app.layoutComplete && (first < 0 || last + 1 >= (int)L.blocks.size())) {
        ensureLayoutComplete(app);
        refreshBlocks(app, L);
        find();
    }
    live::Look look = live::pageLook(app);
    bool code = first >= 0 && first == last && L.blocks[first].code;
    columnOf(app, look, code, L.textX, L.width);
    if (first >= 0) {
        L.from = std::min(a, today(L, L.blocks[first].from, false));
        L.to = std::max(b, today(L, L.blocks[last].to, true));
    } else {
        L.from = a;
        L.to = b;
    }
    L.first = first;
    L.last = last;
    L.layout = live::layoutOf(app, L.e, L.from, L.to, L.width, look, L.region, live::caretLineOf(L.e, L.from, L.to));
    L.height = L.layout ? paint::metrics(L.layout).height : 0;
    L.preview = false;
    if (first >= 0) {
        const Block& fb = L.blocks[first];
        const Block& lb = L.blocks[last];
        L.top = fb.top;
        L.bottom = lb.bottom;
        L.textY = blockTextY(fb, L.layout);
        L.liveBottom = L.textY + L.height + (lb.bottom - lb.inkBottom);
        L.regionKey = first;
        if (first == last && fb.inkBottom > fb.inkTop && drawsSomething(t, L.from, L.to)) {
            L.preview = true;
            L.previewTop = fb.inkTop;
            L.previewBottom = fb.inkBottom;
            L.previewY = L.textY + L.height + dpi(app, 12.0f);
            L.liveBottom = L.previewY + (L.previewBottom - L.previewTop) + (lb.bottom - lb.inkBottom);
        }
    } else {
        // A line between blocks: it opens where the block before it ends
        float y = before >= 0 ? L.blocks[before].bottom : !L.blocks.empty() ? L.blocks[0].top : documentContentTop(app);
        L.top = L.bottom = y;
        L.textY = y;
        L.liveBottom = y + L.height + 14.0f * app.contentScale * app.zoomFactor;
        L.regionKey = -2 - (long long)a;
    }
    L.shift = L.liveBottom - L.bottom;
    L.placed = L.layout != nullptr;
}

// The text position at a point of the page as shown; Up and Down say
// which way to look past the live stretch
size_t positionAtShown(App& app, LiveEditor& L, float x, float y, int direction) {
    if (!L.placed) return L.e.caret;
    if (y >= L.top && y < L.liveBottom) {
        // Up and Down past the text go on into the blocks around it,
        // over the spacing the stretch keeps
        float ly = y - L.textY;
        if (!(direction > 0 && ly >= L.height) && !(direction < 0 && ly < 0))
            return live::positionAt(L.layout, L.from, L.to, x - L.textX, ly);
        y = direction > 0 ? L.liveBottom : L.top - 0.5f;
    }
    if (L.blocks.empty()) return direction < 0 ? 0 : L.e.text.size();
    float py = y < L.top ? y : y - L.shift;
    auto it = std::upper_bound(L.blocks.begin(), L.blocks.end(), py, [](float v, const Block& b) { return v < b.top; });
    int i = it == L.blocks.begin() ? 0 : (int)(it - L.blocks.begin()) - 1;
    if (L.first >= 0 && i >= L.first && i <= L.last) i = direction < 0 || y < L.top ? L.first - 1 : L.last + 1;
    // Past the page's first or last block there is nothing to go to
    if (direction > 0 && i >= 0 && i < (int)L.blocks.size() && py >= L.blocks[i].bottom) return L.to;
    if (direction < 0 && i >= 0 && i < (int)L.blocks.size() && py < L.blocks[i].top) return L.from;
    // A stretch with no lines of its own (footnotes gathered from elsewhere)
    // gives way to the block beside it
    while (i >= 0 && i < (int)L.blocks.size() && L.blocks[i].pageOnly) i += direction < 0 ? -1 : 1;
    if (i < 0) return L.from;
    if (i >= (int)L.blocks.size()) return L.to;
    // That block laid out as it reads, where the page has it
    const Block& block = L.blocks[i];
    size_t f = today(L, block.from, false), t = today(L, block.to, true);
    live::Look look = live::pageLook(app);
    float bx = 0, width = 0;
    columnOf(app, look, block.code, bx, width);
    IDWriteTextLayout* layout = live::layoutOf(app, L.e, f, t, width, look, L.probe, (size_t)-1);
    if (!layout) return f;
    py = std::clamp(py, block.inkTop, std::max(block.inkTop, block.inkBottom - 1.0f));
    return live::positionAt(layout, f, t, x - bx, py - blockTextY(block, layout));
}

float maxScroll(const App& app) { return std::max(0.0f, livePageHeight(app) - (float)app.height); }

void settleScroll(App& app, LiveEditor& L) {
    float cx = 0, cy = 0, ch = 0;
    if (!L.placed || !live::caretPoint(L.layout, L.from, L.e.caret, cx, cy, ch)) return;
    float caretTop = L.textY + cy;
    if (L.wantCaretY >= 0) {
        app.scrollY = std::clamp(caretTop - L.wantCaretY, 0.0f, maxScroll(app));
    } else if (L.reveal) {
        float margin = dpi(app, 16.0f);
        float top = app.scrollY + chromeTopHeight(app) + margin;
        float bottom = app.scrollY + (float)app.height - readPillClearance(app) - margin * 0.5f;
        if (caretTop < top) app.scrollY = caretTop - chromeTopHeight(app) - margin;
        else if (caretTop + ch > bottom) app.scrollY = caretTop + ch - (float)app.height + readPillClearance(app) + margin * 0.5f;
        app.scrollY = std::clamp(app.scrollY, 0.0f, maxScroll(app));
    }
    app.targetScrollY = app.scrollY;
    L.wantCaretY = -1;
    L.reveal = false;
}

// --- Links, task boxes, names, images --------------------------------------------

// The link around a place in the text, as followDocumentLink takes it
std::string rawLinkAt(const std::wstring& t, size_t at) {
    live::LinkSpan l = live::linkAt(t, at);
    if (l.kind == live::LinkSpan::None) return {};
    std::string address = toUtf8(t.substr(l.addressFrom, l.addressTo - l.addressFrom));
    return l.kind == live::LinkSpan::Wiki ? "wiki:" + address : address;
}

bool inside(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom; }

// The link at a point of the page as shown: in the live text, the one
// around the character there; on the page, the one drawn there
std::string linkAt(App& app, LiveEditor& L, float x, float y) {
    if (!L.placed) return {};
    if (y >= L.top && y < L.liveBottom) return rawLinkAt(L.e.text, live::positionAt(L.layout, L.from, L.to, x - L.textX, y - L.textY));
    float py = y >= L.liveBottom ? y - L.shift : y;
    for (const auto& link : app.linkRects) {
        if (inside(link.bounds, x, py)) return link.url;
    }
    return {};
}

// The task box at a point of the page as shown: the place of its mark
size_t taskAt(App& app, LiveEditor& L, float x, float y) {
    const size_t none = (size_t)-1;
    if (!L.placed) return none;
    if (y >= L.top && y < L.liveBottom) {
        size_t line = live::taskBoxAt(L.layout, L.e, L.from, L.to, x - L.textX, y - L.textY);
        if (line == none) return none;
        return line + live::lineMark(L.e.text, line).indent + 3;
    }
    float py = y >= L.liveBottom ? y - L.shift : y;
    for (const auto& task : app.taskRects) {
        if (!inside(task.rect, x, py) || task.markOffset == SIZE_MAX) continue;
        size_t at = today(L, wideAt(L, task.markOffset), false);
        const std::wstring& t = L.e.text;
        if (at > 0 && at < t.size() && t[at - 1] == L'[') return at;
        if (at + 1 < t.size() && t[at] == L'[') return at + 1;
        return none;
    }
    return none;
}

std::vector<LiveText::Choice> noteNames(App& app, LiveEditor& L, const std::wstring& typed) {
    namespace fs = std::filesystem;
    if (app.currentFile.empty()) return {};
    fs::path doc(toWide(app.currentFile));
    std::wstring dir = doc.parent_path().wstring();
    unsigned long long now = GetTickCount64();
    if (dir != L.namesDir || now - L.namesMs > 3000) {
        L.names.clear();
        L.namesDir = dir;
        L.namesMs = now;
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end && L.names.size() < 2000; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::wstring ext = toLower(it->path().extension().wstring());
            if (ext != L".md" && ext != L".markdown") continue;
            std::wstring stem = it->path().stem().wstring();
            if (_wcsicmp(stem.c_str(), doc.stem().c_str()) != 0) L.names.push_back(stem);
        }
        std::sort(L.names.begin(), L.names.end());
    }
    std::wstring q = toLower(typed);
    std::vector<LiveText::Choice> starting, holding;
    for (const std::wstring& name : L.names) {
        std::wstring key = toLower(name);
        LiveText::Choice c;
        c.label = name;
        c.detail = tr(app, "sheet.complete.folder");
        if (key.rfind(q, 0) == 0) starting.push_back(c);
        else if (key.find(q) != std::wstring::npos) holding.push_back(c);
    }
    starting.insert(starting.end(), holding.begin(), holding.end());
    return starting;
}

// An image picked from disk, as the document links it: beside it, relative
std::wstring pickImage(App& app, HWND hwnd) {
    namespace fs = std::filesystem;
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.svg;*.webp\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    std::wstring path = file;
    if (!app.currentFile.empty()) {
        std::error_code ec;
        fs::path rel = fs::path(path).lexically_relative(fs::path(toWide(app.currentFile)).parent_path());
        if (!rel.empty() && rel.native().rfind(L"..\\..", 0) != 0) path = rel.wstring();
    }
    std::wstring out;
    for (wchar_t c : path) {
        if (c == L'\\') out += L'/';
        else if (c == L' ') out += L"%20";
        else out += c;
    }
    return out;
}

// The clipboard holds more than text: a screenshot saves beside the document
// and a spreadsheet's range becomes a table, as the source's Ctrl+V makes
// them at the caret the page shares with it; the page takes the change
bool pasteMore(App& app, HWND hwnd, LiveEditor& L) {
    std::wstring clip = IsClipboardFormatAvailable(CF_UNICODETEXT) ? readClipboardText(hwnd) : std::wstring();
    bool range = clip.find(L'\t') != std::wstring::npos && clip.find(L'\n') != std::wstring::npos;
    if (!clip.empty() && !range) return false;
    editorPasteClipboard(app, hwnd, false);
    sync(app, L);
    return true;
}

live::Host hostFor(App& app, HWND hwnd, LiveEditor& L) {
    live::Host host;
    host.caretPoint = [&L](float& x, float& y, float& height) {
        if (!L.placed || !live::caretPoint(L.layout, L.from, L.e.caret, x, y, height)) return false;
        x += L.textX;
        y += L.textY;
        return true;
    };
    host.positionAt = [&app, &L](float x, float y, int direction) { return positionAtShown(app, L, x, y, direction); };
    host.names = [&app, &L](const std::wstring& typed) { return noteNames(app, L, typed); };
    host.pickImage = [&app, hwnd]() { return pickImage(app, hwnd); };
    host.undo = [&app, &L](bool redo) {
        editorUndoRedo(app, redo);
        sync(app, L);  // the text and the caret as the source's undo left them
    };
    // A link is followed once the menu's edit is in the source
    host.openLink = [&L](const std::string& url) { L.followLink = url; };
    host.paste = [&app, hwnd, &L]() { return pasteMore(app, hwnd, L); };
    return host;
}

// After every edit and move: the source takes the change as an undo step
// and the caret; leaving a block that was typed in lays the page out again
void finish(App& app, HWND hwnd, LiveEditor& L) {
    L.caretSeen = (size_t)-1;
    std::vector<LiveText::Change> changes;
    changes.swap(L.e.changes);
    for (const auto& c : changes) noteChange(L, c.at, c.removed, c.inserted.size());
    editorApplyLiveEdits(app, changes, L.e.caret, L.e.anchor);
    sync(app, L);
    place(app, L);
    L.reveal = true;
    app.cursorBlinkOn = true;
    InvalidateRect(hwnd, nullptr, FALSE);
}

// A new tab's empty page keeps its Open and New sheet buttons
bool onQuickNoteButton(const App& app, int x, int y) {
    if (!quickNoteEmptyStateActive(app)) return false;
    for (const D2D1_RECT_F& r : {app.quickNoteButtonRect, app.quickNoteSheetRect}) {
        if (r.right > r.left && inside(r, (float)x, (float)y)) return true;
    }
    return false;
}

LiveEditor& begin(App& app) {
    LiveEditor& L = state(app);
    // A menu stays with the document it was opened on
    if (L.e.menu.open && L.menuFile != app.currentFile) live::closeMenu(L.e);
    sync(app, L);
    place(app, L);
    return L;
}

// The menu's page: the window beside the rail, below the tab strip
D2D1_RECT_F menuBounds(const App& app) {
    return D2D1::RectF(0, chromeTopHeight(app), (float)app.width - documentViewportX(app), (float)app.height);
}

// After the menu took the pointer or a key: an edit goes into the source,
// then a link it asked for is followed
void afterMenu(App& app, HWND hwnd, LiveEditor& L, live::MenuResult result) {
    if (result == live::MenuResult::Ran) {
        finish(app, hwnd, L);
        if (!L.followLink.empty()) {
            std::string url;
            url.swap(L.followLink);
            followDocumentLink(app, hwnd, url);
        }
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

}  // namespace

// --- The page ---------------------------------------------------------------------

void liveTextParsed(App& app) {
    LiveEditor& L = state(app);
    L.parsed = app.editorText;
    L.parsedLines = app.editorLineStarts;
    L.parsedBytes = app.editorLineByteOffsets;
    L.parsedRoot = app.root.get();
    if (L.e.text != app.editorText) {
        L.e.text = app.editorText;
        live::closePopup(L.e);
    }
    L.dirty = false;
    L.delta = 0;
    L.blocks.clear();
    L.spansSeen = (size_t)-1;
}

float livePageHeight(const App& app) {
    const LiveEditor& L = state(app);
    return app.contentHeight + (L.placed ? std::max(0.0f, L.shift) : 0.0f);
}

void renderLivePage(App& app) {
    LiveEditor& L = begin(app);
    if (L.caretSeen != (size_t)-1 && L.caretSeen != L.e.caret) L.reveal = true;
    L.caretSeen = L.e.caret;
    if (L.wantCaretY >= 0 || L.reveal) settleScroll(app, L);
    const float sx = app.scrollX, sy = app.scrollY, h = (float)app.height;
    ID2D1DeviceContext* dc = app.deviceContext;
    // The page's own shrink bands are print's; the screen has none
    std::vector<App::PrintShrinkBand> bands;
    bands.swap(app.printShrinkBands);
    // A stretch of the page, moved down by `shift`, clipped to itself
    auto slice = [&](float from, float to, float shift) {
        float top = std::max(from, sy - shift), bottom = std::min(to, sy + h - shift);
        if (bottom <= top || !dc) return;
        app.renderTarget->PushAxisAlignedClip(D2D1::RectF(-1.0e5f, std::max(0.0f, from + shift - sy), 1.0e5f, std::min(h, to + shift - sy)),
                                              D2D1_ANTIALIAS_MODE_ALIASED);
        drawLayoutRange(app, dc, app.brush, top, bottom, -sx, top + shift - sy, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        app.renderTarget->PopAxisAlignedClip();
    };
    if (!L.placed) {
        slice(0, 1.0e9f, 0);
        bands.swap(app.printShrinkBands);
        return;
    }
    slice(0, L.top, 0);
    slice(L.bottom, 1.0e9f, L.shift);
    live::Look look = live::pageLook(app);
    // The stretch being typed in: a faint wash, then its text
    float washLeft = std::max(dpi(app, 10.0f), app.layoutIndent - dpi(app, 12.0f));
    float washRight = app.layoutIndent + app.layoutMaxWidth + dpi(app, 12.0f);
    paint::fillRound(app, D2D1::RectF(washLeft - sx, L.textY - dpi(app, 6.0f) - sy, washRight - sx, L.textY + L.height + dpi(app, 6.0f) - sy),
                     dpi(app, 6.0f), paint::alpha(app.theme.accent, 0.05f));
    D2D1_MATRIX_3X2_F saved;
    app.renderTarget->GetTransform(&saved);
    app.renderTarget->SetTransform(D2D1::Matrix3x2F::Translation(L.textX - sx, L.textY - sy) * saved);
    live::draw(app, L.e, L.layout, L.region, L.from, look, app.cursorBlinkOn, 1.0f);
    app.renderTarget->SetTransform(saved);
    // Under a diagram's, a formula's or a picture's Markdown, what the page
    // drew for it, caught up after each pause in the typing
    if (L.preview) slice(L.previewTop, L.previewBottom, L.previewY - L.previewTop);
    app.renderTarget->SetTransform(D2D1::Matrix3x2F::Translation(L.textX - sx, L.textY - sy) * saved);
    // An empty document says how to begin
    if (L.e.text.empty()) {
        IDWriteTextLayout* hint = paint::makeLayout(app, tr(app, "live.placeholder"),
                                                    paint::textFormat(app, look.face.c_str(), look.size, DWRITE_FONT_WEIGHT_NORMAL, false),
                                                    L.width, look.size * 3);
        paint::drawText(app, hint, 2.0f, 0, paint::alpha(look.text, 0.4f));
        if (hint) hint->Release();
    }
    app.renderTarget->SetTransform(saved);
    // The slash menu or the [[ names under the caret, or the format bar over a selection
    L.e.popupHits.clear();
    L.e.barHits.clear();
    D2D1_RECT_F bounds = D2D1::RectF(0, chromeTopHeight(app), (float)app.width - documentViewportX(app), h);
    auto at = [&](size_t pos, float& x, float& top, float& bottom) {
        float cx = 0, cy = 0, ch = 0;
        if (!live::caretPoint(L.layout, L.from, std::clamp(pos, L.from, L.to), cx, cy, ch)) return false;
        x = L.textX + cx - sx;
        top = L.textY + cy - sy;
        bottom = top + ch;
        return true;
    };
    float x = 0, top = 0, bottom = 0;
    if (L.e.popup != LiveText::Popup::None) {
        if (at(L.e.popupFrom, x, top, bottom)) live::drawPopup(app, L.e, D2D1::Point2F(x, top), bottom, bounds);
    } else if (L.e.hasSelection() && !L.dragging && at(L.e.selStart(), x, top, bottom)) {
        float x2 = 0, top2 = 0, bottom2 = 0;
        at(L.e.selEnd(), x2, top2, bottom2);
        live::drawFormatBar(app, L.e, D2D1::Point2F(x, top), bottom2, bounds);
    }
    bands.swap(app.printShrinkBands);
}

// --- Switching ----------------------------------------------------------------------

void setEditorLive(App& app, HWND hwnd, bool live) {
    if (app.editorLive == live || (live && app.editMode && app.editorRawDocument)) return;
    LiveEditor& L = state(app);
    // The caret keeps its height in the window
    float caretY = -1;
    if (live) {
        D2D1_POINT_2F p{};
        if (app.editMode && !app.editorReadingPreview && editorCaretPoint(app, p)) caretY = p.y;
    } else {
        D2D1_POINT_2F top{};
        float height = 0;
        if (liveCaretClientPoint(app, top, height)) caretY = top.y;
    }
    app.editorLive = live;
    persistEditorMode(app);
    app.editorExtraCarets.clear();
    app.hoveredCodeBlock = -1;
    app.hoveredLink.clear();
    app.hasSelection = false;
    app.selecting = false;
    app.clearEditorLineLayoutCache();
    app.editorRowMetricsWidth = -1.0f;
    L.placed = false;
    L.dragging = false;
    live::closePopup(L.e);
    live::closeMenu(L.e);
    if (live) {
        L.wantCaretY = caretY;
        editorReparse(app, true);
    } else {
        editorRevealCaret(app);
        D2D1_POINT_2F p{};
        if (caretY >= 0 && editorCaretPoint(app, p)) {
            float editorMax = std::max(0.0f, app.editorContentHeight - (float)app.height);
            app.editorScrollY = std::clamp(app.editorScrollY + p.y - caretY, 0.0f, std::max(editorMax, app.editorScrollY + p.y - caretY));
            app.editorScrollY = std::max(0.0f, app.editorScrollY);
        }
    }
    app.layoutDirty = true;
    signalHintKey(app, SIGI_EYE, live ? "rail.live" : "rail.source");
    InvalidateRect(hwnd, nullptr, FALSE);
}

// --- Keys ---------------------------------------------------------------------------

bool liveKeyDown(App& app, HWND hwnd, WPARAM key, bool& consumed) {
    consumed = false;
    // The unsaved-changes dialog and the find bar keep their keys
    if (!editorLiveOn(app) || app.confirmExitPending || (app.showSearch && app.searchActive)) return false;
    // The insert menu closes on a key, as in the source
    if (app.editCtxOpen) {
        closeEditCtxMenu(app);
        InvalidateRect(hwnd, nullptr, FALSE);
        if (key == VK_ESCAPE) {
            consumed = true;
            return true;
        }
    }
    bool ctrl = keyDown(VK_CONTROL), shift = keyDown(VK_SHIFT);
    // Ctrl+/ shows the source; Ctrl+E the source with its preview beside it
    if (ctrl && key == VK_OEM_2) {
        setEditorLive(app, hwnd, false);
        consumed = true;
        return true;
    }
    if (ctrl && !shift && key == 'E') {
        app.editorShowPreview = true;
        setEditorLive(app, hwnd, false);
        consumed = true;
        return true;
    }
    // One caret, and lines that always wrap
    if (ctrl && (key == 'D' || key == 'W')) {
        consumed = true;
        return true;
    }
    LiveEditor& L = begin(app);
    if (ctrl && key == 'V' && !shift) {
        // A picture saves beside the document and a spreadsheet's range
        // becomes a table, as in the source; text pastes here
        std::wstring clip = IsClipboardFormatAvailable(CF_UNICODETEXT) ? readClipboardText(hwnd) : std::wstring();
        if (clip.empty() || (clip.find(L'\t') != std::wstring::npos && clip.find(L'\n') != std::wstring::npos)) return false;
    }
    if (ctrl && key == 'K') {
        live::makeLink(L.e);
        finish(app, hwnd, L);
        consumed = true;
        return true;
    }
    if (key == VK_ESCAPE) {
        // The popup first, then a selection; then Esc is the editor's
        if (L.e.popup != LiveText::Popup::None) {
            live::closePopup(L.e);
        } else if (L.e.hasSelection()) {
            L.e.anchor = L.e.caret;
            finish(app, hwnd, L);
        } else {
            return false;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        consumed = true;
        return true;
    }
    if (key == VK_PRIOR || key == VK_NEXT) {
        // A screen up or down, the caret along at its place in the window
        float x = 0, y = 0, height = 0;
        if (L.placed && live::caretPoint(L.layout, L.from, L.e.caret, x, y, height)) {
            float step = ((float)app.height - chromeTopHeight(app)) * 0.85f * (key == VK_NEXT ? 1.0f : -1.0f);
            float before = app.scrollY;
            app.scrollY = app.targetScrollY = std::clamp(app.scrollY + step, 0.0f, maxScroll(app));
            if (app.scrollY == before) step = key == VK_NEXT ? 1.0e6f : -1.0e6f;
            size_t at = positionAtShown(app, L, L.textX + x, L.textY + y + step, key == VK_NEXT ? 1 : -1);
            if (step >= 1.0e6f) at = L.e.text.size();
            if (step <= -1.0e6f) at = 0;
            L.e.caret = at;
            if (!shift) L.e.anchor = at;
        }
        finish(app, hwnd, L);
        consumed = true;
        return true;
    }
    if (ctrl && key == VK_RETURN && L.e.popup == LiveText::Popup::None) {
        // A new line below this one, wherever the caret is in it
        L.e.caret = L.e.anchor = live::lineEnd(L.e.text, L.e.caret);
        live::newline(L.e, false);
        finish(app, hwnd, L);
        consumed = true;
        return true;
    }
    live::Result r = live::key(app, hwnd, L.e, key, ctrl, shift, hostFor(app, hwnd, L));
    if (r == live::Result::Unhandled || r == live::Result::Cancel) return false;
    finish(app, hwnd, L);
    consumed = true;
    return true;
}

bool liveAltArrow(App& app, HWND hwnd, WPARAM key) {
    if (!editorLiveOn(app) || (key != VK_UP && key != VK_DOWN)) return false;
    if (!keyDown(VK_MENU) || keyDown(VK_SHIFT) || keyDown(VK_CONTROL)) return false;
    LiveEditor& L = begin(app);
    live::moveLines(L.e, key == VK_UP);
    finish(app, hwnd, L);
    return true;
}

void liveCharInput(App& app, HWND hwnd, wchar_t ch) {
    LiveEditor& L = begin(app);
    live::character(app, L.e, ch, hostFor(app, hwnd, L));
    finish(app, hwnd, L);
}

bool liveRailCommand(App& app, HWND hwnd, int id) {
    LiveEditor& L = begin(app);
    switch (id) {
        case 1: live::applyFormat(L.e, live::FMT_BOLD); break;
        case 2: live::applyFormat(L.e, live::FMT_ITALIC); break;
        case 3: live::applyFormat(L.e, live::FMT_STRIKE); break;
        case 4: live::applyFormat(L.e, live::FMT_CODE); break;
        case 5: live::applyFormat(L.e, live::FMT_LINK); break;
        case 10: live::toggleLineMark(L.e, live::LineMark::Bullet); break;
        case 11: live::toggleLineMark(L.e, live::LineMark::Task); break;
        case 12: live::toggleLineMark(L.e, live::LineMark::Quote); break;
        case 20:
        case 21: {
            // The table grid and the diagrams open beside their buttons, as the menu's submenus
            float y = chromeTopHeight(app) + dpi(app, 60.0f);
            for (const auto& hit : app.editRailHits) {
                if (hit.second == id) y = hit.first.top;
            }
            closeEditCtxMenu(app);
            live::openMenuPanel(app, L.e, editRailWidth(app) - documentViewportX(app) + dpi(app, 6.0f), y,
                                id == 20 ? live::SUB_TABLE : live::SUB_DIAGRAM, menuBounds(app), hostFor(app, hwnd, L));
            L.menuFile = app.currentFile;
            InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        case 22: live::menuCommand(app, hwnd, L.e, live::CMD_INSERT + live::INS_IMAGE, hostFor(app, hwnd, L)); break;
        default: return false;
    }
    finish(app, hwnd, L);
    return true;
}

// --- The pointer -------------------------------------------------------------------

bool liveMouseDown(App& app, HWND hwnd, int x, int y) {
    float vx = documentViewportX(app);
    if (!editorLiveOn(app) || (float)x < vx || (float)y < chromeTopHeight(app) || onQuickNoteButton(app, x, y)) return false;
    LiveEditor& L = begin(app);
    float px = (float)x - vx, py = (float)y;
    int hit = live::hitAt(L.e, px, py);
    if (hit >= 1000) {
        live::applyFormat(L.e, hit - 1000);
        finish(app, hwnd, L);
        app.swallowNextMouseUp = true;
        return true;
    }
    if (hit >= 0) {
        live::pickChoice(app, L.e, hit, false, hostFor(app, hwnd, L));
        finish(app, hwnd, L);
        app.swallowNextMouseUp = true;
        return true;
    }
    if (L.e.popup != LiveText::Popup::None) live::closePopup(L.e);
    float gx = px + app.scrollX, gy = py + app.scrollY;
    bool shift = keyDown(VK_SHIFT);
    // Ctrl+click follows a link; a task box ticks without moving the caret
    if (keyDown(VK_CONTROL)) {
        std::string url = linkAt(app, L, gx, gy);
        if (!url.empty()) {
            app.swallowNextMouseUp = true;
            followDocumentLink(app, hwnd, url);
            return true;
        }
    }
    if (!shift) {
        size_t box = taskAt(app, L, gx, gy);
        if (box != (size_t)-1 && box < L.e.text.size()) {
            size_t caret = L.e.caret, anchor = L.e.anchor;
            live::pushUndo(L.e, false);
            live::splice(L.e, box, 1, L.e.text[box] == L' ' ? L"x" : L" ");
            L.e.caret = caret;
            L.e.anchor = anchor;
            finish(app, hwnd, L);
            app.swallowNextMouseUp = true;
            return true;
        }
    }
    size_t at = positionAtShown(app, L, gx, gy, 0);
    unsigned long long now = GetTickCount64();
    bool again = now - L.e.lastClickMs < GetDoubleClickTime() && std::fabs(px - L.pressX) < dpi(app, 4.0f) &&
                 std::fabs(py - L.pressY) < dpi(app, 4.0f);
    L.e.clickCount = again ? L.e.clickCount + 1 : 1;
    L.e.lastClickMs = now;
    L.pressX = px;
    L.pressY = py;
    const std::wstring& t = L.e.text;
    if (L.e.clickCount == 2) {
        L.e.anchor = live::wordLeft(t, std::min(at + 1, t.size()));
        L.e.caret = live::wordRight(t, at);
    } else if (L.e.clickCount >= 3) {
        L.e.anchor = live::lineStart(t, at);
        L.e.caret = live::lineEnd(t, at);
    } else {
        L.e.caret = at;
        if (!shift) L.e.anchor = at;
    }
    L.e.preferredX = -1;
    L.e.selectingWithMouse = true;
    L.dragging = true;
    SetCapture(hwnd);
    finish(app, hwnd, L);
    return true;
}

bool liveMouseMove(App& app, HWND hwnd, int x, int y) {
    if (!editorLiveOn(app)) return false;
    LiveEditor& L = state(app);
    float vx = documentViewportX(app);
    static HCURSOR beam = LoadCursor(nullptr, IDC_IBEAM);
    static HCURSOR hand = LoadCursor(nullptr, IDC_HAND);
    static HCURSOR arrow = LoadCursor(nullptr, IDC_ARROW);
    if (L.dragging) {
        sync(app, L);
        place(app, L);
        float top = chromeTopHeight(app), h = (float)app.height;
        // Past the window's edges the page scrolls along
        if ((float)y < top) app.scrollY = std::max(0.0f, app.scrollY - (top - (float)y) * 0.5f);
        else if ((float)y > h) app.scrollY = std::min(maxScroll(app), app.scrollY + ((float)y - h) * 0.5f);
        app.targetScrollY = app.scrollY;
        float gx = (float)x - vx + app.scrollX, gy = std::clamp((float)y, top, h) + app.scrollY;
        size_t at = positionAtShown(app, L, gx, gy, 0);
        const std::wstring& t = L.e.text;
        if (L.e.clickCount == 2) L.e.caret = at >= L.e.anchor ? live::wordRight(t, at) : live::wordLeft(t, std::min(at + 1, t.size()));
        else if (L.e.clickCount >= 3) L.e.caret = at >= L.e.anchor ? live::lineEnd(t, at) : live::lineStart(t, at);
        else L.e.caret = at;
        finish(app, hwnd, L);
        SetCursor(beam);
        return true;
    }
    if ((float)x < vx || (float)y < chromeTopHeight(app) || onQuickNoteButton(app, x, y)) return false;
    int hit = live::hitAt(L.e, (float)x - vx, (float)y);
    if (hit != L.e.hoverHit) {
        L.e.hoverHit = hit;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (hit >= 0) {
        SetCursor(arrow);
        return true;
    }
    float gx = (float)x - vx + app.scrollX, gy = (float)y + app.scrollY;
    if ((keyDown(VK_CONTROL) && !linkAt(app, L, gx, gy).empty()) || taskAt(app, L, gx, gy) != (size_t)-1) SetCursor(hand);
    else SetCursor(beam);
    return true;
}

void liveCaptureLost(App& app) {
    LiveEditor& L = state(app);
    L.dragging = false;
    L.e.selectingWithMouse = false;
}

bool liveMouseUp(App& app, HWND hwnd, int, int) {
    LiveEditor& L = state(app);
    if (!L.dragging) return false;
    L.dragging = false;
    L.e.selectingWithMouse = false;
    if (GetCapture() == hwnd) ReleaseCapture();
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

// --- The menu ---------------------------------------------------------------------

bool liveMenuOpen(const App& app) { return editorLiveOn(app) && state(app).e.menu.open; }

void liveContextMenu(App& app, HWND hwnd, int x, int y, bool byKeys) {
    closeEditCtxMenu(app);
    LiveEditor& L = begin(app);
    live::closePopup(L.e);
    const float vx = documentViewportX(app);
    size_t at = L.e.caret;
    float px = 0, top = 0, bottom = 0;
    if (!byKeys) {
        // The caret goes where the click was, unless that is in the selection
        px = (float)x - vx;
        top = bottom = (float)y;
        at = positionAtShown(app, L, px + app.scrollX, (float)y + app.scrollY, 0);
        if (!(L.e.hasSelection() && at >= L.e.selStart() && at <= L.e.selEnd())) {
            L.e.caret = L.e.anchor = at;
            L.e.preferredX = -1;
            finish(app, hwnd, L);
        }
    } else {
        // From the keys: under the caret
        float cx = 0, cy = 0, ch = 0;
        if (L.placed && live::caretPoint(L.layout, L.from, L.e.caret, cx, cy, ch)) {
            px = L.textX + cx - app.scrollX;
            top = L.textY + cy - app.scrollY;
            bottom = top + ch;
        } else {
            px = dpi(app, 40.0f);
            top = bottom = chromeTopHeight(app) + dpi(app, 40.0f);
        }
    }
    live::openMenu(app, L.e, px, top, bottom, at, menuBounds(app), hostFor(app, hwnd, L), byKeys);
    L.menuFile = app.currentFile;
    InvalidateRect(hwnd, nullptr, FALSE);
}

bool liveMenuMouseDown(App& app, HWND hwnd, int x, int y) {
    if (!liveMenuOpen(app)) return false;
    LiveEditor& L = begin(app);
    live::MenuResult result = live::menuPress(app, hwnd, L.e, (float)x - documentViewportX(app), (float)y, hostFor(app, hwnd, L));
    app.swallowNextMouseUp = true;
    afterMenu(app, hwnd, L, result);
    return true;
}

bool liveMenuMouseMove(App& app, HWND hwnd, int x, int y) {
    if (!liveMenuOpen(app)) return false;
    LiveEditor& L = state(app);
    if (live::menuHover(app, L.e, (float)x - documentViewportX(app), (float)y)) InvalidateRect(hwnd, nullptr, FALSE);
    static HCURSOR arrow = LoadCursor(nullptr, IDC_ARROW);
    SetCursor(arrow);
    return true;
}

bool liveMenuKey(App& app, HWND hwnd, WPARAM key) {
    if (!liveMenuOpen(app)) return false;
    LiveEditor& L = begin(app);
    afterMenu(app, hwnd, L, live::menuKey(app, hwnd, L.e, key, hostFor(app, hwnd, L)));
    return true;
}

void liveCloseMenu(App& app) {
    LiveEditor& L = state(app);
    if (!L.e.menu.open) return;
    live::closeMenu(L.e);
    if (app.hwnd) InvalidateRect(app.hwnd, nullptr, FALSE);
}

void renderLiveMenu(App& app) {
    if (!liveMenuOpen(app)) return;
    LiveEditor& L = state(app);
    D2D1_MATRIX_3X2_F saved;
    app.renderTarget->GetTransform(&saved);
    app.renderTarget->SetTransform(D2D1::Matrix3x2F::Translation(documentViewportX(app), 0) * saved);
    live::drawMenu(app, L.e);
    app.renderTarget->SetTransform(saved);
}

const LiveText& liveText(const App& app) { return state(app).e; }

bool livePageGeometry(const App& app, float& top, float& bottom, float& shift) {
    const LiveEditor& L = state(app);
    top = L.top;
    bottom = L.liveBottom;
    shift = L.shift;
    return L.placed;
}

bool liveCaretClientPoint(App& app, D2D1_POINT_2F& top, float& height) {
    LiveEditor& L = state(app);
    float x = 0, y = 0, h = 0;
    if (!L.placed || !editorLiveOn(app) || !live::caretPoint(L.layout, L.from, L.e.caret, x, y, h)) return false;
    top = D2D1::Point2F(documentViewportX(app) + L.textX + x - app.scrollX, L.textY + y - app.scrollY);
    height = h;
    return true;
}
