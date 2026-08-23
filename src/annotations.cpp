#include "annotations.h"

#include "editor.h"
#include "i18n.h"
#include "input.h"
#include "render.h"
#include "selection.h"
#include "utils.h"

#include <algorithm>
#include <cwctype>
#include <fstream>

namespace {

constexpr size_t kNone = (size_t)-1;
const char kCommentOpen[] = "<!-- review \"";
const wchar_t kElision[] = L" ... ";

// The review hue is deliberately theme-independent so annotations read the
// same across themes: a warm marker red, softened on dark backgrounds
D2D1_COLOR_F annotColor(const App& app) {
    return app.theme.isDark ? hexColor(0xE06A50) : hexColor(0xC2401A);
}

std::wstring collapseWs(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    bool pendingSpace = false;
    for (wchar_t c : text) {
        if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') {
            if (!out.empty()) pendingSpace = true;
        } else {
            if (pendingSpace) out += L' ';
            pendingSpace = false;
            out += c;
        }
    }
    return out;
}

// Whitespace-tolerant find: any whitespace run on either side matches a
// single space on the other. The needle must already be collapsed.
size_t normFind(const std::wstring& hay, const std::wstring& needle,
                size_t from, size_t* endOut) {
    if (needle.empty() || hay.empty()) return kNone;
    auto isWs = [](wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r';
    };
    for (size_t i = from; i < hay.size(); i++) {
        if (hay[i] != needle[0]) continue;
        size_t h = i, n = 0;
        while (n < needle.size() && h < hay.size()) {
            if (needle[n] == L' ') {
                if (!isWs(hay[h])) break;
                while (h < hay.size() && isWs(hay[h])) h++;
                n++;
            } else if (hay[h] == needle[n]) {
                h++;
                n++;
            } else {
                break;
            }
        }
        if (n == needle.size()) {
            if (endOut) *endOut = h;
            return i;
        }
    }
    return kNone;
}

// Long anchors store only the edges: first five words ... last five words
std::wstring elideQuote(const std::wstring& collapsed) {
    std::vector<size_t> starts;
    bool inWord = false;
    for (size_t i = 0; i < collapsed.size(); i++) {
        bool ws = collapsed[i] == L' ';
        if (!ws && !inWord) starts.push_back(i);
        inWord = !ws;
    }
    if (starts.size() <= 12) return collapsed;
    size_t headEnd = starts[5] - 1;                 // space before word 6
    size_t tailStart = starts[starts.size() - 5];
    return collapsed.substr(0, headEnd) + kElision + collapsed.substr(tailStart);
}

// Resolve the (possibly elided) quote to a docText range
bool findQuote(const App& app, const std::string& quoteUtf8,
               size_t& startOut, size_t& endOut) {
    std::wstring needle = collapseWs(toWide(quoteUtf8));
    if (needle.empty()) return false;
    size_t cut = needle.find(kElision);
    if (cut == std::wstring::npos) {
        size_t end = 0;
        size_t start = normFind(app.docText, needle, 0, &end);
        if (start == kNone) return false;
        startOut = start;
        endOut = end;
        return true;
    }
    std::wstring head = needle.substr(0, cut);
    std::wstring tail = needle.substr(cut + wcslen(kElision));
    if (head.empty() || tail.empty()) return false;
    size_t headEnd = 0;
    size_t start = normFind(app.docText, head, 0, &headEnd);
    if (start == kNone) return false;
    size_t tailEnd = 0;
    size_t tailStart = normFind(app.docText, tail, headEnd, &tailEnd);
    if (tailStart == kNone || tailEnd > headEnd + 4000) return false;
    startOut = start;
    endOut = tailEnd;
    return true;
}

int lineOfOffset(const std::string& src, size_t offset) {
    int line = 1;
    for (size_t i = 0; i < offset && i < src.size(); i++) {
        if (src[i] == '\n') line++;
    }
    return line;
}

bool lineIsBlank(const std::string& src, size_t lineStart, size_t lineEnd) {
    for (size_t i = lineStart; i < lineEnd; i++) {
        if (src[i] != ' ' && src[i] != '\t' && src[i] != '\r') return false;
    }
    return true;
}

bool lineIsComment(const std::string& src, size_t lineStart) {
    size_t i = lineStart;
    while (i < src.size() && (src[i] == ' ' || src[i] == '\t')) i++;
    return src.compare(i, 4, "<!--") == 0;
}

// The block of prose immediately above the comment: its line range is what
// the agent gets as "where this was selected"
void blockAbove(const std::string& src, size_t commentPos,
                int& fromOut, int& toOut) {
    size_t lineStart = src.rfind('\n', commentPos ? commentPos - 1 : 0);
    lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
    int commentLine = lineOfOffset(src, lineStart);
    int from = commentLine, to = commentLine;
    size_t end = lineStart;  // exclusive end of the line above
    int line = commentLine;
    while (end > 0) {
        size_t prevStart = src.rfind('\n', end >= 2 ? end - 2 : 0);
        prevStart = (prevStart == std::string::npos || end < 2) ? 0 : prevStart + 1;
        line--;
        if (lineIsBlank(src, prevStart, end - 1 >= prevStart ? end - 1 : prevStart)) break;
        if (!lineIsComment(src, prevStart)) {
            if (to == commentLine) to = line;
            from = line;
        }
        if (prevStart == 0) break;
        end = prevStart;
    }
    if (to == commentLine) {  // nothing above: report the comment's own line
        from = to = commentLine;
    }
    fromOut = from;
    toOut = to;
}

bool usesCrLf(const std::string& src) {
    return src.find("\r\n") != std::string::npos;
}

void sanitizeForComment(std::string& text) {
    size_t pos = 0;
    while ((pos = text.find("-->", pos)) != std::string::npos) {
        text.replace(pos, 3, "-- >");
    }
}

void writeSourceAndReload(App& app, HWND hwnd, const std::string& src) {
    if (app.currentFile.empty()) return;
    std::ofstream out(toWide(app.currentFile), std::ios::binary | std::ios::trunc);
    if (!out) return;
    out.write(src.data(), (std::streamsize)src.size());
    out.close();
    handleFileWatchTimer(app, hwnd);  // reload now instead of on the timer
}

// Markdown source line reduced to roughly its rendered text, so a viewer
// selection (which comes from laid-out text) can be located in the source
std::wstring stripInline(const std::wstring& line) {
    std::wstring out;
    out.reserve(line.size());
    size_t i = 0;
    // Block prefixes: heading hashes, quote markers, list bullets, numbers
    while (i < line.size() && (line[i] == L' ' || line[i] == L'\t')) i++;
    while (i < line.size() && (line[i] == L'#' || line[i] == L'>')) {
        i++;
        while (i < line.size() && line[i] == L' ') i++;
    }
    if (i + 1 < line.size() &&
        (line[i] == L'-' || line[i] == L'*' || line[i] == L'+') &&
        line[i + 1] == L' ') {
        i += 2;
        // Task marker after the bullet
        if (i + 2 < line.size() && line[i] == L'[' && line[i + 2] == L']') i += 3;
    } else {
        size_t d = i;
        while (d < line.size() && iswdigit(line[d])) d++;
        if (d > i && d < line.size() && (line[d] == L'.' || line[d] == L')') &&
            d + 1 < line.size() && line[d + 1] == L' ') {
            i = d + 2;
        }
    }
    for (; i < line.size(); i++) {
        wchar_t c = line[i];
        if (c == L'*' || c == L'_' || c == L'`' || c == L'~' || c == L'=') {
            continue;  // emphasis / code / strike / highlight markers
        }
        if (c == L'|') {
            out += L' ';  // table cell separators become spaces
            continue;
        }
        if (c == L'[') {
            if (i + 1 < line.size() && line[i + 1] == L'[') { i++; continue; }
            continue;  // link text opens; keep the text
        }
        if (c == L']') {
            if (i + 1 < line.size() && line[i + 1] == L']') { i++; continue; }
            // Skip a following (url) target
            if (i + 1 < line.size() && line[i + 1] == L'(') {
                size_t close = line.find(L')', i + 1);
                if (close != std::wstring::npos) i = close;
            }
            continue;
        }
        out += c;
    }
    return out;
}

// Locate collapsed viewer text in the source; returns the 1-based line range
bool locateInSource(const std::string& src, const std::wstring& collapsed,
                    int& lineFrom, int& lineTo) {
    std::wstring stream;
    std::vector<int> lineNos;
    std::wstring wsrc = toWide(src);
    size_t lineStart = 0;
    int line = 1;
    for (size_t i = 0; i <= wsrc.size(); i++) {
        if (i == wsrc.size() || wsrc[i] == L'\n') {
            std::wstring raw = wsrc.substr(lineStart, i - lineStart);
            if (!raw.empty() && raw.back() == L'\r') raw.pop_back();
            std::wstring stripped = stripInline(raw);
            if (!stripped.empty()) {
                if (!stream.empty()) {
                    stream += L' ';
                    lineNos.push_back(line);
                }
                for (size_t k = 0; k < stripped.size(); k++) {
                    stream += stripped[k];
                    lineNos.push_back(line);
                }
            }
            lineStart = i + 1;
            line++;
        }
    }
    size_t end = 0;
    size_t start = normFind(stream, collapsed, 0, &end);
    if (start == kNone || end == 0) return false;
    lineFrom = lineNos[std::min(start, lineNos.size() - 1)];
    lineTo = lineNos[std::min(end - 1, lineNos.size() - 1)];
    return true;
}

// Source offset where a new comment line for a selection ending on
// `lineTo` belongs: after the last non-blank line of that block
size_t insertOffsetAfterBlock(const std::string& src, int lineTo) {
    size_t offset = 0;
    int line = 1;
    while (line < lineTo && offset < src.size()) {
        size_t nl = src.find('\n', offset);
        if (nl == std::string::npos) return src.size();
        offset = nl + 1;
        line++;
    }
    // offset = start of lineTo; walk forward over the rest of the block
    while (offset < src.size()) {
        size_t nl = src.find('\n', offset);
        size_t lineEnd = (nl == std::string::npos) ? src.size() : nl;
        if (lineIsBlank(src, offset, lineEnd)) return offset;
        if (nl == std::string::npos) return src.size();
        offset = nl + 1;
    }
    return src.size();
}

// ---- editor overlay geometry (single window, recomputed on demand) ----

struct EditorGeom {
    D2D1_RECT_F panel{};
    D2D1_RECT_F textArea{};
    D2D1_RECT_F okBtn{};
    D2D1_RECT_F cancelBtn{};
    D2D1_RECT_F trashBtn{};
    float textHeight = 0.0f;
};

EditorGeom editorGeom(const App& app) {
    EditorGeom g;
    float w = dpi(app, 400.0f);
    float pad = dpi(app, 14.0f);
    float viewX = documentViewportX(app);
    float viewW = documentViewportWidth(app);
    float x = viewX + std::max(0.0f, (viewW - w) / 2.0f);
    float y = chromeTopHeight(app) + dpi(app, 60.0f);

    float textH = dpi(app, 64.0f);
    if (app.dwriteFactory && app.folderBrowserFormat) {
        IDWriteTextLayout* layout = nullptr;
        const std::wstring& text = app.annotEditorText;
        app.dwriteFactory->CreateTextLayout(
            text.c_str(), (UINT32)text.size(), app.folderBrowserFormat,
            w - pad * 2, 10000.0f, &layout);
        if (layout) layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        if (layout) {
            DWRITE_TEXT_METRICS m{};
            layout->GetMetrics(&m);
            textH = std::max(textH, m.height + dpi(app, 8.0f));
            layout->Release();
        }
    }
    textH = std::min(textH, dpi(app, 240.0f));

    float headerH = dpi(app, 30.0f);
    float btnH = dpi(app, 26.0f);
    float btnRow = btnH + pad;
    float h = pad + headerH + textH + pad * 0.5f + btnRow;

    g.panel = D2D1::RectF(x, y, x + w, y + h);
    g.textArea = D2D1::RectF(x + pad, y + pad + headerH,
                             x + w - pad, y + pad + headerH + textH);
    float by = g.panel.bottom - pad - btnH + dpi(app, 4.0f);
    float bw = dpi(app, 30.0f);
    g.okBtn = D2D1::RectF(x + w - pad - bw, by, x + w - pad, by + btnH);
    g.cancelBtn = D2D1::RectF(g.okBtn.left - bw - dpi(app, 6.0f), by,
                              g.okBtn.left - dpi(app, 6.0f), by + btnH);
    if (app.annotEditorIndex >= 0) {
        g.trashBtn = D2D1::RectF(x + pad, by, x + pad + bw, by + btnH);
    }
    g.textHeight = textH;
    return g;
}

void closeEditor(App& app) {
    app.annotEditorOpen = false;
    app.annotEditorIndex = -1;
    app.annotEditorText.clear();
    app.annotEditorCaret = 0;
    app.annotPendingInsert = kNone;
    app.annotPendingQuote.clear();
}

std::wstring fullClipboardText(HWND hwnd) {
    std::wstring out;
    if (!OpenClipboard(hwnd)) return out;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* text = (const wchar_t*)GlobalLock(data)) {
            out = text;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    // Normalize line endings for the note buffer
    size_t pos = 0;
    while ((pos = out.find(L"\r\n", pos)) != std::wstring::npos) {
        out.replace(pos, 2, L"\n");
    }
    return out;
}

void showToast(App& app, const char* key) {
    app.copiedNotificationKey = key;
    app.showCopiedNotification = true;
    app.copiedNotificationStart = std::chrono::steady_clock::now();
    startNotificationTimer(app);
}

}  // namespace

void annotationsParseSource(App& app) {
    app.annotations.clear();
    app.hoveredAnnotation = -1;
    const std::string& src = app.sourceText;
    if (src.empty()) return;
    size_t pos = 0;
    while ((pos = src.find(kCommentOpen, pos)) != std::string::npos) {
        size_t qStart = pos + sizeof(kCommentOpen) - 1;
        size_t qEnd = src.find("\":", qStart);
        size_t cEnd = src.find("-->", qStart);
        if (qEnd == std::string::npos || cEnd == std::string::npos || qEnd > cEnd) {
            pos = (cEnd == std::string::npos) ? src.size() : cEnd + 3;
            continue;
        }
        App::Annotation a;
        a.quote = src.substr(qStart, qEnd - qStart);
        size_t nStart = qEnd + 2;
        size_t nEnd = cEnd;
        while (nStart < nEnd && src[nStart] == ' ') nStart++;
        while (nEnd > nStart &&
               (src[nEnd - 1] == ' ' || src[nEnd - 1] == '\n' ||
                src[nEnd - 1] == '\r' || src[nEnd - 1] == '\t')) {
            nEnd--;
        }
        a.note = src.substr(nStart, nEnd - nStart);
        a.commentStart = pos;
        a.commentEnd = cEnd + 3;
        a.commentLine = lineOfOffset(src, pos);
        blockAbove(src, pos, a.lineFrom, a.lineTo);
        size_t next = a.commentEnd;
        app.annotations.push_back(std::move(a));
        pos = next;
    }
}

void renderAnnotations(App& app) {
    app.annotationMarks.clear();
    app.annotCopyBtnRect = D2D1_RECT_F{};
    if (app.editMode || app.annotations.empty() || !app.renderTarget || !app.brush) {
        return;
    }

    // Re-arm anchor resolution whenever the laid-out text changes size
    // (chunked layout keeps growing docText after first paint)
    static size_t lastDocSize = kNone;
    if (app.docText.size() != lastDocSize) {
        lastDocSize = app.docText.size();
        for (auto& a : app.annotations) {
            if (a.docStart == kNone) a.anchorTried = false;
        }
    }
    for (auto& a : app.annotations) {
        if (!a.anchorTried) {
            a.anchorTried = true;
            findQuote(app, a.quote, a.docStart, a.docEnd);
        }
    }

    float viewX = documentViewportX(app);
    float viewW = documentViewportWidth(app);
    float topBound = chromeTopHeight(app);
    D2D1_COLOR_F color = annotColor(app);

    // Tint the anchored text and remember where each annotation sits on
    // screen (first fragment centers the rail square, last ends the leader)
    struct Anchor {
        float firstY = -1.0f;   // screen center of the first visible fragment
        float lastY = -1.0f;
        float lastRight = 0.0f; // screen x of the last fragment's right edge
        // Screen bounding box of the visible tint, for popup placement
        float boxLeft = 1e9f;
        float boxRight = -1e9f;
        float boxTop = 1e9f;
        float boxBottom = -1e9f;
        bool visible = false;
    };
    std::vector<Anchor> anchors(app.annotations.size());

    D2D1_COLOR_F tint = color;
    tint.a = 0.16f;
    for (size_t ai = 0; ai < app.annotations.size(); ai++) {
        const auto& a = app.annotations[ai];
        if (a.docStart == kNone || a.docEnd <= a.docStart) continue;
        // Word-granular textRects merge into one run per visual line so
        // the tint is continuous across spaces
        D2D1_RECT_F lineRun{};
        bool runOpen = false;
        auto flushRun = [&]() {
            if (!runOpen) return;
            runOpen = false;
            float sx0 = lineRun.left + viewX - app.scrollX;
            float sx1 = lineRun.right + viewX - app.scrollX;
            float sy0 = lineRun.top - app.scrollY;
            float sy1 = lineRun.bottom - app.scrollY;
            Anchor& an = anchors[ai];
            if (sy1 < topBound || sy0 > (float)app.height) return;
            if (!an.visible) an.firstY = (sy0 + sy1) * 0.5f;
            an.visible = true;
            an.lastY = (sy0 + sy1) * 0.5f;
            an.lastRight = sx1;
            an.boxLeft = std::min(an.boxLeft, sx0);
            an.boxRight = std::max(an.boxRight, sx1);
            an.boxTop = std::min(an.boxTop, sy0);
            an.boxBottom = std::max(an.boxBottom, sy1);
            app.brush->SetColor(tint);
            app.renderTarget->FillRectangle(
                D2D1::RectF(sx0 - 1, sy0, sx1 + 1, sy1), app.brush);
            app.drawCalls++;
        };
        for (const auto& trc : app.textRects) {
            size_t rs = trc.docStart, re = rs + trc.docLength;
            if (re <= a.docStart || rs >= a.docEnd || trc.docLength == 0) continue;
            D2D1_RECT_F frag;
            if (!selectionRangeRect(app, trc,
                                    std::max(rs, a.docStart),
                                    std::min(re, a.docEnd), frag)) {
                continue;
            }
            bool sameLine = runOpen &&
                            std::abs(frag.top - lineRun.top) < 2.0f &&
                            frag.left >= lineRun.left;
            if (sameLine) {
                lineRun.right = std::max(lineRun.right, frag.right);
                lineRun.bottom = std::max(lineRun.bottom, frag.bottom);
            } else {
                flushRun();
                lineRun = frag;
                runOpen = true;
            }
        }
        flushRun();
    }

    // Marker rail beside the scrollbar
    float sq = dpi(app, 10.0f);
    float railRight = viewX + viewW - dpi(app, 16.0f);
    float railLeft = railRight - sq;
    struct Mark {
        float y;
        int index;
        bool onText;   // square is level with visible tinted text
        float textY;
        float textRight;
    };
    std::vector<Mark> marks;
    marks.reserve(app.annotations.size());
    for (size_t ai = 0; ai < app.annotations.size(); ai++) {
        const auto& a = app.annotations[ai];
        const Anchor& an = anchors[ai];
        float y;
        bool onText = an.visible;
        if (an.visible) {
            y = an.firstY;
        } else {
            // Off-screen or unanchored: place by source-line ratio so the
            // rail still gives an overview of where the marks are
            int totalLines = std::max(1, lineOfOffset(app.sourceText,
                                                      app.sourceText.size()));
            float docY = ((float)a.commentLine / (float)totalLines) *
                         std::max(app.contentHeight, (float)app.height);
            y = docY - app.scrollY;
        }
        y = std::max(topBound + dpi(app, 34.0f) + sq,
                     std::min(y, (float)app.height - sq * 2.0f));
        marks.push_back({y, (int)ai, onText, an.lastY, an.lastRight});
    }
    std::sort(marks.begin(), marks.end(),
              [](const Mark& a, const Mark& b) {
                  if (a.y != b.y) return a.y < b.y;
                  return a.index < b.index;  // deterministic tie-break
              });
    float spacing = sq + dpi(app, 3.0f);
    float prevY = -1000.0f;
    for (auto& m : marks) {
        if (m.y < prevY + spacing) m.y = prevY + spacing;
        prevY = m.y;
    }
    // A cluster parked at the bottom edge stacks upward from it instead
    // of the overflow running off screen
    if (!marks.empty()) {
        float limit = (float)app.height - sq * 2.0f;
        for (size_t i = marks.size(); i-- > 0;) {
            if (marks[i].y > limit) marks[i].y = limit;
            limit = marks[i].y - spacing;
        }
    }

    for (const auto& m : marks) {
        bool hovered = app.hoveredAnnotation == m.index;
        // Leader line from the square toward the end of the tinted text
        if (m.onText) {
            D2D1_COLOR_F lead = color;
            lead.a = hovered ? 0.55f : 0.28f;
            app.brush->SetColor(lead);
            app.renderTarget->DrawLine(
                D2D1::Point2F(m.textRight + dpi(app, 4.0f), m.textY),
                D2D1::Point2F(railLeft - dpi(app, 3.0f), m.y),
                app.brush, 1.0f);
        }
        D2D1_COLOR_F fill = color;
        fill.a = m.onText ? (hovered ? 1.0f : 0.85f) : 0.35f;
        app.brush->SetColor(fill);
        float grow = hovered ? dpi(app, 1.5f) : 0.0f;
        D2D1_RECT_F rect = D2D1::RectF(railLeft - grow, m.y - sq * 0.5f - grow,
                                       railLeft + sq + grow,
                                       m.y + sq * 0.5f + grow);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(rect, 2, 2), app.brush);
        app.annotationMarks.push_back({rect, m.index, m.onText});
    }

    // Copy-for-agent button pinned above the rail: copy glyph + agent glyph
    {
        float bw = dpi(app, 46.0f);
        float bh = dpi(app, 24.0f);
        float bx = railRight - bw + sq;
        float by = topBound + dpi(app, 8.0f);
        D2D1_RECT_F btn = D2D1::RectF(bx, by, bx + bw, by + bh);
        bool hovered = app.mouseX >= btn.left && app.mouseX <= btn.right &&
                       app.mouseY >= btn.top && app.mouseY <= btn.bottom;
        D2D1_COLOR_F bg = app.theme.isDark ? hexColor(0x1E1E1E, 0.95f)
                                           : hexColor(0xF8F8F8, 0.95f);
        app.brush->SetColor(bg);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(btn, 5, 5), app.brush);
        D2D1_COLOR_F border = color;
        border.a = hovered ? 0.9f : 0.5f;
        app.brush->SetColor(border);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(btn, 5, 5), app.brush, 1.0f);

        D2D1_COLOR_F glyph = app.theme.text;
        glyph.a = hovered ? 1.0f : 0.75f;
        app.brush->SetColor(glyph);
        // Copy: two offset rectangles
        float g = dpi(app, 9.0f);
        float gx = bx + dpi(app, 7.0f);
        float gy = by + (bh - g - dpi(app, 3.0f)) / 2.0f;
        app.renderTarget->DrawRectangle(
            D2D1::RectF(gx + dpi(app, 3.0f), gy + dpi(app, 3.0f),
                        gx + dpi(app, 3.0f) + g, gy + dpi(app, 3.0f) + g),
            app.brush, 1.0f);
        app.brush->SetColor(bg);
        app.renderTarget->FillRectangle(
            D2D1::RectF(gx, gy, gx + g, gy + g), app.brush);
        app.brush->SetColor(glyph);
        app.renderTarget->DrawRectangle(
            D2D1::RectF(gx, gy, gx + g, gy + g), app.brush, 1.0f);
        // Agent: robot head with antenna and two eyes
        float hx = gx + g + dpi(app, 9.0f);
        float hw = dpi(app, 12.0f);
        float hh = dpi(app, 9.0f);
        float hy = by + (bh - hh) / 2.0f + dpi(app, 1.5f);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(hx, hy, hx + hw, hy + hh), 2, 2),
            app.brush, 1.0f);
        app.renderTarget->DrawLine(
            D2D1::Point2F(hx + hw / 2.0f, hy),
            D2D1::Point2F(hx + hw / 2.0f, hy - dpi(app, 3.0f)), app.brush, 1.0f);
        app.renderTarget->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(hx + hw / 2.0f, hy - dpi(app, 3.5f)),
                          dpi(app, 1.2f), dpi(app, 1.2f)),
            app.brush);
        float eye = dpi(app, 1.3f);
        app.renderTarget->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(hx + hw * 0.32f, hy + hh * 0.45f),
                          eye, eye), app.brush);
        app.renderTarget->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(hx + hw * 0.68f, hy + hh * 0.45f),
                          eye, eye), app.brush);
        app.annotCopyBtnRect = btn;
    }

    // Hover preview: quote header plus the note, anchored to the tinted
    // text itself; the rail-side spot is kept for parked squares whose
    // text is off-screen
    if (app.hoveredAnnotation >= 0 &&
        app.hoveredAnnotation < (int)app.annotations.size() &&
        !app.annotEditorOpen && app.folderBrowserFormat && app.dwriteFactory) {
        const auto& a = app.annotations[app.hoveredAnnotation];
        float w = dpi(app, 300.0f);
        float pad = dpi(app, 10.0f);
        std::wstring quote = toWide(a.quote);
        if (quote.size() > 48) quote = quote.substr(0, 48) + L"...";
        std::wstring note = toWide(a.note);
        if (note.empty()) note = L" ";

        IDWriteTextLayout* noteLayout = nullptr;
        app.dwriteFactory->CreateTextLayout(
            note.c_str(), (UINT32)note.size(), app.folderBrowserFormat,
            w - pad * 2 - dpi(app, 4.0f), 10000.0f, &noteLayout);
        if (noteLayout) noteLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        float noteH = dpi(app, 18.0f);
        if (noteLayout) {
            DWRITE_TEXT_METRICS m{};
            noteLayout->GetMetrics(&m);
            noteH = m.height;
        }
        noteH = std::min(noteH, dpi(app, 200.0f));
        float quoteH = dpi(app, 18.0f);
        float h = pad * 2 + quoteH + dpi(app, 4.0f) + noteH;

        float gap = dpi(app, 8.0f);
        float px, py;
        const Anchor& an = anchors[app.hoveredAnnotation];
        if (an.visible) {
            if (an.lastRight + gap + w <= railLeft - gap) {
                // Beside the end of the tinted text
                px = an.lastRight + gap;
                py = an.lastY - h * 0.5f;
            } else if (an.boxTop - h - gap >= topBound) {
                // No room to the right: above the section
                px = an.boxLeft;
                py = an.boxTop - h - gap;
            } else {
                // Pinned near the top of the window: below the section
                px = an.boxLeft;
                py = an.boxBottom + gap;
            }
            px = std::max(viewX + gap, std::min(px, railLeft - w - gap));
            py = std::max(topBound + dpi(app, 4.0f),
                          std::min(py, (float)app.height - h - gap));
        } else {
            // Parked square: beside the rail at the square's height
            float y = topBound + dpi(app, 40.0f);
            for (const auto& mark : app.annotationMarks) {
                if (mark.index == app.hoveredAnnotation) {
                    y = mark.square.top;
                    break;
                }
            }
            px = railLeft - w - dpi(app, 10.0f);
            py = std::min(y, (float)app.height - h - gap);
        }

        D2D1_RECT_F panel = D2D1::RectF(px, py, px + w, py + h);
        D2D1_COLOR_F bg = app.theme.isDark ? hexColor(0x1E1E1E, 0.97f)
                                           : hexColor(0xFCFCFA, 0.97f);
        app.brush->SetColor(bg);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(panel, 6, 6), app.brush);
        D2D1_COLOR_F border = color;
        border.a = 0.55f;
        app.brush->SetColor(border);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(panel, 6, 6), app.brush, 1.0f);
        app.brush->SetColor(color);
        app.renderTarget->FillRectangle(
            D2D1::RectF(px, py + 6, px + dpi(app, 3.0f), py + h - 6), app.brush);

        D2D1_COLOR_F quoteColor = app.theme.text;
        quoteColor.a = 0.55f;
        app.brush->SetColor(quoteColor);
        app.renderTarget->DrawText(
            quote.c_str(), (UINT32)quote.size(), app.folderBrowserFormat,
            D2D1::RectF(px + pad + dpi(app, 4.0f), py + pad,
                        px + w - pad, py + pad + quoteH),
            app.brush);
        if (noteLayout) {
            app.brush->SetColor(app.theme.text);
            app.renderTarget->DrawTextLayout(
                D2D1::Point2F(px + pad + dpi(app, 4.0f),
                              py + pad + quoteH + dpi(app, 4.0f)),
                noteLayout, app.brush);
            noteLayout->Release();
        }
    }
}

int annotationAtDocPoint(App& app, float docX, float docY) {
    if (app.editMode || app.annotations.empty()) return -1;
    const App::TextRect* trc = findTextRectAt(app, (int)docX, (int)docY);
    if (!trc || trc->docLength == 0) return -1;
    size_t rs = trc->docStart, re = rs + trc->docLength;
    for (size_t ai = 0; ai < app.annotations.size(); ai++) {
        const auto& a = app.annotations[ai];
        if (a.docStart == kNone) continue;
        if (re <= a.docStart || rs >= a.docEnd) continue;
        D2D1_RECT_F frag;
        if (selectionRangeRect(app, *trc, std::max(rs, a.docStart),
                               std::min(re, a.docEnd), frag) &&
            docX >= frag.left - 1 && docX <= frag.right + 1) {
            return (int)ai;
        }
    }
    return -1;
}

int annotationRailHit(const App& app, float x, float y) {
    for (const auto& mark : app.annotationMarks) {
        if (x >= mark.square.left - 2 && x <= mark.square.right + 2 &&
            y >= mark.square.top - 2 && y <= mark.square.bottom + 2) {
            return mark.index;
        }
    }
    return -1;
}

bool annotationCopyButtonHit(const App& app, float x, float y) {
    const D2D1_RECT_F& r = app.annotCopyBtnRect;
    if (r.right <= r.left) return false;
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

void annotationScrollTo(App& app, int index) {
    if (index < 0 || index >= (int)app.annotations.size()) return;
    // The anchor may live in a chunk that has not laid out yet
    ensureLayoutComplete(app);
    App::Annotation& a = app.annotations[index];
    if (a.docStart == kNone) findQuote(app, a.quote, a.docStart, a.docEnd);
    float docY = -1.0f;
    if (a.docStart != kNone) {
        for (const auto& trc : app.textRects) {
            if (trc.docLength == 0) continue;
            if (trc.docStart + trc.docLength > a.docStart) {
                docY = trc.rect.top;
                break;
            }
        }
    }
    if (docY < 0.0f) {
        int total = std::max(1, lineOfOffset(app.sourceText,
                                             app.sourceText.size()));
        docY = ((float)a.commentLine / (float)total) * app.contentHeight;
    }
    // Land the annotation in the upper third (scrolling is immediate in
    // the viewer, same as search navigation)
    float target = docY - (float)app.height * 0.35f;
    float maxScroll = std::max(0.0f, app.contentHeight - (float)app.height);
    app.targetScrollY = std::max(0.0f, std::min(target, maxScroll));
    app.scrollY = app.targetScrollY;
}

void annotationOpenEditor(App& app, int index) {
    if (index < 0 || index >= (int)app.annotations.size()) return;
    app.annotEditorOpen = true;
    app.annotEditorIndex = index;
    app.annotEditorText = toWide(app.annotations[index].note);
    size_t pos = 0;  // note buffer uses \n only
    while ((pos = app.annotEditorText.find(L"\r\n", pos)) != std::wstring::npos) {
        app.annotEditorText.replace(pos, 2, L"\n");
    }
    app.annotEditorCaret = app.annotEditorText.size();
    resetCursorBlink(app);
}

bool annotationBeginCreate(App& app) {
    if (app.editMode || app.currentFile.empty() || !app.hasSelection ||
        app.selAnchor == app.selFocus) {
        return false;
    }
    std::wstring selected = selectionTextForRange(
        app, std::min(app.selAnchor, app.selFocus),
        std::max(app.selAnchor, app.selFocus));
    std::wstring collapsed = collapseWs(selected);
    if (collapsed.empty()) return false;

    int lineFrom = 0, lineTo = 0;
    if (!locateInSource(app.sourceText, collapsed, lineFrom, lineTo)) {
        showToast(app, "annot.nolocate");
        return false;
    }
    std::wstring quote = elideQuote(collapsed);
    std::string quoteU8 = toUtf8(quote);
    sanitizeForComment(quoteU8);
    size_t pos = 0;  // the quote terminator sequence must stay unique
    while ((pos = quoteU8.find("\":", pos)) != std::string::npos) {
        quoteU8.replace(pos, 2, "\" :");
    }

    app.annotPendingInsert = insertOffsetAfterBlock(app.sourceText, lineTo);
    app.annotPendingQuote = quoteU8;
    app.annotPendingLineFrom = lineFrom;
    app.annotPendingLineTo = lineTo;
    app.annotEditorOpen = true;
    app.annotEditorIndex = -1;
    app.annotEditorText.clear();
    app.annotEditorCaret = 0;
    resetCursorBlink(app);
    return true;
}

void annotationEditorConfirm(App& app, HWND hwnd) {
    if (!app.annotEditorOpen) return;
    std::string note = toUtf8(app.annotEditorText);
    sanitizeForComment(note);
    std::string eol = usesCrLf(app.sourceText) ? "\r\n" : "\n";
    size_t pos = 0;
    while ((pos = note.find('\n', pos)) != std::string::npos) {
        note.replace(pos, 1, eol);
        pos += eol.size();
    }

    std::string src = app.sourceText;
    if (app.annotEditorIndex >= 0 &&
        app.annotEditorIndex < (int)app.annotations.size()) {
        const auto& a = app.annotations[app.annotEditorIndex];
        std::string comment =
            std::string(kCommentOpen) + a.quote + "\": " + note + " -->";
        if (a.commentEnd <= src.size() && a.commentStart < a.commentEnd) {
            src.replace(a.commentStart, a.commentEnd - a.commentStart, comment);
        }
    } else if (app.annotPendingInsert != kNone) {
        std::string comment =
            std::string(kCommentOpen) + app.annotPendingQuote + "\": " + note +
            " -->" + eol;
        size_t at = std::min(app.annotPendingInsert, src.size());
        if (at > 0 && src[at - 1] != '\n') comment = eol + comment;
        src.insert(at, comment);
        app.hasSelection = false;
    } else {
        closeEditor(app);
        return;
    }
    closeEditor(app);
    writeSourceAndReload(app, hwnd, src);
}

void annotationEditorCancel(App& app) {
    closeEditor(app);
}

void annotationEditorDelete(App& app, HWND hwnd) {
    if (!app.annotEditorOpen || app.annotEditorIndex < 0 ||
        app.annotEditorIndex >= (int)app.annotations.size()) {
        closeEditor(app);
        return;
    }
    const auto& a = app.annotations[app.annotEditorIndex];
    std::string src = app.sourceText;
    size_t start = a.commentStart;
    size_t end = std::min(a.commentEnd, src.size());
    // Take the whole line when the comment stands alone on it
    size_t lineStart = src.rfind('\n', start ? start - 1 : 0);
    lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
    if (lineIsComment(src, lineStart)) start = lineStart;
    if (end < src.size() && src[end] == '\r') end++;
    if (end < src.size() && src[end] == '\n') end++;
    if (start < end && end <= src.size()) src.erase(start, end - start);
    closeEditor(app);
    writeSourceAndReload(app, hwnd, src);
}

bool annotationEditorKeyDown(App& app, HWND hwnd, WPARAM key, bool ctrl) {
    if (!app.annotEditorOpen) return false;
    std::wstring& text = app.annotEditorText;
    size_t& caret = app.annotEditorCaret;
    caret = std::min(caret, text.size());
    switch (key) {
        case VK_ESCAPE:
            annotationEditorCancel(app);
            break;
        case VK_RETURN:
            if (ctrl) {
                annotationEditorConfirm(app, hwnd);
            } else {
                text.insert(caret, 1, L'\n');
                caret++;
            }
            break;
        case VK_BACK:
            if (caret > 0) {
                text.erase(caret - 1, 1);
                caret--;
            }
            break;
        case VK_DELETE:
            if (caret < text.size()) text.erase(caret, 1);
            break;
        case VK_LEFT:
            if (caret > 0) caret--;
            break;
        case VK_RIGHT:
            if (caret < text.size()) caret++;
            break;
        case VK_HOME: {
            size_t nl = text.rfind(L'\n', caret ? caret - 1 : 0);
            caret = (nl == std::wstring::npos || caret == 0) ? 0 : nl + 1;
            break;
        }
        case VK_END: {
            size_t nl = text.find(L'\n', caret);
            caret = (nl == std::wstring::npos) ? text.size() : nl;
            break;
        }
        case 'V':
            if (ctrl) {
                std::wstring pasted = fullClipboardText(hwnd);
                text.insert(caret, pasted);
                caret += pasted.size();
            }
            break;
        default:
            return true;  // consumed anyway: the editor owns the keyboard
    }
    resetCursorBlink(app);
    return true;
}

bool annotationEditorChar(App& app, WPARAM ch) {
    if (!app.annotEditorOpen) return false;
    if (ch == L'\r' || ch == L'\n' || (ch < 0x20 && ch != L'\t')) return true;
    app.annotEditorCaret = std::min(app.annotEditorCaret, app.annotEditorText.size());
    app.annotEditorText.insert(app.annotEditorCaret, 1, (wchar_t)ch);
    app.annotEditorCaret++;
    resetCursorBlink(app);
    return true;
}

int annotationEditorButtonAt(const App& app, float x, float y) {
    if (!app.annotEditorOpen) return 0;
    EditorGeom g = editorGeom(app);
    auto in = [&](const D2D1_RECT_F& r) {
        return r.right > r.left && x >= r.left && x <= r.right &&
               y >= r.top && y <= r.bottom;
    };
    if (in(g.okBtn)) return 1;
    if (in(g.cancelBtn)) return 2;
    if (in(g.trashBtn)) return 3;
    return 0;
}

bool annotationEditorContains(const App& app, float x, float y) {
    if (!app.annotEditorOpen) return false;
    EditorGeom g = editorGeom(app);
    return x >= g.panel.left && x <= g.panel.right &&
           y >= g.panel.top && y <= g.panel.bottom;
}

void renderAnnotationEditor(App& app) {
    if (!app.annotEditorOpen || !app.renderTarget || !app.brush ||
        !app.folderBrowserFormat || !app.dwriteFactory) {
        return;
    }
    EditorGeom g = editorGeom(app);
    D2D1_COLOR_F color = annotColor(app);

    D2D1_COLOR_F bg = app.theme.isDark ? hexColor(0x1E1E1E, 0.98f)
                                       : hexColor(0xFCFCFA, 0.98f);
    app.brush->SetColor(bg);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(g.panel, 8, 8), app.brush);
    D2D1_COLOR_F border = color;
    border.a = 0.7f;
    app.brush->SetColor(border);
    app.renderTarget->DrawRoundedRectangle(
        D2D1::RoundedRect(g.panel, 8, 8), app.brush, 1.0f);

    // Header: the quote this note is anchored to
    std::string quoteU8 = app.annotEditorIndex >= 0 &&
                          app.annotEditorIndex < (int)app.annotations.size()
                              ? app.annotations[app.annotEditorIndex].quote
                              : app.annotPendingQuote;
    std::wstring quote = toWide(quoteU8);
    if (quote.size() > 52) quote = quote.substr(0, 52) + L"...";
    D2D1_COLOR_F quoteColor = app.theme.text;
    quoteColor.a = 0.55f;
    app.brush->SetColor(quoteColor);
    float pad = dpi(app, 14.0f);
    app.renderTarget->DrawText(
        quote.c_str(), (UINT32)quote.size(), app.folderBrowserFormat,
        D2D1::RectF(g.panel.left + pad, g.panel.top + pad * 0.7f,
                    g.panel.right - pad, g.textArea.top),
        app.brush);

    // Text area
    D2D1_COLOR_F fieldBg = app.theme.isDark ? hexColor(0x161616, 1.0f)
                                            : hexColor(0xFFFFFF, 1.0f);
    app.brush->SetColor(fieldBg);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(g.textArea, 4, 4), app.brush);
    D2D1_COLOR_F fieldBorder = app.theme.text;
    fieldBorder.a = 0.2f;
    app.brush->SetColor(fieldBorder);
    app.renderTarget->DrawRoundedRectangle(
        D2D1::RoundedRect(g.textArea, 4, 4), app.brush, 1.0f);

    float tx = g.textArea.left + dpi(app, 6.0f);
    float ty = g.textArea.top + dpi(app, 4.0f);
    float tw = g.textArea.right - g.textArea.left - dpi(app, 12.0f);
    const std::wstring& text = app.annotEditorText;
    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(
        text.c_str(), (UINT32)text.size(), app.folderBrowserFormat,
        tw, 10000.0f, &layout);
    if (layout) layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    if (text.empty()) {
        const wchar_t* hint = tr(app, "annot.note_hint");
        D2D1_COLOR_F hintColor = app.theme.text;
        hintColor.a = 0.35f;
        app.brush->SetColor(hintColor);
        app.renderTarget->DrawText(
            hint, (UINT32)wcslen(hint), app.folderBrowserFormat,
            D2D1::RectF(tx, ty, g.textArea.right - dpi(app, 6.0f),
                        g.textArea.bottom),
            app.brush);
    }
    float caretX = tx, caretY = ty, caretH = dpi(app, 16.0f);
    if (layout) {
        app.brush->SetColor(app.theme.text);
        app.renderTarget->DrawTextLayout(D2D1::Point2F(tx, ty), layout,
                                         app.brush);
        DWRITE_HIT_TEST_METRICS m{};
        FLOAT cx = 0, cy = 0;
        if (SUCCEEDED(layout->HitTestTextPosition(
                (UINT32)std::min(app.annotEditorCaret, text.size()), FALSE,
                &cx, &cy, &m))) {
            caretX = tx + cx;
            caretY = ty + cy;
            caretH = m.height > 0 ? m.height : caretH;
        }
        layout->Release();
    }
    if (app.cursorBlinkOn) {
        app.brush->SetColor(app.theme.text);
        app.renderTarget->DrawLine(D2D1::Point2F(caretX, caretY),
                                   D2D1::Point2F(caretX, caretY + caretH),
                                   app.brush, 1.0f);
    }

    // Buttons: vector glyphs so no icon font dependency
    auto buttonBg = [&](const D2D1_RECT_F& r, bool hovered) {
        D2D1_COLOR_F hb = app.theme.text;
        hb.a = hovered ? 0.12f : 0.05f;
        app.brush->SetColor(hb);
        app.renderTarget->FillRoundedRectangle(D2D1::RoundedRect(r, 4, 4),
                                               app.brush);
    };
    int hoveredBtn = annotationEditorButtonAt(app, (float)app.mouseX,
                                              (float)app.mouseY);
    // Confirm: check mark
    buttonBg(g.okBtn, hoveredBtn == 1);
    D2D1_COLOR_F ok = app.theme.isDark ? hexColor(0x5FBF6E) : hexColor(0x2F8F44);
    app.brush->SetColor(ok);
    {
        float cx = (g.okBtn.left + g.okBtn.right) / 2.0f;
        float cy = (g.okBtn.top + g.okBtn.bottom) / 2.0f;
        float s = dpi(app, 4.5f);
        app.renderTarget->DrawLine(
            D2D1::Point2F(cx - s, cy), D2D1::Point2F(cx - s * 0.2f, cy + s * 0.8f),
            app.brush, 2.0f);
        app.renderTarget->DrawLine(
            D2D1::Point2F(cx - s * 0.2f, cy + s * 0.8f),
            D2D1::Point2F(cx + s, cy - s * 0.7f), app.brush, 2.0f);
    }
    // Cancel: X
    buttonBg(g.cancelBtn, hoveredBtn == 2);
    app.brush->SetColor(app.theme.text);
    {
        float cx = (g.cancelBtn.left + g.cancelBtn.right) / 2.0f;
        float cy = (g.cancelBtn.top + g.cancelBtn.bottom) / 2.0f;
        float s = dpi(app, 4.0f);
        app.renderTarget->DrawLine(D2D1::Point2F(cx - s, cy - s),
                                   D2D1::Point2F(cx + s, cy + s), app.brush, 1.5f);
        app.renderTarget->DrawLine(D2D1::Point2F(cx - s, cy + s),
                                   D2D1::Point2F(cx + s, cy - s), app.brush, 1.5f);
    }
    // Delete: trash can (existing annotations only)
    if (g.trashBtn.right > g.trashBtn.left) {
        buttonBg(g.trashBtn, hoveredBtn == 3);
        D2D1_COLOR_F trash = app.theme.isDark ? hexColor(0xE06A50)
                                              : hexColor(0xB03A24);
        app.brush->SetColor(trash);
        float cx = (g.trashBtn.left + g.trashBtn.right) / 2.0f;
        float cy = (g.trashBtn.top + g.trashBtn.bottom) / 2.0f;
        float bw2 = dpi(app, 3.5f);
        float bh2 = dpi(app, 4.5f);
        app.renderTarget->DrawRectangle(
            D2D1::RectF(cx - bw2, cy - bh2 + dpi(app, 2.0f),
                        cx + bw2, cy + bh2), app.brush, 1.2f);
        app.renderTarget->DrawLine(
            D2D1::Point2F(cx - bw2 - dpi(app, 1.5f), cy - bh2 + dpi(app, 2.0f)),
            D2D1::Point2F(cx + bw2 + dpi(app, 1.5f), cy - bh2 + dpi(app, 2.0f)),
            app.brush, 1.2f);
        app.renderTarget->DrawLine(
            D2D1::Point2F(cx - dpi(app, 1.2f), cy - bh2),
            D2D1::Point2F(cx + dpi(app, 1.2f), cy - bh2), app.brush, 1.2f);
        app.renderTarget->DrawLine(
            D2D1::Point2F(cx, cy - dpi(app, 1.0f)),
            D2D1::Point2F(cx, cy + dpi(app, 2.5f)), app.brush, 1.0f);
    }
}

void annotationsCopyForAgent(App& app, HWND hwnd) {
    if (app.annotations.empty() || app.currentFile.empty()) return;
    // A short task preamble so any agent knows what to do with the list
    std::wstring out = toWide(app.currentFile);
    out += L"\n";
    out += L"Address each review remark below in this file. The L numbers "
           L"are source lines and the quoted text locates the passage. "
           L"After addressing a remark, delete its matching "
           L"\"<!-- review ... -->\" comment line from the file.\n";
    for (const auto& a : app.annotations) {
        out += L"L" + std::to_wstring(a.lineFrom);
        if (a.lineTo > a.lineFrom) out += L"-L" + std::to_wstring(a.lineTo);
        out += L" \"" + toWide(a.quote) + L"\": ";
        std::wstring note = toWide(a.note);
        size_t pos = 0;  // continuation lines indent under their remark
        while ((pos = note.find(L'\n', pos)) != std::wstring::npos) {
            note.replace(pos, 1, L"\n  ");
            pos += 3;
        }
        out += note;
        out += L"\n";
    }
    copyToClipboard(hwnd, out);
    showToast(app, "toast.agent_copied");
}
