#include "render.h"
#include "inline_style.h"
#include "utils.h"
#include "syntax.h"
#include "search.h"
#include "mermaid.h"
#include "mermaid_ext.h"
#include "math_render.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <string_view>
#include <filesystem>
#include <urlmon.h>
#include <thread>
#pragma comment(lib, "urlmon.lib")

namespace {
constexpr float kHugeWidth = 100000.0f;
constexpr float kLineBucketTolerance = 5.0f;
// Horizontal padding drawn around an inline `code` pill
constexpr float kCodeSpanPadding = 4.0f;

struct LayoutInfo {
    IDWriteTextLayout* layout = nullptr;
    float width = 0.0f;
    float height = 0.0f;
};

static LayoutInfo createLayout(App& app, std::wstring_view text, IDWriteTextFormat* format,
                               float lineHeight, IDWriteTypography* typography) {
    LayoutInfo info;
    if (!format || text.empty()) return info;

    app.dwriteFactory->CreateTextLayout(text.data(), (UINT32)text.length(),
        format, kHugeWidth, lineHeight, &info.layout);
    if (info.layout) {
        if (typography) {
            info.layout->SetTypography(typography, {0, (UINT32)text.length()});
        }
        // Apply font fallback for emoji support
        if (app.fontFallback) {
            IDWriteTextLayout2* layout2 = nullptr;
            if (SUCCEEDED(info.layout->QueryInterface(__uuidof(IDWriteTextLayout2),
                    reinterpret_cast<void**>(&layout2)))) {
                layout2->SetFontFallback(app.fontFallback);
                layout2->Release();
            }
        }
        DWRITE_TEXT_METRICS metrics{};
        info.layout->GetMetrics(&metrics);
        info.width = metrics.widthIncludingTrailingWhitespace;
        info.height = metrics.height;
    }
    return info;
}

static void addTextRect(App& app, const D2D1_RECT_F& rect, size_t docStart, size_t docLength,
                        size_t runIndex = (size_t)-1) {
    size_t idx = app.textRects.size();
    app.textRects.push_back({rect, docStart, docLength, runIndex});

    // Table cells are laid out column by column. A wrapped cell can append a
    // later line before the next column appends its first line, so buckets
    // cannot be grouped by looking only at the last one.
    for (auto& bucket : app.lineBuckets) {
        if (std::abs(rect.top - bucket.top) <= kLineBucketTolerance) {
            bucket.bottom = std::max(bucket.bottom, rect.bottom);
            bucket.minX = std::min(bucket.minX, rect.left);
            bucket.maxX = std::max(bucket.maxX, rect.right);
            bucket.textRectIndices.push_back(idx);
            return;
        }
    }

    if (app.lineBuckets.empty() || rect.top > app.lineBuckets.back().top) {
        App::LineBucket bucket;
        bucket.top = rect.top;
        bucket.bottom = rect.bottom;
        bucket.minX = rect.left;
        bucket.maxX = rect.right;
        bucket.textRectIndices.push_back(idx);
        app.lineBuckets.push_back(std::move(bucket));
        return;
    }

    App::LineBucket bucket;
    bucket.top = rect.top;
    bucket.bottom = rect.bottom;
    bucket.minX = rect.left;
    bucket.maxX = rect.right;
    bucket.textRectIndices.push_back(idx);
    auto insertAt = std::lower_bound(
        app.lineBuckets.begin(), app.lineBuckets.end(), rect.top,
        [](const App::LineBucket& existing, float top) {
            return existing.top < top;
        });
    app.lineBuckets.insert(insertAt, std::move(bucket));
}

static void addTextRun(App& app, LayoutInfo&& info, const D2D1_POINT_2F& pos,
                       const D2D1_RECT_F& bounds, D2D1_COLOR_F color,
                       size_t docStart, size_t docLength, bool selectable) {
    if (!info.layout) return;

    App::LayoutTextRun run;
    run.layout = info.layout;
    run.pos = pos;
    run.bounds = bounds;
    run.color = color;
    run.docStart = docStart;
    run.docLength = docLength;
    run.selectable = selectable;
    app.layoutTextRuns.push_back(run);

    if (selectable) {
        addTextRect(app, bounds, docStart, docLength, app.layoutTextRuns.size() - 1);
    }
}

struct LayoutSnapshot {
    size_t textRuns, rects, lines, shapes, connectors;
    size_t links, textRects, lineBuckets, docTextLen, tasks;
};

static LayoutSnapshot takeSnapshot(App& app) {
    return {
        app.layoutTextRuns.size(),
        app.layoutRects.size(),
        app.layoutLines.size(),
        app.layoutShapes.size(),
        app.layoutConnectors.size(),
        app.linkRects.size(),
        app.textRects.size(),
        app.lineBuckets.size(),
        app.docText.size(),
        app.taskRects.size()
    };
}

static void rollbackTo(App& app, const LayoutSnapshot& s) {
    for (size_t i = s.textRuns; i < app.layoutTextRuns.size(); i++) {
        if (app.layoutTextRuns[i].layout) {
            app.layoutTextRuns[i].layout->Release();
        }
    }
    for (size_t i = s.shapes; i < app.layoutShapes.size(); i++) {
        if (app.layoutShapes[i].geometry) {
            app.layoutShapes[i].geometry->Release();
        }
    }
    app.layoutTextRuns.resize(s.textRuns);
    app.layoutRects.resize(s.rects);
    app.layoutLines.resize(s.lines);
    app.layoutShapes.resize(s.shapes);
    app.layoutConnectors.resize(s.connectors);
    app.linkRects.resize(s.links);
    app.textRects.resize(s.textRects);
    app.lineBuckets.resize(s.lineBuckets);
    app.docText.resize(s.docTextLen);
    app.taskRects.resize(s.tasks);
}

static void shiftLayoutItems(App& app, const LayoutSnapshot& from, float dx) {
    if (dx == 0.0f) return;
    for (size_t i = from.textRuns; i < app.layoutTextRuns.size(); i++) {
        auto& r = app.layoutTextRuns[i];
        r.pos.x += dx;
        r.bounds.left += dx;
        r.bounds.right += dx;
    }
    for (size_t i = from.rects; i < app.layoutRects.size(); i++) {
        auto& r = app.layoutRects[i];
        r.rect.left += dx;
        r.rect.right += dx;
    }
    for (size_t i = from.lines; i < app.layoutLines.size(); i++) {
        auto& r = app.layoutLines[i];
        r.p1.x += dx;
        r.p2.x += dx;
    }
    for (size_t i = from.shapes; i < app.layoutShapes.size(); i++) {
        auto& shape = app.layoutShapes[i];
        shape.rect.left += dx;
        shape.rect.right += dx;
    }
    for (size_t i = from.connectors; i < app.layoutConnectors.size(); i++) {
        auto& connector = app.layoutConnectors[i];
        connector.bounds.left += dx;
        connector.bounds.right += dx;
        for (auto& point : connector.points) point.x += dx;
    }
    for (size_t i = from.links; i < app.linkRects.size(); i++) {
        auto& r = app.linkRects[i];
        r.bounds.left += dx;
        r.bounds.right += dx;
    }
    for (size_t i = from.textRects; i < app.textRects.size(); i++) {
        auto& r = app.textRects[i];
        r.rect.left += dx;
        r.rect.right += dx;
    }
    for (size_t i = from.tasks; i < app.taskRects.size(); i++) {
        app.taskRects[i].rect.left += dx;
        app.taskRects[i].rect.right += dx;
    }

    // A cell can share a line bucket with content laid out before it. Rebuild
    // the affected bucket extents from their rectangles after the shift.
    for (auto& bucket : app.lineBuckets) {
        bool affected = false;
        for (size_t idx : bucket.textRectIndices) {
            if (idx >= from.textRects) {
                affected = true;
                break;
            }
        }
        if (!affected) continue;

        bucket.minX = std::numeric_limits<float>::max();
        bucket.maxX = std::numeric_limits<float>::lowest();
        for (size_t idx : bucket.textRectIndices) {
            const auto& rect = app.textRects[idx].rect;
            bucket.minX = std::min(bucket.minX, rect.left);
            bucket.maxX = std::max(bucket.maxX, rect.right);
        }
    }
}

static float getSpaceWidth(App& app, IDWriteTextFormat* format) {
    if (format == app.textFormat) return app.spaceWidthText;
    if (format == app.boldFormat) return app.spaceWidthBold;
    if (format == app.italicFormat) return app.spaceWidthItalic;
    if (format == app.codeFormat) return app.spaceWidthCode;
    return measureText(app, L" ", format);
}

static void layoutElement(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth);
static void layoutImage(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth);

// --- UAX#14 line-break analysis ---
//
// DirectWrite's text analyzer implements the Unicode line breaking
// algorithm, including CJK rules (a break opportunity between almost every
// pair of ideographs, but never before closing punctuation like 。，」or
// after opening brackets). Chinese prose has no spaces, so anything less
// treats an entire sentence as one unbreakable word.

namespace {

class LineBreakAnalysis final : public IDWriteTextAnalysisSource,
                                public IDWriteTextAnalysisSink {
public:
    LineBreakAnalysis(const wchar_t* text, UINT32 length)
        : text_(text), length_(length), breakpoints_(length) {}

    std::vector<DWRITE_LINE_BREAKPOINT> breakpoints_;

    // Stack-allocated and used synchronously: refcounting is inert
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (riid == __uuidof(IDWriteTextAnalysisSource)) {
            *object = static_cast<IDWriteTextAnalysisSource*>(this);
            return S_OK;
        }
        if (riid == __uuidof(IDWriteTextAnalysisSink)) {
            *object = static_cast<IDWriteTextAnalysisSink*>(this);
            return S_OK;
        }
        if (riid == __uuidof(IUnknown)) {
            *object = static_cast<IUnknown*>(static_cast<IDWriteTextAnalysisSource*>(this));
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    // IDWriteTextAnalysisSource
    HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 position, const WCHAR** text,
                                                UINT32* textLength) override {
        if (position >= length_) { *text = nullptr; *textLength = 0; return S_OK; }
        *text = text_ + position;
        *textLength = length_ - position;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 position, const WCHAR** text,
                                                    UINT32* textLength) override {
        if (position == 0 || position > length_) { *text = nullptr; *textLength = 0; return S_OK; }
        *text = text_;
        *textLength = position;
        return S_OK;
    }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override {
        return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
    }
    HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 position, UINT32* textLength,
                                            const WCHAR** localeName) override {
        *localeName = L"en-us";
        *textLength = length_ - position;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32 position, UINT32* textLength,
                                                    IDWriteNumberSubstitution** substitution) override {
        *substitution = nullptr;
        *textLength = length_ - position;
        return S_OK;
    }

    // IDWriteTextAnalysisSink
    HRESULT STDMETHODCALLTYPE SetLineBreakpoints(UINT32 position, UINT32 length,
                                                 const DWRITE_LINE_BREAKPOINT* lineBreakpoints) override {
        for (UINT32 i = 0; i < length && position + i < breakpoints_.size(); i++) {
            breakpoints_[position + i] = lineBreakpoints[i];
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetScriptAnalysis(UINT32, UINT32, const DWRITE_SCRIPT_ANALYSIS*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetBidiLevel(UINT32, UINT32, UINT8, UINT8) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetNumberSubstitution(UINT32, UINT32, IDWriteNumberSubstitution*) override { return S_OK; }

private:
    const wchar_t* text_;
    UINT32 length_;
};

// canBreak[i] == true when a line may break before text[i]; canBreak[len]
// is always true. Falls back to space-only breaking if analysis fails.
void analyzeBreakOpportunities(App& app, const std::wstring& text, std::vector<bool>& canBreak) {
    canBreak.assign(text.size() + 1, false);
    if (text.empty()) return;
    canBreak[text.size()] = true;

    // Fast path: plain ASCII prose with ordinary token lengths breaks at
    // spaces exactly like the full algorithm — skip the analyzer round trip
    bool simple = true;
    size_t tokenLen = 0;
    for (wchar_t c : text) {
        if (c >= 0x80) { simple = false; break; }
        if (c == L' ') tokenLen = 0;
        else if (++tokenLen > 60) { simple = false; break; }  // long URLs etc. want real breaks
    }
    if (simple) {
        for (size_t i = 1; i < text.size(); i++) {
            if (text[i - 1] == L' ') canBreak[i] = true;
        }
        return;
    }

    bool analyzed = false;
    if (app.textAnalyzer) {
        LineBreakAnalysis analysis(text.c_str(), (UINT32)text.size());
        if (SUCCEEDED(app.textAnalyzer->AnalyzeLineBreakpoints(
                &analysis, 0, (UINT32)text.size(), &analysis))) {
            for (size_t i = 1; i < text.size(); i++) {
                UINT8 after = analysis.breakpoints_[i - 1].breakConditionAfter;
                canBreak[i] = (after == DWRITE_BREAK_CONDITION_CAN_BREAK ||
                               after == DWRITE_BREAK_CONDITION_MUST_BREAK);
            }
            analyzed = true;
        }
    }
    if (!analyzed) {
        for (size_t i = 1; i < text.size(); i++) {
            if (text[i - 1] == L' ') canBreak[i] = true;
        }
    }
}

} // namespace

static void layoutInlineContent(App& app, const std::vector<ElementPtr>& elements,
                                float startX, float& y, float maxWidth,
                                IDWriteTextFormat* baseFormat, D2D1_COLOR_F baseColor,
                                const std::string& baseLinkUrl = {}, float customLineHeight = 0.0f) {
    float x = startX;
    float lineHeight = customLineHeight > 0 ? customLineHeight : baseFormat->GetFontSize() * 1.7f;
    float maxX = startX + maxWidth;
    float spaceWidth = getSpaceWidth(app, baseFormat);

    auto addLinkSegment = [&](float lineStartX, float lineEndX, float lineY,
                              const std::string& linkUrl, D2D1_COLOR_F color) {
        if (lineEndX <= lineStartX) return;
        float underlineY = lineY + lineHeight - 2;
        app.layoutLines.push_back({D2D1::Point2F(lineStartX, underlineY),
                                   D2D1::Point2F(lineEndX, underlineY),
                                   color, 1.0f});
        App::LinkRect lr;
        lr.bounds = D2D1::RectF(lineStartX, lineY, lineEndX, lineY + lineHeight);
        lr.url = linkUrl;
        app.linkRects.push_back(lr);
    };

    InlineStyle rootStyle;
    rootStyle.format = baseFormat;
    rootStyle.color = baseColor;
    rootStyle.linkUrl = baseLinkUrl;
    rootStyle.isLink = !baseLinkUrl.empty();
    std::vector<StyledRun> runs;
    flattenInline(app, elements, rootStyle, lineHeight, runs);

    for (const auto& run : runs) {
        const ElementPtr& elem = run.elem;
        IDWriteTextFormat* format = run.style.format;
        D2D1_COLOR_F color = run.style.color;
        const std::string& linkUrl = run.style.linkUrl;
        bool isLink = run.style.isLink;
        bool hasBg = run.style.hasBg;
        bool hasStrike = run.style.hasStrike;
        D2D1_COLOR_F bgColor = run.style.bgColor;
        float drawYOffset = run.style.drawYOffset;

        std::wstring text;

        switch (elem->type) {
            case ElementType::Text:
                text = toWide(elem->text);
                break;

            case ElementType::Code: {
                format = inlineCodeFormat(app, run.style.bold, run.style.italic);
                if (!isLink) color = app.theme.code;
                for (const auto& child : elem->children) {
                    if (child->type == ElementType::Text) {
                        text += toWide(child->text);
                    }
                }

                size_t codeDocStart = app.docText.size();
                LayoutInfo info = createLayout(app, text, format, lineHeight, app.codeTypography);
                float textWidth = info.width;

                if (x + textWidth > maxX && x > startX) {
                    x = startX;
                    y += lineHeight;
                }

                app.layoutRects.push_back({D2D1::RectF(x - 2, y,
                                                      x + textWidth + kCodeSpanPadding,
                                                      y + lineHeight),
                                           app.theme.codeBackground});

                float codeFontHeight = format->GetFontSize() * 1.2f;
                float verticalOffset = (lineHeight - codeFontHeight) / 2.0f;
                D2D1_POINT_2F pos = D2D1::Point2F(x, y + verticalOffset);
                D2D1_RECT_F bounds = D2D1::RectF(x, y, x + textWidth, y + lineHeight);
                addTextRun(app, std::move(info), pos, bounds, color,
                           codeDocStart, text.length(), true);

                app.docText += text;
                if (isLink) addLinkSegment(x, x + textWidth, y, linkUrl, color);
                // Advance by the pill's own padding only: the surrounding text
                // already carries the source's spacing, so a full space width
                // here would double it and shove a following comma off the span
                x += textWidth + kCodeSpanPadding;
                continue;
            }

            case ElementType::MathInline:
            case ElementType::MathDisplay: {
                // Native inline math (#80): an atomic measured box in the
                // text flow, baseline-aligned with the surrounding line
                std::wstring latex = toWide(elem->text);
                float mathSize = format ? format->GetFontSize()
                                        : app.textFormat->GetFontSize();
                MathBoxPtr box = mathParse(app, latex, mathSize, false);
                if (!box) {
                    // Unsupported TeX: show the raw source in code style
                    format = inlineCodeFormat(app, run.style.bold, run.style.italic);
                    if (!isLink) color = app.theme.code;
                    text = L"$" + latex + L"$";
                    break;
                }
                float w = mathBoxWidth(box);
                if (x + w > maxX && x > startX) {
                    x = startX;
                    y += lineHeight;
                }
                // Align the box baseline with the surrounding text baseline
                float textBaseline = mathSize * 0.8f;
                {
                    IDWriteTextLayout* probe = nullptr;
                    app.dwriteFactory->CreateTextLayout(
                        L"Ag", 2, format ? format : app.textFormat,
                        1000.0f, 1000.0f, &probe);
                    if (probe) {
                        DWRITE_LINE_METRICS lm{};
                        UINT32 n = 1;
                        probe->GetLineMetrics(&lm, 1, &n);
                        textBaseline = lm.baseline;
                        probe->Release();
                    }
                }
                float boxTop = y + drawYOffset + textBaseline - mathBoxBaseline(box);
                mathBoxRetain(app, box, x, boxTop, app.theme.text);

                size_t docStart = app.docText.size();
                std::wstring source = L"$" + latex + L"$";
                app.docText += source;
                addTextRect(app,
                            D2D1::RectF(x, y, x + w, y + lineHeight),
                            docStart, source.size());
                x += w;
                continue;
            }

            case ElementType::SoftBreak:
                text = L" ";
                break;

            case ElementType::HardBreak:
                app.docText += L"\n";
                x = startX;
                y += lineHeight;
                continue;

            case ElementType::Image: {
                // Break out of inline flow, render image as block
                if (x > startX) {
                    y += lineHeight;  // end current line
                    x = startX;
                }
                layoutImage(app, elem, y, startX, maxWidth);
                continue;
            }

            case ElementType::Ruby: {
                // Collect base text and ruby annotation text
                std::wstring baseText, rubyText;
                for (const auto& child : elem->children) {
                    if (child->type == ElementType::RubyText) {
                        for (const auto& rtChild : child->children) {
                            if (rtChild->type == ElementType::Text) {
                                rubyText += toWide(rtChild->text);
                            }
                        }
                    } else if (child->type == ElementType::Text) {
                        baseText += toWide(child->text);
                    }
                }
                if (baseText.empty()) continue;

                float rubyFontSize = baseFormat->GetFontSize() * 0.5f;
                float rubyLineHeight = rubyFontSize * 1.4f;

                // Measure base text
                size_t rubyDocStart = app.docText.size();
                LayoutInfo baseInfo = createLayout(app, baseText, baseFormat, lineHeight, app.bodyTypography);
                float baseWidth = baseInfo.width;

                // Measure ruby text
                LayoutInfo rubyInfo = {nullptr, 0.0f};
                float rubyWidth = 0.0f;
                if (!rubyText.empty()) {
                    rubyInfo = createLayout(app, rubyText, baseFormat, rubyLineHeight, app.bodyTypography);
                    if (rubyInfo.layout) {
                        // Set smaller font size on the ruby layout
                        DWRITE_TEXT_RANGE range = {0, (UINT32)rubyText.length()};
                        rubyInfo.layout->SetFontSize(rubyFontSize, range);
                        DWRITE_TEXT_METRICS metrics{};
                        rubyInfo.layout->GetMetrics(&metrics);
                        rubyWidth = metrics.widthIncludingTrailingWhitespace;
                    }
                }

                float totalWidth = std::max(baseWidth, rubyWidth);

                // Word-wrap: treat ruby as atomic
                if (x + totalWidth > maxX && x > startX) {
                    x = startX;
                    y += lineHeight;
                }

                // We need extra space above for the ruby text
                float rubyAboveOffset = rubyLineHeight;
                // If we're at the start of a line, push y down to make room for annotation
                // For simplicity, always reserve space above
                float baseY = y + rubyAboveOffset;

                // Center the narrower one under the wider one
                float basePosX = x + (totalWidth - baseWidth) / 2.0f;
                float rubyPosX = x + (totalWidth - rubyWidth) / 2.0f;

                // Draw ruby annotation (above base text, not selectable)
                if (rubyInfo.layout) {
                    D2D1_COLOR_F rubyColor = baseColor;
                    rubyColor.a *= 0.7f;
                    D2D1_POINT_2F rubyPos = D2D1::Point2F(rubyPosX, y);
                    D2D1_RECT_F rubyBounds = D2D1::RectF(rubyPosX, y, rubyPosX + rubyWidth, y + rubyLineHeight);
                    addTextRun(app, std::move(rubyInfo), rubyPos, rubyBounds, rubyColor, 0, 0, false);
                }

                // Draw base text (selectable)
                D2D1_POINT_2F basePos = D2D1::Point2F(basePosX, baseY);
                D2D1_RECT_F baseBounds = D2D1::RectF(basePosX, baseY, basePosX + baseWidth, baseY + lineHeight);
                addTextRun(app, std::move(baseInfo), basePos, baseBounds, baseColor,
                           rubyDocStart, baseText.length(), true);

                app.docText += baseText;
                x += totalWidth + spaceWidth;

                // Adjust y to account for the extra ruby height on the next line wrap
                // The total height is rubyAboveOffset + lineHeight but we only advance by lineHeight
                // at the end of the line, so we need to make sure the ruby doesn't overlap
                continue;
            }

            default:
                continue;  // flattenInline only ever hands us leaves
        }

        if (text.empty()) continue;

        size_t textDocStart = app.docText.size();
        float linkLineStartX = x;
        float linkLineY = y;

        // Measure the whole element once: cluster metrics give every break
        // unit's width without creating one IDWriteTextLayout per unit.
        // The measurement layout is kept alive: when the element ends up as
        // a single segment (no wrap — the common case), it IS the segment's
        // layout and no second CreateTextLayout happens.
        std::vector<float> cumW(text.length() + 1, -1.0f);
        std::vector<bool> isBoundary(text.length() + 1, false);
        cumW[0] = 0.0f;
        isBoundary[0] = true;
        isBoundary[text.length()] = true;
        LayoutInfo measureInfo = createLayout(app, text, format, lineHeight, app.bodyTypography);
        {
            if (measureInfo.layout) {
                UINT32 clusterCount = 0;
                measureInfo.layout->GetClusterMetrics(nullptr, 0, &clusterCount);
                if (clusterCount > 0) {
                    std::vector<DWRITE_CLUSTER_METRICS> clusters(clusterCount);
                    if (SUCCEEDED(measureInfo.layout->GetClusterMetrics(
                            clusters.data(), clusterCount, &clusterCount))) {
                        size_t cpos = 0;
                        float w = 0.0f;
                        for (UINT32 ci = 0; ci < clusterCount && cpos < text.length(); ci++) {
                            w += clusters[ci].width;
                            cpos += clusters[ci].length;
                            if (cpos <= text.length()) {
                                cumW[cpos] = w;
                                isBoundary[cpos] = true;
                            }
                        }
                    }
                }
            }
            // Fill positions that fall inside clusters (break opportunities
            // never land there)
            float last = 0.0f;
            for (auto& v : cumW) { if (v < 0.0f) v = last; else last = v; }
        }
        auto widthOf = [&cumW](size_t a, size_t b) { return cumW[b] - cumW[a]; };

        // Words on the same line merge into one drawn layout; per-word rects
        // are still recorded so hit-testing/selection/search stay word-level.
        struct WordRef { size_t start, len; };
        std::vector<WordRef> segWords;
        size_t segStart = 0;
        float segX = 0.0f;
        bool segOpen = false;
        float lastWordEndX = linkLineStartX;

        auto flushSegment = [&](size_t segEnd) {
            if (!segOpen) return;
            segOpen = false;
            if (segEnd <= segStart) { segWords.clear(); return; }
            std::wstring_view segText(text.data() + segStart, segEnd - segStart);

            // Single-segment element: the measurement layout already shaped
            // exactly this text (a space-only tail draws nothing) — hand it
            // over instead of shaping a second time
            bool coversAll = segStart == 0 && measureInfo.layout != nullptr;
            for (size_t i = segEnd; coversAll && i < text.length(); i++) {
                if (text[i] != L' ') coversAll = false;
            }
            LayoutInfo info;
            if (coversAll) {
                info = measureInfo;
                measureInfo.layout = nullptr;
            } else {
                info = createLayout(app, segText, format, lineHeight, app.bodyTypography);
            }
            float segWidth = widthOf(segStart, segEnd);
            if (hasBg) {
                app.layoutRects.push_back({
                    D2D1::RectF(segX - 2, y + 1, segX + segWidth + 2, y + lineHeight - 1),
                    bgColor});
            }
            if (hasStrike) {
                float strikeY = y + lineHeight * 0.55f;
                app.layoutLines.push_back({D2D1::Point2F(segX, strikeY),
                                           D2D1::Point2F(segX + segWidth, strikeY),
                                           color, 1.0f});
            }
            D2D1_POINT_2F segPos = D2D1::Point2F(segX, y + drawYOffset);
            D2D1_RECT_F segBounds = D2D1::RectF(segX, y, segX + segWidth, y + lineHeight);
            size_t runsBefore = app.layoutTextRuns.size();
            addTextRun(app, std::move(info), segPos, segBounds, color,
                       textDocStart + segStart, segEnd - segStart, false);
            // Word rects hit-test against the covering segment layout
            size_t segRun = app.layoutTextRuns.size() > runsBefore
                ? app.layoutTextRuns.size() - 1 : (size_t)-1;
            for (const auto& w : segWords) {
                float wx = segX + widthOf(segStart, w.start);
                D2D1_RECT_F wb = D2D1::RectF(wx, y, wx + widthOf(w.start, w.start + w.len),
                                             y + lineHeight);
                addTextRect(app, wb, textDocStart + w.start, w.len, segRun);
            }
            segWords.clear();
        };

        // Break opportunities from the Unicode line breaking algorithm:
        // spaces for Latin text, between-ideograph positions (with proper
        // punctuation rules) for CJK, after / and - inside URLs, etc.
        std::vector<bool> canBreak;
        analyzeBreakOpportunities(app, text, canBreak);

        size_t pos = 0;
        size_t lastWordEnd = 0;

        auto emitWord = [&](size_t wordStart, size_t wordEnd) {
            float wordWidth = widthOf(wordStart, wordEnd);
            float wordX = segOpen ? segX + widthOf(segStart, wordStart) : x;

            if (wordX + wordWidth > maxX && wordX > startX) {
                // Unit wraps: flush the current line's segment first
                flushSegment(lastWordEnd);
                if (isLink && lastWordEndX > linkLineStartX) {
                    addLinkSegment(linkLineStartX, lastWordEndX, linkLineY, linkUrl, color);
                }
                x = startX;
                y += lineHeight;
                linkLineStartX = x;
                linkLineY = y;
                wordX = x;
            }

            if (!segOpen) {
                segOpen = true;
                segStart = wordStart;
                segX = wordX;
            }
            segWords.push_back({wordStart, wordEnd - wordStart});
            lastWordEnd = wordEnd;
            lastWordEndX = segX + widthOf(segStart, wordEnd);
            x = lastWordEndX;
        };

        while (pos < text.length()) {
            // The next unit runs to the following break opportunity
            size_t bp = pos + 1;
            while (bp < text.length() && !canBreak[bp]) bp++;

            // Trailing spaces belong to the unit but don't participate in
            // the wrap decision and never render at a line start
            size_t vis = bp;
            while (vis > pos && text[vis - 1] == L' ') vis--;

            if (vis > pos) {
                if (widthOf(pos, vis) > maxWidth) {
                    // Emergency break: a single unbreakable unit wider than a
                    // whole line splits at cluster boundaries so nothing is
                    // ever clipped or overlaps a neighbor
                    size_t pieceStart = pos;
                    while (widthOf(pieceStart, vis) > maxWidth) {
                        size_t lo = pieceStart + 1, hi = vis - 1;
                        while (lo < hi) {
                            size_t mid = (lo + hi + 1) / 2;
                            if (widthOf(pieceStart, mid) <= maxWidth) lo = mid;
                            else hi = mid - 1;
                        }
                        size_t pieceEnd = lo;
                        while (pieceEnd > pieceStart + 1 && !isBoundary[pieceEnd]) pieceEnd--;
                        if (pieceEnd <= pieceStart) pieceEnd = pieceStart + 1;
                        emitWord(pieceStart, pieceEnd);
                        pieceStart = pieceEnd;
                        if (pieceStart >= vis) break;
                    }
                    if (pieceStart < vis) emitWord(pieceStart, vis);
                } else {
                    emitWord(pos, vis);
                }
            }

            if (bp > vis) {
                // Advance x across the trailing spaces (shaped width when a
                // segment is open)
                x = segOpen ? segX + widthOf(segStart, bp) : x + widthOf(vis, bp);
            }
            pos = bp;
        }
        flushSegment(lastWordEnd);
        if (measureInfo.layout) measureInfo.layout->Release();

        app.docText += text;

        if (isLink && lastWordEndX > linkLineStartX) {
            addLinkSegment(linkLineStartX, lastWordEndX, linkLineY, linkUrl, color);
        }
    }

    y += lineHeight;
}

static void layoutCodeBlock(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth);

// Display math ($$...$$, #80): a centered block rendered natively; the raw
// TeX joins docText so search and selection see the source
static void layoutMathBlock(App& app, const ElementPtr& elem, float& y,
                            float indent, float maxWidth) {
    std::wstring latex = toWide(elem->text);
    float baseSize = app.textFormat ? app.textFormat->GetFontSize() : 16.0f;
    MathBoxPtr box = mathParse(app, latex, baseSize * 1.06f, true);
    if (!box) {
        // Unsupported TeX: raw source in code style (the mermaid pattern)
        auto fallback = std::make_shared<Element>(ElementType::CodeBlock);
        auto text = std::make_shared<Element>(ElementType::Text);
        text->text = elem->text;
        text->parent = fallback.get();
        fallback->children.push_back(std::move(text));
        fallback->sourceOffset = elem->sourceOffset;
        layoutCodeBlock(app, fallback, y, indent, maxWidth);
        return;
    }
    float scale = app.contentScale * app.zoomFactor;
    y += 6 * scale;
    float w = mathBoxWidth(box);
    float x = indent + std::max(0.0f, (maxWidth - w) / 2);
    mathBoxRetain(app, box, x, y, app.theme.text);

    size_t docStart = app.docText.size();
    app.docText += latex;
    addTextRect(app, D2D1::RectF(x, y, x + w, y + mathBoxHeight(box)),
                docStart, latex.size());
    app.docText += L"\n\n";

    y += mathBoxHeight(box) + 14 * scale;
}

// A paragraph that is exactly one $$...$$ span renders as a math block
static const ElementPtr* soleMathDisplayChild(const ElementPtr& elem) {
    const ElementPtr* found = nullptr;
    for (const auto& child : elem->children) {
        if (!child) continue;
        if (child->type == ElementType::MathDisplay) {
            if (found) return nullptr;
            found = &child;
        } else if (child->type == ElementType::Text) {
            for (char c : child->text) {
                if (!isspace((unsigned char)c)) return nullptr;
            }
        } else if (child->type != ElementType::SoftBreak) {
            return nullptr;
        }
    }
    return found;
}

static void layoutParagraph(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    if (const ElementPtr* math = soleMathDisplayChild(elem)) {
        layoutMathBlock(app, *math, y, indent, maxWidth);
        return;
    }
    layoutInlineContent(app, elem->children, indent, y, maxWidth, app.textFormat, app.theme.text);
    app.docText += L"\n\n";
    float scale = app.contentScale * app.zoomFactor;
    y += 14 * scale;
}

static void layoutHeading(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    int levelIndex = std::min(elem->level - 1, 5);
    IDWriteTextFormat* format = app.headingFormats[levelIndex] ? app.headingFormats[levelIndex] : app.textFormat;

    if (elem->level == 1) {
        y += 16 * scale;
    } else {
        y += 20 * scale;
    }

    // Record heading for TOC (h1-h3 only)
    if (elem->level <= 3) {
        std::wstring headingText;
        std::function<void(const ElementPtr&)> extract = [&](const ElementPtr& e) {
            if (!e) return;
            if (e->type == ElementType::Text) headingText += toWide(e->text);
            else for (const auto& c : e->children) extract(c);
        };
        for (const auto& child : elem->children) extract(child);

        std::string baseId = slugifyHeading(headingText);
        int& n = app.headingSlugCounts[baseId];
        std::string id = (n == 0) ? baseId : (baseId + "-" + std::to_string(n));
        n++;
        app.headings.push_back({headingText, elem->level, y, id});
    }

    layoutInlineContent(app, elem->children, indent, y, maxWidth, format, app.theme.heading);

    if (elem->level <= 2) {
        y += 6 * scale;
        D2D1_COLOR_F lineColor = app.theme.heading;
        lineColor.a = 0.3f;
        float lineWidth = (elem->level == 1) ? 2.0f * scale : 1.0f * scale;
        app.layoutLines.push_back({D2D1::Point2F(indent, y),
                                   D2D1::Point2F(indent + maxWidth, y),
                                   lineColor, lineWidth});
        y += lineWidth;
    }

    app.docText += L"\n\n";
    y += 12 * scale;
}

static LayoutInfo createWrappedLayout(App& app, std::wstring_view text,
                                      IDWriteTextFormat* format,
                                      float width, float height) {
    LayoutInfo info;
    if (!format || text.empty() || width <= 0.0f || height <= 0.0f) return info;

    app.dwriteFactory->CreateTextLayout(
        text.data(), static_cast<UINT32>(text.length()),
        format, width, height, &info.layout);
    if (!info.layout) return info;

    info.layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    info.layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    info.layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (app.fontFallback) {
        IDWriteTextLayout2* layout2 = nullptr;
        if (SUCCEEDED(info.layout->QueryInterface(
                __uuidof(IDWriteTextLayout2),
                reinterpret_cast<void**>(&layout2)))) {
            layout2->SetFontFallback(app.fontFallback);
            layout2->Release();
        }
    }

    DWRITE_TEXT_METRICS metrics{};
    info.layout->GetMetrics(&metrics);
    info.width = metrics.widthIncludingTrailingWhitespace;
    info.height = metrics.height;
    return info;
}

static D2D1_COLOR_F mermaidColor(const mermaid::Color& color) {
    return D2D1::ColorF(
        ((color.rgb >> 16) & 0xFF) / 255.0f,
        ((color.rgb >> 8) & 0xFF) / 255.0f,
        (color.rgb & 0xFF) / 255.0f,
        color.alpha);
}

struct ResolvedMermaidStyle {
    D2D1_COLOR_F fill{};
    D2D1_COLOR_F stroke{};
    D2D1_COLOR_F text{};
    float strokeWidth = 1.0f;
};

static ResolvedMermaidStyle resolveMermaidStyle(
        const App& app, const mermaid::Diagram& diagram,
        const mermaid::Node& node, float scale) {
    ResolvedMermaidStyle resolved;
    resolved.fill = app.theme.codeBackground;
    resolved.stroke = app.theme.accent;
    resolved.text = app.theme.text;
    resolved.strokeWidth = 1.5f * scale;

    auto apply = [&](const mermaid::Style& style) {
        if (style.hasFill) resolved.fill = mermaidColor(style.fill);
        if (style.hasStroke) resolved.stroke = mermaidColor(style.stroke);
        if (style.hasText) resolved.text = mermaidColor(style.text);
        if (style.hasStrokeWidth) {
            resolved.strokeWidth = style.strokeWidth * scale;
        }
    };

    auto defaultStyle = diagram.classStyles.find("default");
    if (defaultStyle != diagram.classStyles.end()) apply(defaultStyle->second);
    if (!node.className.empty()) {
        auto classStyle = diagram.classStyles.find(node.className);
        if (classStyle != diagram.classStyles.end()) apply(classStyle->second);
    }
    apply(node.style);
    return resolved;
}

static App::LayoutShapeType mermaidShapeType(mermaid::NodeShape shape) {
    switch (shape) {
        case mermaid::NodeShape::RoundedRectangle:
            return App::LayoutShapeType::RoundedRectangle;
        case mermaid::NodeShape::Diamond:
            return App::LayoutShapeType::Diamond;
        case mermaid::NodeShape::Stadium:
            return App::LayoutShapeType::Stadium;
        case mermaid::NodeShape::Circle:
            return App::LayoutShapeType::Ellipse;
        case mermaid::NodeShape::Hexagon:
            return App::LayoutShapeType::Hexagon;
        case mermaid::NodeShape::Rectangle:
        default:
            return App::LayoutShapeType::Rectangle;
    }
}

static bool layoutMermaidDiagram(App& app, const std::string& source,
                                 size_t sourceOffset, float& y,
                                 float indent, float maxWidth,
                                 D2D1_RECT_F* renderedBounds = nullptr) {
    auto parsed = mermaid::parse(source);
    if (!parsed.success || parsed.diagram.nodes.empty()) return false;

    const auto& diagram = parsed.diagram;
    float scale = app.contentScale * app.zoomFactor;
    float maxLabelWidth = 280.0f * scale;
    float measureHeight = 10000.0f * scale;
    float paddingX = 18.0f * scale;
    float paddingY = 12.0f * scale;
    float minimumWidth = 120.0f * scale;
    float minimumHeight = 52.0f * scale;

    std::vector<std::wstring> labels;
    std::vector<mermaid::Size> nodeSizes;
    std::vector<ResolvedMermaidStyle> styles;
    labels.reserve(diagram.nodes.size());
    nodeSizes.reserve(diagram.nodes.size());
    styles.reserve(diagram.nodes.size());

    for (const auto& node : diagram.nodes) {
        std::wstring label = toWide(node.label.empty() ? node.id : node.label);
        LayoutInfo measured = createWrappedLayout(
            app, label, app.textFormat, maxLabelWidth, measureHeight);
        float width = std::max(minimumWidth, measured.width + paddingX * 2.0f);
        float height = std::max(minimumHeight, measured.height + paddingY * 2.0f);
        if (measured.layout) measured.layout->Release();

        if (node.shape == mermaid::NodeShape::Diamond) {
            width = std::max(width * 1.28f, 150.0f * scale);
            height = std::max(height * 1.45f, 82.0f * scale);
        } else if (node.shape == mermaid::NodeShape::Hexagon) {
            width += 40.0f * scale;
        } else if (node.shape == mermaid::NodeShape::Circle) {
            float diameter = std::max(width, height);
            width = diameter;
            height = diameter;
        }

        labels.push_back(std::move(label));
        nodeSizes.push_back({width, height});
        styles.push_back(resolveMermaidStyle(app, diagram, node, scale));
    }

    struct MeasuredMermaidEdgeLabel {
        std::wstring text;
        float width = 0.0f;
        float height = 0.0f;
    };
    bool vertical = diagram.direction == mermaid::Direction::TopToBottom ||
                    diagram.direction == mermaid::Direction::BottomToTop;
    float labelPaddingX = 6.0f * scale;
    float labelPaddingY = 4.0f * scale;
    float rankGap = 78.0f * scale;
    std::vector<MeasuredMermaidEdgeLabel> edgeLabels(diagram.edges.size());
    for (size_t i = 0; i < diagram.edges.size(); i++) {
        if (diagram.edges[i].label.empty()) continue;

        auto& edgeLabel = edgeLabels[i];
        edgeLabel.text = toWide(diagram.edges[i].label);
        edgeLabel.width = std::min(
            180.0f * scale,
            std::max(60.0f * scale,
                     measureText(app, edgeLabel.text, app.textFormat) +
                         labelPaddingX * 2.0f));
        LayoutInfo measured = createWrappedLayout(
            app, edgeLabel.text, app.textFormat,
            edgeLabel.width - labelPaddingX * 2.0f, measureHeight);
        edgeLabel.height = std::max(
            28.0f * scale, measured.height + labelPaddingY * 2.0f);
        if (measured.layout) measured.layout->Release();

        float labelExtent = vertical ? edgeLabel.height : edgeLabel.width;
        rankGap = std::max(rankGap, labelExtent + 20.0f * scale);
    }

    mermaid::Layout graphLayout = mermaid::layout(
        diagram, nodeSizes, 32.0f * scale, rankGap);
    if (graphLayout.nodes.size() != diagram.nodes.size()) return false;

    float baseX = indent;
    if (graphLayout.width < maxWidth) {
        baseX += (maxWidth - graphLayout.width) * 0.5f;
    }
    float baseY = y + 10.0f * scale;
    float diagramLeft = baseX;
    float diagramTop = baseY;
    float diagramRight = baseX + graphLayout.width;
    float diagramBottom = baseY + graphLayout.height;

    std::vector<D2D1_RECT_F> nodeRects;
    nodeRects.reserve(graphLayout.nodes.size());
    for (const auto& rect : graphLayout.nodes) {
        nodeRects.push_back(D2D1::RectF(
            baseX + rect.left,
            baseY + rect.top,
            baseX + rect.right,
            baseY + rect.bottom));
    }

    struct MermaidTextItem {
        std::wstring text;
        D2D1_RECT_F rect{};
        D2D1_COLOR_F color{};
    };
    std::vector<MermaidTextItem> textItems;
    textItems.reserve(diagram.nodes.size() + diagram.edges.size());

    D2D1_COLOR_F connectorColor = app.theme.text;
    connectorColor.a = app.theme.isDark ? 0.7f : 0.6f;

    app.layoutConnectors.reserve(
        app.layoutConnectors.size() + diagram.edges.size());
    size_t exteriorLane = 0;
    std::vector<D2D1_RECT_F> placedLabelRects;
    placedLabelRects.reserve(diagram.edges.size());
    for (size_t edgeIndex = 0; edgeIndex < diagram.edges.size(); edgeIndex++) {
        const auto& edge = diagram.edges[edgeIndex];
        if (edge.from >= nodeRects.size() || edge.to >= nodeRects.size()) continue;

        const auto& from = nodeRects[edge.from];
        const auto& to = nodeRects[edge.to];
        App::LayoutConnector connector;
        connector.color = connectorColor;
        connector.stroke = 1.4f * scale * edge.strokeScale;
        connector.arrowSize = 8.0f * scale;
        connector.directed = edge.directed;
        connector.dashed = edge.dashed;

        float fromCenterX = (from.left + from.right) * 0.5f;
        float fromCenterY = (from.top + from.bottom) * 0.5f;
        float toCenterX = (to.left + to.right) * 0.5f;
        float toCenterY = (to.top + to.bottom) * 0.5f;
        bool selfLoop = edge.from == edge.to;

        // Edges that skip over intermediate ranks would cut straight through
        // the nodes between them (and drop their label onto whatever edge
        // happens to sit at the midpoint) — route those through an exterior
        // lane like back-edges instead
        bool skipsRanks = false;
        if (edge.from < graphLayout.ranks.size() &&
            edge.to < graphLayout.ranks.size()) {
            size_t fromRank = graphLayout.ranks[edge.from];
            size_t toRank = graphLayout.ranks[edge.to];
            skipsRanks = (fromRank < toRank ? toRank - fromRank
                                            : fromRank - toRank) > 1;
        }

        if (vertical) {
            bool topToBottom =
                diagram.direction == mermaid::Direction::TopToBottom;
            bool forward = !selfLoop && !skipsRanks &&
                (topToBottom ? toCenterY > fromCenterY : toCenterY < fromCenterY);
            if (selfLoop) {
                float lane = (36.0f + exteriorLane++ * 14.0f) * scale;
                D2D1_POINT_2F start = D2D1::Point2F(from.right, fromCenterY);
                D2D1_POINT_2F end = D2D1::Point2F(fromCenterX, from.bottom);
                float laneX = from.right + lane;
                float laneY = from.bottom + lane;
                connector.points = {
                    start,
                    D2D1::Point2F(laneX, start.y),
                    D2D1::Point2F(laneX, laneY),
                    D2D1::Point2F(end.x, laneY),
                    end,
                };
            } else if (forward) {
                D2D1_POINT_2F start = D2D1::Point2F(
                    fromCenterX, topToBottom ? from.bottom : from.top);
                D2D1_POINT_2F end = D2D1::Point2F(
                    toCenterX, topToBottom ? to.top : to.bottom);
                float middleY = (start.y + end.y) * 0.5f;
                connector.points = {
                    start,
                    D2D1::Point2F(start.x, middleY),
                    D2D1::Point2F(end.x, middleY),
                    end,
                };
            } else {
                float lane = (36.0f + exteriorLane++ * 14.0f) * scale;
                D2D1_POINT_2F start = D2D1::Point2F(from.right, fromCenterY);
                D2D1_POINT_2F end = D2D1::Point2F(to.right, toCenterY);
                float laneX = std::max(from.right, to.right) + lane;
                connector.points = {
                    start,
                    D2D1::Point2F(laneX, start.y),
                    D2D1::Point2F(laneX, end.y),
                    end,
                };
            }
        } else {
            bool leftToRight =
                diagram.direction == mermaid::Direction::LeftToRight;
            bool forward = !selfLoop && !skipsRanks &&
                (leftToRight ? toCenterX > fromCenterX : toCenterX < fromCenterX);
            if (selfLoop) {
                float lane = (36.0f + exteriorLane++ * 14.0f) * scale;
                D2D1_POINT_2F start = D2D1::Point2F(fromCenterX, from.bottom);
                D2D1_POINT_2F end = D2D1::Point2F(from.right, fromCenterY);
                float laneX = from.right + lane;
                float laneY = from.bottom + lane;
                connector.points = {
                    start,
                    D2D1::Point2F(start.x, laneY),
                    D2D1::Point2F(laneX, laneY),
                    D2D1::Point2F(laneX, end.y),
                    end,
                };
            } else if (forward) {
                D2D1_POINT_2F start = D2D1::Point2F(
                    leftToRight ? from.right : from.left, fromCenterY);
                D2D1_POINT_2F end = D2D1::Point2F(
                    leftToRight ? to.left : to.right, toCenterY);
                float middleX = (start.x + end.x) * 0.5f;
                connector.points = {
                    start,
                    D2D1::Point2F(middleX, start.y),
                    D2D1::Point2F(middleX, end.y),
                    end,
                };
            } else {
                float lane = (36.0f + exteriorLane++ * 14.0f) * scale;
                D2D1_POINT_2F start = D2D1::Point2F(fromCenterX, from.bottom);
                D2D1_POINT_2F end = D2D1::Point2F(toCenterX, to.bottom);
                float laneY = std::max(from.bottom, to.bottom) + lane;
                connector.points = {
                    start,
                    D2D1::Point2F(start.x, laneY),
                    D2D1::Point2F(end.x, laneY),
                    end,
                };
            }
        }

        connector.bounds = D2D1::RectF(
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest());
        for (const auto& point : connector.points) {
            connector.bounds.left = std::min(connector.bounds.left, point.x);
            connector.bounds.top = std::min(connector.bounds.top, point.y);
            connector.bounds.right = std::max(connector.bounds.right, point.x);
            connector.bounds.bottom = std::max(connector.bounds.bottom, point.y);
        }
        connector.bounds.left -= connector.arrowSize;
        connector.bounds.top -= connector.arrowSize;
        connector.bounds.right += connector.arrowSize;
        connector.bounds.bottom += connector.arrowSize;
        diagramLeft = std::min(diagramLeft, connector.bounds.left);
        diagramTop = std::min(diagramTop, connector.bounds.top);
        diagramRight = std::max(diagramRight, connector.bounds.right);
        diagramBottom = std::max(diagramBottom, connector.bounds.bottom);
        app.layoutConnectors.push_back(std::move(connector));

        if (!edge.label.empty()) {
            const auto& edgeLabel = edgeLabels[edgeIndex];
            const auto& points = app.layoutConnectors.back().points;
            size_t middle = points.size() / 2;
            const auto& middleStart = points[middle - 1];
            const auto& middleEnd = points[middle];
            float centerX = (middleStart.x + middleEnd.x) * 0.5f;
            float centerY = (middleStart.y + middleEnd.y) * 0.5f;
            auto chipAt = [&](float cx, float cy) {
                return D2D1::RectF(
                    cx - edgeLabel.width * 0.5f,
                    cy - edgeLabel.height * 0.5f,
                    cx + edgeLabel.width * 0.5f,
                    cy + edgeLabel.height * 0.5f);
            };
            D2D1_RECT_F labelRect = chipAt(centerX, centerY);

            // Parallel exterior lanes sit only a few pixels apart, so their
            // midpoint chips stack on top of each other — slide an
            // overlapping chip along its own segment until it finds space
            bool segVertical = std::abs(middleEnd.x - middleStart.x) <
                               std::abs(middleEnd.y - middleStart.y);
            float stepX = segVertical ? 0.0f : edgeLabel.width + 8.0f * scale;
            float stepY = segVertical ? edgeLabel.height + 8.0f * scale : 0.0f;
            auto overlapsPlaced = [&](const D2D1_RECT_F& rect) {
                for (const auto& placed : placedLabelRects) {
                    if (rect.left < placed.right && rect.right > placed.left &&
                        rect.top < placed.bottom && rect.bottom > placed.top) {
                        return true;
                    }
                }
                return false;
            };
            for (int attempt = 1; attempt <= 8 && overlapsPlaced(labelRect); attempt++) {
                float direction = (attempt % 2 == 1) ? 1.0f : -1.0f;
                float magnitude = (float)((attempt + 1) / 2);
                labelRect = chipAt(centerX + stepX * direction * magnitude,
                                   centerY + stepY * direction * magnitude);
            }
            placedLabelRects.push_back(labelRect);
            // Draw the label as a visible chip on the edge — an invisible
            // background-colored pill erases the line under it, which makes
            // edges look disconnected and labels look like floating text
            D2D1_COLOR_F chipStroke = connectorColor;
            chipStroke.a *= 0.6f;
            app.layoutShapes.push_back({
                App::LayoutShapeType::RoundedRectangle,
                labelRect,
                app.theme.codeBackground,
                chipStroke,
                1.2f * scale,
                4.0f * scale,
            });
            diagramLeft = std::min(diagramLeft, labelRect.left);
            diagramTop = std::min(diagramTop, labelRect.top);
            diagramRight = std::max(diagramRight, labelRect.right);
            diagramBottom = std::max(diagramBottom, labelRect.bottom);
            D2D1_RECT_F textRect = D2D1::RectF(
                labelRect.left + labelPaddingX,
                labelRect.top + labelPaddingY,
                labelRect.right - labelPaddingX,
                labelRect.bottom - labelPaddingY);
            textItems.push_back({edgeLabel.text, textRect, app.theme.text});
        }
    }

    app.layoutShapes.reserve(app.layoutShapes.size() + diagram.nodes.size());
    for (size_t i = 0; i < diagram.nodes.size(); i++) {
        const auto& node = diagram.nodes[i];
        const auto& rect = nodeRects[i];
        const auto& style = styles[i];

        app.layoutShapes.push_back({
            mermaidShapeType(node.shape),
            rect,
            style.fill,
            style.stroke,
            style.strokeWidth,
            8.0f * scale,
        });

        float insetX = paddingX;
        float insetY = paddingY;
        if (node.shape == mermaid::NodeShape::Diamond) {
            insetX = (rect.right - rect.left) * 0.18f;
            insetY = (rect.bottom - rect.top) * 0.18f;
        } else if (node.shape == mermaid::NodeShape::Hexagon) {
            insetX = (rect.right - rect.left) * 0.18f;
        }

        D2D1_RECT_F textRect = D2D1::RectF(
            rect.left + insetX,
            rect.top + insetY,
            rect.right - insetX,
            rect.bottom - insetY);
        textItems.push_back({labels[i], textRect, style.text});

        if (sourceOffset != SIZE_MAX) {
            size_t anchorOffset = sourceOffset + node.sourceOffset;
            if (app.scrollAnchors.empty() ||
                app.scrollAnchors.back().sourceOffset < anchorOffset) {
                app.scrollAnchors.push_back({anchorOffset, rect.top});
            }
        }
    }

    std::stable_sort(
        textItems.begin(), textItems.end(),
        [](const MermaidTextItem& left, const MermaidTextItem& right) {
            if (std::abs(left.rect.top - right.rect.top) > kLineBucketTolerance) {
                return left.rect.top < right.rect.top;
            }
            return left.rect.left < right.rect.left;
        });
    for (const auto& item : textItems) {
        LayoutInfo textLayout = createWrappedLayout(
            app, item.text, app.textFormat,
            item.rect.right - item.rect.left,
            item.rect.bottom - item.rect.top);
        size_t docStart = app.docText.size();
        app.docText += item.text;
        addTextRun(
            app, std::move(textLayout),
            D2D1::Point2F(item.rect.left, item.rect.top),
            item.rect, item.color, docStart, item.text.size(), true);
        app.docText += L"\n";
    }
    app.docText += L"\n";

    app.contentWidth = std::max(
        app.contentWidth,
        diagramRight + 40.0f * scale);
    if (app.focusMermaidOnNextLayout && !nodeRects.empty()) {
        std::vector<bool> hasIncoming(diagram.nodes.size(), false);
        for (const auto& edge : diagram.edges) {
            if (edge.to < hasIncoming.size()) hasIncoming[edge.to] = true;
        }
        size_t rootIndex = 0;
        for (size_t i = 0; i < hasIncoming.size(); i++) {
            if (!hasIncoming[i]) {
                rootIndex = i;
                break;
            }
        }

        float rootCenter = (nodeRects[rootIndex].left + nodeRects[rootIndex].right) * 0.5f;
        float viewportWidth = documentViewportWidth(app);
        float maxScroll = std::max(0.0f, app.contentWidth - viewportWidth);
        float focusedScroll = rootCenter - viewportWidth * 0.5f;
        app.scrollX = std::max(0.0f, std::min(focusedScroll, maxScroll));
        app.targetScrollX = app.scrollX;
        app.focusMermaidOnNextLayout = false;
    }
    if (renderedBounds) {
        *renderedBounds =
            D2D1::RectF(diagramLeft, diagramTop, diagramRight, diagramBottom);
    }
    y = diagramBottom + 24.0f * scale;
    return true;
}

// --- extended Mermaid diagram families (sequence, pie, state, ...) ---

static void rgbToHsl(float r, float g, float b, float& h, float& s, float& l) {
    float maxC = std::max(r, std::max(g, b));
    float minC = std::min(r, std::min(g, b));
    l = (maxC + minC) * 0.5f;
    if (maxC == minC) {
        h = 0.0f;
        s = 0.0f;
        return;
    }
    float d = maxC - minC;
    s = l > 0.5f ? d / (2.0f - maxC - minC) : d / (maxC + minC);
    if (maxC == r) {
        h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    } else if (maxC == g) {
        h = (b - r) / d + 2.0f;
    } else {
        h = (r - g) / d + 4.0f;
    }
    h /= 6.0f;
}

static float hueChannel(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 0.5f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

static void hslToRgb(float h, float s, float l, float& r, float& g, float& b) {
    if (s == 0.0f) {
        r = g = b = l;
        return;
    }
    float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    r = hueChannel(p, q, h + 1.0f / 3.0f);
    g = hueChannel(p, q, h);
    b = hueChannel(p, q, h - 1.0f / 3.0f);
}

// Categorical palette derived from the theme accent: golden-angle hue steps
// keep neighboring series distinct in any theme
static D2D1_COLOR_F diagramSeriesColor(const App& app, int index) {
    float h, s, l;
    rgbToHsl(app.theme.accent.r, app.theme.accent.g, app.theme.accent.b,
             h, s, l);
    h = std::fmod(h + index * 0.38197f, 1.0f);
    s = app.theme.isDark ? 0.42f : 0.48f;
    l = app.theme.isDark ? 0.60f : 0.52f;
    float r, g, b;
    hslToRgb(h, s, l, r, g, b);
    return D2D1::ColorF(r, g, b, 1.0f);
}

static D2D1_COLOR_F resolveDiagramRole(const App& app,
                                       const mermaidext::Prim& prim,
                                       mermaidext::Role role) {
    int seriesIndex = prim.seriesIndex;
    D2D1_COLOR_F color = app.theme.text;
    switch (role) {
        case mermaidext::Role::Text:
            color = app.theme.text;
            break;
        case mermaidext::Role::Muted:
            color = app.theme.text;
            color.a = app.theme.isDark ? 0.72f : 0.62f;
            break;
        case mermaidext::Role::Stroke:
            color = app.theme.accent;
            break;
        case mermaidext::Role::Fill:
            color = app.theme.codeBackground;
            break;
        case mermaidext::Role::Accent:
            color = app.theme.accent;
            break;
        case mermaidext::Role::AccentSoft:
            color = app.theme.accent;
            color.a = app.theme.isDark ? 0.20f : 0.13f;
            break;
        case mermaidext::Role::Background:
            color = app.theme.background;
            break;
        case mermaidext::Role::Series:
            color = diagramSeriesColor(app, seriesIndex);
            break;
        case mermaidext::Role::SeriesSoft:
            color = diagramSeriesColor(app, seriesIndex);
            color.a = 0.30f;
            break;
        case mermaidext::Role::Custom:
            color = D2D1::ColorF(prim.customR, prim.customG, prim.customB);
            break;
        case mermaidext::Role::None:
            color.a = 0.0f;
            break;
    }
    return color;
}

// Per-layout-pass cache of text formats for the diagram text styles
struct DiagramFormats {
    App& app;
    std::map<int, IDWriteTextFormat*> formats;

    explicit DiagramFormats(App& application) : app(application) {}
    DiagramFormats(const DiagramFormats&) = delete;
    DiagramFormats& operator=(const DiagramFormats&) = delete;
    ~DiagramFormats() {
        for (auto& entry : formats) {
            if (entry.second) entry.second->Release();
        }
    }

    IDWriteTextFormat* get(const mermaidext::TextStyle& style, float scale) {
        int key = (style.bold ? 1 : 0) | (style.italic ? 2 : 0) |
                  (style.mono ? 4 : 0) |
                  (static_cast<int>(style.scale * 100.0f + 0.5f) << 3);
        auto found = formats.find(key);
        if (found != formats.end()) return found->second;

        const wchar_t* family =
            style.mono ? app.theme.codeFontFamily : app.theme.fontFamily;
        float size = (style.mono ? 13.0f : 14.0f) * scale * style.scale;
        IDWriteTextFormat* format = nullptr;
        app.dwriteFactory->CreateTextFormat(
            family, nullptr,
            style.bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD
                       : DWRITE_FONT_WEIGHT_NORMAL,
            style.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &format);
        formats[key] = format;
        return format;
    }
};

static IDWriteTextLayout* createDiagramTextLayout(
        App& app, DiagramFormats& formats, const std::wstring& text,
        const mermaidext::TextStyle& style, float scale, float width,
        float height, int alignH, int alignV) {
    IDWriteTextFormat* format = formats.get(style, scale);
    if (!format || text.empty()) return nullptr;
    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(
        text.data(), static_cast<UINT32>(text.size()), format,
        std::max(width, 1.0f), std::max(height, 1.0f), &layout);
    if (!layout) return nullptr;
    layout->SetTextAlignment(alignH < 0 ? DWRITE_TEXT_ALIGNMENT_LEADING
                             : alignH > 0 ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                          : DWRITE_TEXT_ALIGNMENT_CENTER);
    layout->SetParagraphAlignment(alignV < 0
                                      ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
                                  : alignV > 0 ? DWRITE_PARAGRAPH_ALIGNMENT_FAR
                                               : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (app.fontFallback) {
        IDWriteTextLayout2* layout2 = nullptr;
        if (SUCCEEDED(layout->QueryInterface(
                __uuidof(IDWriteTextLayout2),
                reinterpret_cast<void**>(&layout2)))) {
            layout2->SetFontFallback(app.fontFallback);
            layout2->Release();
        }
    }
    return layout;
}

static ID2D1PathGeometry* buildPolygonGeometry(
        App& app, const std::vector<mermaidext::Point>& points,
        float originX, float originY) {
    if (points.size() < 3) return nullptr;
    ID2D1PathGeometry* geometry = nullptr;
    if (FAILED(app.d2dFactory->CreatePathGeometry(&geometry)) || !geometry) {
        return nullptr;
    }
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(geometry->Open(&sink)) || !sink) {
        geometry->Release();
        return nullptr;
    }
    sink->BeginFigure(
        D2D1::Point2F(points[0].x - originX, points[0].y - originY),
        D2D1_FIGURE_BEGIN_FILLED);
    for (size_t i = 1; i < points.size(); i++) {
        sink->AddLine(
            D2D1::Point2F(points[i].x - originX, points[i].y - originY));
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    sink->Release();
    return geometry;
}

static ID2D1PathGeometry* buildSliceGeometry(App& app, float radius,
                                             float a0, float a1) {
    // Local space: circle center at (radius, radius)
    ID2D1PathGeometry* geometry = nullptr;
    if (FAILED(app.d2dFactory->CreatePathGeometry(&geometry)) || !geometry) {
        return nullptr;
    }
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(geometry->Open(&sink)) || !sink) {
        geometry->Release();
        return nullptr;
    }
    float cx = radius, cy = radius;
    D2D1_POINT_2F start =
        D2D1::Point2F(cx + radius * std::cos(a0), cy + radius * std::sin(a0));
    D2D1_POINT_2F end =
        D2D1::Point2F(cx + radius * std::cos(a1), cy + radius * std::sin(a1));
    float sweep = a1 - a0;
    if (sweep >= 6.28318f - 0.0001f) {
        // Full circle: two half arcs
        sink->BeginFigure(start, D2D1_FIGURE_BEGIN_FILLED);
        D2D1_POINT_2F opposite = D2D1::Point2F(
            cx + radius * std::cos(a0 + 3.14159f),
            cy + radius * std::sin(a0 + 3.14159f));
        D2D1_ARC_SEGMENT half1 = {
            opposite, {radius, radius}, 0.0f,
            D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL};
        D2D1_ARC_SEGMENT half2 = {
            start, {radius, radius}, 0.0f,
            D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL};
        sink->AddArc(half1);
        sink->AddArc(half2);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    } else {
        sink->BeginFigure(D2D1::Point2F(cx, cy), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(start);
        D2D1_ARC_SEGMENT arc = {
            end, {radius, radius}, 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
            sweep > 3.14159f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL};
        sink->AddArc(arc);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
    sink->Close();
    sink->Release();
    return geometry;
}

static bool layoutMermaidExtDiagram(App& app, mermaidext::Kind kind,
                                    const std::string& source,
                                    size_t sourceOffset, float& y,
                                    float indent, float maxWidth,
                                    D2D1_RECT_F* renderedBounds) {
    float scale = app.contentScale * app.zoomFactor;
    DiagramFormats formats(app);

    auto measureFn = [&](const std::string& text,
                         const mermaidext::TextStyle& style,
                         float wrapWidth) -> mermaidext::Size {
        if (text.empty()) return {};
        std::wstring wide = toWide(text);
        IDWriteTextLayout* layout = createDiagramTextLayout(
            app, formats, wide, style, scale,
            wrapWidth > 0.0f ? wrapWidth : kHugeWidth, kHugeWidth, -1, -1);
        if (!layout) return {};
        if (wrapWidth <= 0.0f) {
            layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        layout->Release();
        return {metrics.widthIncludingTrailingWhitespace, metrics.height};
    };

    mermaidext::Built built =
        mermaidext::build(kind, source, measureFn, scale);
    if (!built.ok || built.prims.empty()) return false;

    float baseX = indent;
    if (built.width < maxWidth) {
        baseX += (maxWidth - built.width) * 0.5f;
    }
    float baseY = y + 10.0f * scale;

    if (sourceOffset != SIZE_MAX) {
        if (app.scrollAnchors.empty() ||
            app.scrollAnchors.back().sourceOffset < sourceOffset) {
            app.scrollAnchors.push_back({sourceOffset, baseY});
        }
    }

    // Text prims are appended to docText in reading order so selection
    // sweeps the diagram top-to-bottom
    std::vector<const mermaidext::Prim*> textPrims;

    for (const auto& prim : built.prims) {
        float x1 = baseX + prim.x1, y1 = baseY + prim.y1;
        float x2 = baseX + prim.x2, y2 = baseY + prim.y2;
        D2D1_COLOR_F fill = resolveDiagramRole(app, prim, prim.fill);
        D2D1_COLOR_F stroke = resolveDiagramRole(app, prim, prim.stroke);
        switch (prim.type) {
            case mermaidext::PrimType::Rect:
            case mermaidext::PrimType::RoundRect:
            case mermaidext::PrimType::Ellipse: {
                App::LayoutShape shape;
                shape.type =
                    prim.type == mermaidext::PrimType::Rect
                        ? App::LayoutShapeType::Rectangle
                    : prim.type == mermaidext::PrimType::RoundRect
                        ? App::LayoutShapeType::RoundedRectangle
                        : App::LayoutShapeType::Ellipse;
                shape.rect = D2D1::RectF(x1, y1, x2, y2);
                shape.fill = fill;
                shape.stroke = stroke;
                shape.strokeWidth = prim.strokeWidth;
                shape.radius = prim.radius;
                app.layoutShapes.push_back(shape);
                break;
            }
            case mermaidext::PrimType::Line: {
                App::LayoutConnector connector;
                connector.color = stroke;
                connector.stroke = prim.strokeWidth;
                connector.dashed = prim.dashed;
                connector.arrowSize = 8.0f * scale;
                connector.directed = prim.openArrow;
                float endX = x2, endY = y2;
                if (prim.arrow) {
                    // Filled triangle head: shorten the line, add a polygon
                    float dx = x2 - x1, dy = y2 - y1;
                    float length = std::sqrt(dx * dx + dy * dy);
                    if (length > 0.01f) {
                        dx /= length;
                        dy /= length;
                        float head = 9.0f * scale;
                        float wing = 4.5f * scale;
                        endX = x2 - dx * head * 0.7f;
                        endY = y2 - dy * head * 0.7f;
                        std::vector<mermaidext::Point> tri = {
                            {x2, y2},
                            {x2 - dx * head + dy * wing,
                             y2 - dy * head - dx * wing},
                            {x2 - dx * head - dy * wing,
                             y2 - dy * head + dx * wing},
                        };
                        float minX = std::min({tri[0].x, tri[1].x, tri[2].x});
                        float minY = std::min({tri[0].y, tri[1].y, tri[2].y});
                        float maxX = std::max({tri[0].x, tri[1].x, tri[2].x});
                        float maxY = std::max({tri[0].y, tri[1].y, tri[2].y});
                        App::LayoutShape triangle;
                        triangle.type = App::LayoutShapeType::Path;
                        triangle.rect = D2D1::RectF(minX, minY, maxX, maxY);
                        triangle.fill = stroke;
                        triangle.stroke.a = 0.0f;
                        triangle.geometry =
                            buildPolygonGeometry(app, tri, minX, minY);
                        app.layoutShapes.push_back(triangle);
                    }
                }
                connector.points = {D2D1::Point2F(x1, y1),
                                    D2D1::Point2F(endX, endY)};
                connector.bounds = D2D1::RectF(
                    std::min(x1, x2) - 10.0f * scale,
                    std::min(y1, y2) - 10.0f * scale,
                    std::max(x1, x2) + 10.0f * scale,
                    std::max(y1, y2) + 10.0f * scale);
                app.layoutConnectors.push_back(std::move(connector));
                break;
            }
            case mermaidext::PrimType::Polygon: {
                if (prim.pts.size() < 3) break;
                std::vector<mermaidext::Point> shifted = prim.pts;
                float minX = shifted[0].x, minY = shifted[0].y;
                float maxX = minX, maxY = minY;
                for (auto& point : shifted) {
                    point.x += baseX;
                    point.y += baseY;
                }
                minX = maxX = shifted[0].x;
                minY = maxY = shifted[0].y;
                for (const auto& point : shifted) {
                    minX = std::min(minX, point.x);
                    minY = std::min(minY, point.y);
                    maxX = std::max(maxX, point.x);
                    maxY = std::max(maxY, point.y);
                }
                App::LayoutShape shape;
                shape.type = App::LayoutShapeType::Path;
                shape.rect = D2D1::RectF(minX, minY, maxX, maxY);
                shape.fill = fill;
                shape.stroke = stroke;
                shape.strokeWidth = prim.strokeWidth;
                shape.geometry = buildPolygonGeometry(app, shifted, minX, minY);
                app.layoutShapes.push_back(shape);
                break;
            }
            case mermaidext::PrimType::Slice: {
                App::LayoutShape shape;
                shape.type = App::LayoutShapeType::Path;
                shape.rect = D2D1::RectF(
                    x1 - prim.radius, y1 - prim.radius,
                    x1 + prim.radius, y1 + prim.radius);
                shape.fill = fill;
                shape.stroke = stroke;
                shape.strokeWidth = prim.strokeWidth;
                shape.geometry =
                    buildSliceGeometry(app, prim.radius, prim.a0, prim.a1);
                app.layoutShapes.push_back(shape);
                break;
            }
            case mermaidext::PrimType::Text:
                textPrims.push_back(&prim);
                break;
        }
    }

    std::stable_sort(textPrims.begin(), textPrims.end(),
                     [](const mermaidext::Prim* a, const mermaidext::Prim* b) {
                         if (std::abs(a->y1 - b->y1) > kLineBucketTolerance) {
                             return a->y1 < b->y1;
                         }
                         return a->x1 < b->x1;
                     });
    for (const mermaidext::Prim* prim : textPrims) {
        std::wstring wide = toWide(prim->text);
        if (wide.empty()) continue;
        D2D1_RECT_F rect = D2D1::RectF(baseX + prim->x1, baseY + prim->y1,
                                       baseX + prim->x2, baseY + prim->y2);
        // Slight width slack: boxes are sized from measured text, and
        // re-wrapping at the exact measured width can push the last word
        // (or a CJK character) onto a phantom second line
        IDWriteTextLayout* layout = createDiagramTextLayout(
            app, formats, wide, prim->style, scale,
            rect.right - rect.left + 2.5f, rect.bottom - rect.top + 2.0f,
            prim->alignH, prim->alignV);
        if (!layout) continue;
        LayoutInfo info;
        info.layout = layout;
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        info.width = metrics.widthIncludingTrailingWhitespace;
        info.height = metrics.height;
        size_t docStart = app.docText.size();
        app.docText += wide;
        addTextRun(app, std::move(info),
                   D2D1::Point2F(rect.left, rect.top), rect,
                   resolveDiagramRole(app, *prim, prim->fill),
                   docStart, wide.size(), true);
        app.docText += L"\n";
    }
    app.docText += L"\n";

    float diagramRight = baseX + built.width;
    float diagramBottom = baseY + built.height;
    app.contentWidth = std::max(app.contentWidth,
                                diagramRight + 40.0f * scale);
    if (renderedBounds) {
        *renderedBounds =
            D2D1::RectF(baseX, baseY, diagramRight, diagramBottom);
    }
    y = diagramBottom + 24.0f * scale;
    return true;
}

static void layoutCodeBlock(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    std::string code;
    for (const auto& child : elem->children) {
        if (child->type == ElementType::Text) {
            code += child->text;
        }
    }

    std::string languageName = elem->language;
    std::transform(
        languageName.begin(), languageName.end(), languageName.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (languageName == "mermaid") {
        D2D1_RECT_F renderedBounds{};
        mermaidext::Kind kind = mermaidext::detectKind(code);
        bool rendered = false;
        if (kind == mermaidext::Kind::Flowchart) {
            rendered = layoutMermaidDiagram(
                app, code, elem->sourceOffset, y, indent, maxWidth,
                &renderedBounds);
        } else if (kind != mermaidext::Kind::None) {
            rendered = layoutMermaidExtDiagram(
                app, kind, code, elem->sourceOffset, y, indent, maxWidth,
                &renderedBounds);
        }
        if (rendered) {
            app.codeBlocks.push_back({renderedBounds, toWide(code)});
            return;
        }
    }

    std::wstring langHint = toWide(elem->language);
    int language = detectLanguage(langHint);

    float scale = app.contentScale * app.zoomFactor;
    float lineHeight = 20.0f * scale;
    float padding = 12.0f * scale;

    int lineCount = 1;
    for (char c : code) if (c == '\n') lineCount++;

    app.docText += L"\n";

    float blockHeight = lineCount * lineHeight + padding * 2;
    size_t bgRectIndex = app.layoutRects.size();
    app.layoutRects.push_back({D2D1::RectF(indent, y, indent + maxWidth, y + blockHeight),
                               app.theme.codeBackground});

    std::wstring wcode = toWide(code);

    // Track code block for copy button
    app.codeBlocks.push_back({
        D2D1::RectF(indent, y, indent + maxWidth, y + blockHeight),
        wcode
    });
    float textY = y + padding;
    bool inBlockComment = false;
    size_t codeDocStart = app.docText.size();
    size_t lineStart = 0;
    float maxLineWidth = 0.0f;

    while (lineStart <= wcode.length()) {
        size_t lineEnd = wcode.find(L'\n', lineStart);
        if (lineEnd == std::wstring::npos) lineEnd = wcode.length();

        std::wstring wline = wcode.substr(lineStart, lineEnd - lineStart);
        if (!wline.empty() && wline.back() == L'\r') wline.pop_back();

        size_t lineDocStart = codeDocStart + lineStart;
        float lineWidth = 0.0f;
        size_t lineRun = (size_t)-1;

        if (language > 0) {
            std::vector<SyntaxToken> tokens = tokenizeLine(wline, language, inBlockComment);
            float tokenX = indent + padding;

            // Merge consecutive same-color tokens into one layout — a line
            // typically collapses to a handful of color runs instead of one
            // IDWriteTextLayout per token.
            size_t ti = 0;
            while (ti < tokens.size()) {
                if (tokens[ti].text.empty()) { ti++; continue; }

                D2D1_COLOR_F runColor = getTokenColor(app.theme, tokens[ti].tokenType);
                std::wstring_view runText = tokens[ti].text;
                size_t tj = ti + 1;
                while (tj < tokens.size()) {
                    const auto& next = tokens[tj];
                    if (next.text.empty()) { tj++; continue; }
                    D2D1_COLOR_F c = getTokenColor(app.theme, next.tokenType);
                    bool sameColor = c.r == runColor.r && c.g == runColor.g &&
                                     c.b == runColor.b && c.a == runColor.a;
                    // Tokens are views into wline; only merge physically
                    // adjacent ones so the combined view stays valid
                    bool adjacent = next.text.data() == runText.data() + runText.size();
                    if (!sameColor || !adjacent) break;
                    runText = std::wstring_view(runText.data(), runText.size() + next.text.size());
                    tj++;
                }

                LayoutInfo info = createLayout(app, runText, app.codeFormat, lineHeight, app.codeTypography);
                float runWidth = info.width;

                D2D1_POINT_2F pos = D2D1::Point2F(tokenX, textY);
                D2D1_RECT_F bounds = D2D1::RectF(tokenX, textY, tokenX + runWidth, textY + lineHeight);
                addTextRun(app, std::move(info), pos, bounds, runColor,
                           lineDocStart, 0, false);

                tokenX += runWidth;
                lineWidth += runWidth;
                ti = tj;
            }
        } else {
            LayoutInfo info = createLayout(app, wline, app.codeFormat, lineHeight, app.codeTypography);
            lineWidth = info.width;
            D2D1_POINT_2F pos = D2D1::Point2F(indent + padding, textY);
            D2D1_RECT_F bounds = D2D1::RectF(indent + padding, textY,
                                             indent + padding + lineWidth, textY + lineHeight);
            size_t runsBefore = app.layoutTextRuns.size();
            addTextRun(app, std::move(info), pos, bounds, app.theme.code,
                       lineDocStart, wline.length(), false);
            if (app.layoutTextRuns.size() > runsBefore) {
                lineRun = app.layoutTextRuns.size() - 1;
            }
        }

        if (!wline.empty()) {
            D2D1_RECT_F lineBounds = D2D1::RectF(indent + padding, textY,
                indent + padding + lineWidth, textY + lineHeight);
            // Syntax-highlighted lines have no single covering layout;
            // monospace interpolation stays accurate there
            addTextRect(app, lineBounds, lineDocStart, wline.length(), lineRun);
        }

        maxLineWidth = std::max(maxLineWidth, lineWidth);
        textY += lineHeight;
        if (lineEnd == wcode.length()) break;
        lineStart = lineEnd + 1;
    }

    // Long code lines used to be clipped at the block edge with no way to
    // reach them — extend the block background and the document width so
    // wide code participates in horizontal scrolling like diagrams do
    float widestExtent = maxLineWidth + padding * 2;
    if (widestExtent > maxWidth) {
        app.layoutRects[bgRectIndex].rect.right = indent + widestExtent;
        app.contentWidth = std::max(app.contentWidth, indent + widestExtent);
    }

    app.docText += wcode;
    app.docText += L"\n\n";
    y += blockHeight + 14 * scale;
}

static void layoutBlockquote(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    float quoteIndent = 20.0f * scale;
    float startY = y;

    // GitHub alert callouts: accent-colored bar plus a bold title line.
    // Colors are github.com's light/dark alert accents, picked by theme.
    static const struct {
        const wchar_t* title;
        uint32_t light;
        uint32_t dark;
    } ALERT_STYLES[] = {
        {L"\u24D8  Note",            0x0969DA, 0x4493F8},  // circled info
        {L"\U0001F4A1\uFE0E  Tip",   0x1A7F37, 0x3FB950},  // bulb, text presentation
        {L"\u2757\uFE0E  Important", 0x8250DF, 0xAB7DF8},  // exclamation
        {L"\u26A0\uFE0E  Warning",   0x9A6700, 0xD29922},  // warning triangle
        {L"\u26D4\uFE0E  Caution",   0xCF222E, 0xF85149},  // no-entry
    };

    D2D1_COLOR_F barColor = app.theme.blockquoteBorder;
    if (elem->alertKind >= 1 && elem->alertKind <= 5) {
        const auto& style = ALERT_STYLES[elem->alertKind - 1];
        barColor = hexColor(app.theme.isDark ? style.dark : style.light);

        std::wstring title = style.title;
        LayoutInfo info = createLayout(app, title, app.textFormat, 24.0f, app.bodyTypography);
        if (info.layout) {
            DWRITE_TEXT_RANGE range = {0, (UINT32)title.length()};
            info.layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, range);
        }
        D2D1_POINT_2F pos = D2D1::Point2F(indent + quoteIndent, y);
        D2D1_RECT_F bounds = D2D1::RectF(indent + quoteIndent, y,
                                         indent + maxWidth, y + 24 * scale);
        addTextRun(app, std::move(info), pos, bounds, barColor, 0, 0, false);
        y += 30 * scale;
    }

    for (const auto& child : elem->children) {
        layoutElement(app, child, y, indent + quoteIndent, maxWidth - quoteIndent);
    }

    app.layoutRects.push_back({D2D1::RectF(indent, startY, indent + 4, y), barColor});
}

static void layoutList(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    float listIndent = 24.0f * scale;
    int itemNum = elem->start;

    for (const auto& child : elem->children) {
        if (child->type != ElementType::ListItem) continue;

        if (child->isTask) {
            // Task item: a clickable checkbox replaces the bullet
            float box = 15.0f * scale;
            float boxX = indent + 1.0f * scale;
            float boxY = y + 4.0f * scale;
            D2D1_RECT_F boxRect = D2D1::RectF(boxX, boxY, boxX + box, boxY + box);
            if (child->taskChecked) {
                app.layoutRects.push_back({boxRect, app.theme.accent});
                D2D1_COLOR_F mark = app.theme.isDark
                    ? D2D1::ColorF(0.08f, 0.08f, 0.10f) : D2D1::ColorF(1, 1, 1);
                app.layoutLines.push_back({
                    D2D1::Point2F(boxX + box * 0.24f, boxY + box * 0.52f),
                    D2D1::Point2F(boxX + box * 0.44f, boxY + box * 0.74f),
                    mark, 2.0f * scale});
                app.layoutLines.push_back({
                    D2D1::Point2F(boxX + box * 0.44f, boxY + box * 0.74f),
                    D2D1::Point2F(boxX + box * 0.78f, boxY + box * 0.28f),
                    mark, 2.0f * scale});
            } else {
                D2D1_COLOR_F edge = app.theme.blockquoteBorder;
                float w = 1.2f * scale;
                app.layoutLines.push_back({{boxX, boxY}, {boxX + box, boxY}, edge, w});
                app.layoutLines.push_back({{boxX, boxY + box}, {boxX + box, boxY + box}, edge, w});
                app.layoutLines.push_back({{boxX, boxY}, {boxX, boxY + box}, edge, w});
                app.layoutLines.push_back({{boxX + box, boxY}, {boxX + box, boxY + box}, edge, w});
            }
            if (child->taskMarkOffset != SIZE_MAX) {
                app.taskRects.push_back({
                    D2D1::RectF(boxX - 4 * scale, boxY - 4 * scale,
                                boxX + box + 4 * scale, boxY + box + 4 * scale),
                    child->taskMarkOffset, child->taskChecked});
            }
        } else {
            std::wstring marker = elem->ordered ?
                std::to_wstring(itemNum++) + L"." : L"\x2022";

            LayoutInfo info = createLayout(app, marker, app.textFormat, 24.0f, app.bodyTypography);
            D2D1_POINT_2F pos = D2D1::Point2F(indent, y);
            D2D1_RECT_F bounds = D2D1::RectF(indent, y, indent + listIndent, y + 24);
            addTextRun(app, std::move(info), pos, bounds, app.theme.text, 0, 0, false);
        }

        bool hasBlockChildren = false;
        for (const auto& itemChild : child->children) {
            if (itemChild->type == ElementType::Paragraph ||
                itemChild->type == ElementType::List ||
                itemChild->type == ElementType::CodeBlock ||
                itemChild->type == ElementType::BlockQuote) {
                hasBlockChildren = true;
                break;
            }
        }

        float itemStartY = y;
        if (hasBlockChildren) {
            std::vector<ElementPtr> inlineElements, blockElements;
            for (const auto& itemChild : child->children) {
                if (itemChild->type == ElementType::Paragraph ||
                    itemChild->type == ElementType::List ||
                    itemChild->type == ElementType::CodeBlock ||
                    itemChild->type == ElementType::BlockQuote) {
                    blockElements.push_back(itemChild);
                } else {
                    inlineElements.push_back(itemChild);
                }
            }

            if (!inlineElements.empty()) {
                layoutInlineContent(app, inlineElements, indent + listIndent, y,
                    maxWidth - listIndent, app.textFormat, app.theme.text);
            }

            for (const auto& blockChild : blockElements) {
                layoutElement(app, blockChild, y, indent + listIndent, maxWidth - listIndent);
            }
        } else {
            layoutInlineContent(app, child->children, indent + listIndent, y,
                maxWidth - listIndent, app.textFormat, app.theme.text);
        }

        app.docText += L"\n\n";

        if (y < itemStartY + 28 * scale) {
            y = itemStartY + 28 * scale;
        }
    }
    y += 8 * scale;
}

// Result of a background image download+decode, handed to the UI thread
// via WM_APP_IMAGE_READY. Pixels are premultiplied BGRA.
struct AsyncImageResult {
    std::string src;
    UINT width = 0;
    UINT height = 0;
    std::vector<uint8_t> pixels;
    bool ok = false;
};

// Runs on a worker thread: blocking download + WIC decode never touch the
// UI thread, so dead links can't stall layout (#44). The worker owns its own
// COM apartment and WIC factory; only plain pixel bytes cross the thread
// boundary.
static void asyncImageWorker(HWND hwnd, std::string src) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto* result = new AsyncImageResult();
    result->src = src;

    wchar_t tempPath[MAX_PATH] = {};
    std::wstring wideSrc = toWide(src);
    if (SUCCEEDED(URLDownloadToCacheFileW(nullptr, wideSrc.c_str(), tempPath, MAX_PATH, 0, nullptr))) {
        IWICImagingFactory* wic = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&wic))) && wic) {
            IWICBitmapDecoder* decoder = nullptr;
            if (SUCCEEDED(wic->CreateDecoderFromFilename(tempPath, nullptr, GENERIC_READ,
                                                         WICDecodeMetadataCacheOnDemand, &decoder)) && decoder) {
                IWICBitmapFrameDecode* frame = nullptr;
                if (SUCCEEDED(decoder->GetFrame(0, &frame)) && frame) {
                    IWICFormatConverter* converter = nullptr;
                    if (SUCCEEDED(wic->CreateFormatConverter(&converter)) && converter) {
                        if (SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                            UINT w = 0, h = 0;
                            converter->GetSize(&w, &h);
                            if (w > 0 && h > 0 && w < 16384 && h < 16384) {
                                result->pixels.resize((size_t)w * h * 4);
                                if (SUCCEEDED(converter->CopyPixels(nullptr, w * 4,
                                        (UINT)result->pixels.size(), result->pixels.data()))) {
                                    result->width = w;
                                    result->height = h;
                                    result->ok = true;
                                }
                            }
                        }
                        converter->Release();
                    }
                    frame->Release();
                }
                decoder->Release();
            }
            wic->Release();
        }
    }

    if (!PostMessageW(hwnd, WM_APP_IMAGE_READY, 0, (LPARAM)result)) {
        delete result;  // window already gone
    }
    CoUninitialize();
}

static App::ImageEntry& getOrLoadImage(App& app, const std::string& src) {
    auto it = app.imageCache.find(src);
    if (it != app.imageCache.end()) {
        app.touchImageCacheEntry(it->second);
        return it->second;
    }

    App::ImageEntry entry;
    entry.failed = true;  // assume failure

    if (!app.wicFactory || !app.renderTarget) {
        app.storeImageCacheEntry(src, std::move(entry));
        return app.imageCache[src];
    }

    std::wstring widePath;

    // Remote images load on a worker thread — a synchronous download here
    // stalled layout for seconds per unreachable URL (#44)
    bool isUrl = (src.rfind("http://", 0) == 0 || src.rfind("https://", 0) == 0);
    if (isUrl) {
        entry.failed = false;
        entry.pending = true;
        app.storeImageCacheEntry(src, std::move(entry));
        std::thread(asyncImageWorker, app.hwnd, src).detach();
        return app.imageCache[src];
    } else {
        // Resolve relative to the current file's directory. Both strings are
        // UTF-8 and must be widened explicitly: a narrow filesystem::path
        // decodes through the ANSI codepage and mangles non-ASCII paths (#87)
        std::wstring wsrc = toWide(src);
        if (!app.currentFile.empty()) {
            std::filesystem::path basePath(toWide(app.currentFile));
            std::filesystem::path imgPath = basePath.parent_path() / wsrc;
            widePath = imgPath.wstring();
        } else {
            widePath = wsrc;
        }
    }

    // Load via WIC
    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = app.wicFactory->CreateDecoderFromFilename(widePath.c_str(), nullptr,
        GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) {
        app.storeImageCacheEntry(src, std::move(entry));
        return app.imageCache[src];
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        decoder->Release();
        app.storeImageCacheEntry(src, std::move(entry));
        return app.imageCache[src];
    }

    IWICFormatConverter* converter = nullptr;
    hr = app.wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        frame->Release();
        decoder->Release();
        app.storeImageCacheEntry(src, std::move(entry));
        return app.imageCache[src];
    }

    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        app.storeImageCacheEntry(src, std::move(entry));
        return app.imageCache[src];
    }

    ID2D1Bitmap* bitmap = nullptr;
    hr = app.renderTarget->CreateBitmapFromWicBitmap(converter, nullptr, &bitmap);
    converter->Release();
    frame->Release();
    decoder->Release();

    if (SUCCEEDED(hr) && bitmap) {
        D2D1_SIZE_F size = bitmap->GetSize();
        entry.bitmap = bitmap;
        entry.width = (int)size.width;
        entry.height = (int)size.height;
        entry.failed = false;
    }

    app.storeImageCacheEntry(src, std::move(entry));
    return app.imageCache[src];
}

static void layoutImage(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    auto& entry = getOrLoadImage(app, elem->url);

    if (entry.failed || !entry.bitmap) {
        // Render alt text as placeholder
        std::wstring altText = L"[image";
        std::wstring alt;
        std::function<void(const ElementPtr&)> extract = [&](const ElementPtr& e) {
            if (!e) return;
            if (e->type == ElementType::Text) alt += toWide(e->text);
            else for (const auto& c : e->children) extract(c);
        };
        for (const auto& c : elem->children) extract(c);
        if (!alt.empty()) {
            altText += L": " + alt;
        }
        altText += L"]";

        float lineHeight = app.italicFormat->GetFontSize() * 1.7f;
        size_t docStart = app.docText.size();
        LayoutInfo info = createLayout(app, altText, app.italicFormat, lineHeight, app.bodyTypography);

        D2D1_COLOR_F color = app.theme.text;
        color.a = 0.6f;
        D2D1_POINT_2F pos = D2D1::Point2F(indent, y);
        D2D1_RECT_F bounds = D2D1::RectF(indent, y, indent + info.width, y + lineHeight);
        addTextRun(app, std::move(info), pos, bounds, color, docStart, altText.length(), false);
        app.docText += altText;
        y += lineHeight;
        return;
    }

    float scale = app.contentScale * app.zoomFactor;
    float imgW = (float)entry.width;
    float imgH = (float)entry.height;

    // Scale to fit within maxWidth, never upscale
    float displayScale = std::min(1.0f, maxWidth / imgW);
    float displayW = imgW * displayScale;
    float displayH = imgH * displayScale;

    // Cap max height
    float maxH = 600.0f * scale;
    if (displayH > maxH) {
        displayH = maxH;
        displayW = displayH * (imgW / imgH);
    }

    app.layoutBitmaps.push_back({entry.bitmap,
        D2D1::RectF(indent, y, indent + displayW, y + displayH)});

    y += displayH + 12 * scale;
}

static void layoutTable(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    float cellPadding = 8.0f * scale;
    float fontSize = app.textFormat->GetFontSize();
    float lineHeight = fontSize * 1.7f;
    float minColWidth = 40.0f * scale;

    // Collect rows
    std::vector<Element*> rows;
    for (const auto& child : elem->children) {
        if (child->type == ElementType::TableRow) {
            rows.push_back(child.get());
        }
    }
    if (rows.empty()) return;

    // Determine column count
    int colCount = elem->col_count;
    if (colCount <= 0 && !rows.empty()) {
        colCount = (int)rows[0]->children.size();
    }
    if (colCount <= 0) return;

    // Pass 1: Measure natural widths using plain text extraction (cheap, approximate)
    std::vector<float> colWidths(colCount, minColWidth);
    std::vector<float> rowHeights(rows.size(), lineHeight + cellPadding * 2);
    std::vector<std::vector<int>> cellAligns(rows.size(), std::vector<int>(colCount, 0));
    // Per-cell natural width + plain-text flag: a simple cell that fits its
    // final column is exactly one line tall, so the height-measuring trial
    // layout in pass 1b can be skipped for it entirely
    std::vector<std::vector<float>> cellNatural(rows.size(), std::vector<float>(colCount, 0.0f));
    std::vector<std::vector<uint8_t>> cellSimple(rows.size(), std::vector<uint8_t>(colCount, 0));

    for (size_t r = 0; r < rows.size(); r++) {
        const auto& row = rows[r];
        for (size_t c = 0; c < row->children.size() && c < (size_t)colCount; c++) {
            const auto& cell = row->children[c];
            cellAligns[r][c] = cell->align;

            // Extract plain text for width estimation
            std::wstring text;
            std::function<void(const ElementPtr&)> extract = [&](const ElementPtr& e) {
                if (!e) return;
                if (e->type == ElementType::Text) text += toWide(e->text);
                else for (const auto& ch : e->children) extract(ch);
            };
            for (const auto& ch : cell->children) extract(ch);

            // Measure natural width
            bool isHeader = (r == 0);
            IDWriteTextFormat* fmt = isHeader ? app.boldFormat : app.textFormat;
            float textWidth = 0;
            if (!text.empty() && fmt) {
                IDWriteTextLayout* layout = nullptr;
                app.dwriteFactory->CreateTextLayout(text.data(), (UINT32)text.length(),
                    fmt, kHugeWidth, lineHeight, &layout);
                if (layout) {
                    DWRITE_TEXT_METRICS metrics{};
                    layout->GetMetrics(&metrics);
                    textWidth = metrics.widthIncludingTrailingWhitespace;
                    layout->Release();
                }
            }
            float needed = textWidth + cellPadding * 2 + 6.0f * scale;
            if (needed > colWidths[c]) colWidths[c] = needed;
            cellNatural[r][c] = needed;
            cellSimple[r][c] = cell->children.size() == 1 &&
                               cell->children[0]->type == ElementType::Text;
        }
    }

    // Distribute widths like a browser's auto table layout: columns whose
    // natural width is under their fair share keep it untouched; only the
    // wide columns shrink, splitting the remaining space proportionally.
    // Pure proportional scaling starved narrow columns (a 3-character CJK
    // header ended up one character wide) while a single huge cell hogged
    // the row (#24).
    float totalWidth = 0;
    for (int c = 0; c < colCount; c++) totalWidth += colWidths[c];

    if (totalWidth > maxWidth) {
        // Floor: at least ~2.5 CJK glyphs per line so no column degenerates
        // into a vertical strip
        float minCol = std::max(minColWidth, fontSize * 2.5f + cellPadding * 2);
        std::vector<bool> fixed(colCount, false);
        float available = maxWidth;
        int unfixedCount = colCount;
        bool changed = true;
        while (changed && unfixedCount > 0) {
            changed = false;
            float fair = available / unfixedCount;
            for (int c = 0; c < colCount; c++) {
                if (!fixed[c] && colWidths[c] <= fair) {
                    fixed[c] = true;
                    available -= colWidths[c];
                    unfixedCount--;
                    changed = true;
                }
            }
        }
        if (unfixedCount > 0) {
            float naturalSum = 0;
            for (int c = 0; c < colCount; c++) {
                if (!fixed[c]) naturalSum += colWidths[c];
            }
            for (int c = 0; c < colCount; c++) {
                if (!fixed[c]) {
                    float w = (naturalSum > 0.0f)
                        ? available * (colWidths[c] / naturalSum)
                        : available / unfixedCount;
                    colWidths[c] = std::max(minCol, w);
                }
            }
        }
        totalWidth = 0;
        for (int c = 0; c < colCount; c++) totalWidth += colWidths[c];
        // Minimum widths can push past maxWidth; the table then joins
        // horizontal scrolling instead of squeezing columns unreadably
        app.contentWidth = std::max(app.contentWidth, indent + totalWidth);
    }

    // Pass 1b: Measure row heights via layoutInlineContent with snapshot-rollback
    for (size_t r = 0; r < rows.size(); r++) {
        float maxRowH = lineHeight + cellPadding * 2;
        bool isHeader = (r == 0);
        IDWriteTextFormat* fmt = isHeader ? app.boldFormat : app.textFormat;
        D2D1_COLOR_F textColor = isHeader ? app.theme.heading : app.theme.text;
        const auto& row = rows[r];

        for (size_t c = 0; c < row->children.size() && c < (size_t)colCount; c++) {
            const auto& cell = row->children[c];
            if (cell->children.empty()) continue;

            // A plain-text cell whose natural width fits the column is one
            // line tall — no trial layout needed (the common case)
            if (cellSimple[r][c] && cellNatural[r][c] <= colWidths[c]) continue;

            float cellW = colWidths[c] - cellPadding * 2;
            LayoutSnapshot snap = takeSnapshot(app);
            float cellY = 0.0f;
            layoutInlineContent(app, cell->children, 0.0f, cellY, cellW,
                                fmt, textColor, {}, lineHeight);
            rollbackTo(app, snap);

            float h = cellY + cellPadding * 2;
            if (h > maxRowH) maxRowH = h;
        }
        rowHeights[r] = maxRowH;
    }

    // Pass 2: Render cells via layoutInlineContent (produces real text runs, links, etc.)
    float tableStartY = y;
    D2D1_COLOR_F borderColor = app.theme.blockquoteBorder;
    float borderStroke = 1.0f * scale;

    for (size_t r = 0; r < rows.size(); r++) {
        float cellX = indent;
        bool isHeader = (r == 0);
        const auto& row = rows[r];

        // Header row background
        if (isHeader) {
            D2D1_COLOR_F headerBg = app.theme.codeBackground;
            headerBg.a = 0.5f;
            app.layoutRects.push_back({D2D1::RectF(indent, y, indent + totalWidth, y + rowHeights[r]), headerBg});
        } else if (r % 2 == 0) {
            // Subtle alternating row background
            D2D1_COLOR_F altBg = app.theme.codeBackground;
            altBg.a = 0.15f;
            app.layoutRects.push_back({D2D1::RectF(indent, y, indent + totalWidth, y + rowHeights[r]), altBg});
        }

        for (size_t c = 0; c < row->children.size() && c < (size_t)colCount; c++) {
            const auto& cell = row->children[c];
            IDWriteTextFormat* fmt = isHeader ? app.boldFormat : app.textFormat;
            D2D1_COLOR_F textColor = isHeader ? app.theme.heading : app.theme.text;

            if (!cell->children.empty()) {
                float cellW = colWidths[c] - cellPadding * 2;
                float textX = cellX + cellPadding;
                float textY = y + cellPadding;

                int align = cellAligns[r][c];
                LayoutSnapshot cellSnap = takeSnapshot(app);

                layoutInlineContent(app, cell->children, textX, textY, cellW,
                                    fmt, textColor, {}, lineHeight);

                // Apply center/right alignment by shifting all new items
                if (align == 2 || align == 3) {
                    float maxRight = 0.0f;
                    for (size_t i = cellSnap.textRuns; i < app.layoutTextRuns.size(); i++) {
                        maxRight = std::max(maxRight, app.layoutTextRuns[i].bounds.right);
                    }
                    float contentW = maxRight - textX;
                    float dx = 0.0f;
                    if (align == 2) { // center
                        dx = (cellW - contentW) / 2.0f;
                    } else { // right
                        dx = cellW - contentW;
                    }
                    if (dx > 0.0f) {
                        shiftLayoutItems(app, cellSnap, dx);
                    }
                }
            }

            cellX += colWidths[c];
        }
        app.docText += L"\n";
        y += rowHeights[r];
    }

    // Grid lines: horizontal
    for (size_t r = 0; r <= rows.size(); r++) {
        float lineY = tableStartY;
        for (size_t i = 0; i < r; i++) lineY += rowHeights[i];
        float stroke = (r == 1) ? borderStroke * 2 : borderStroke; // thicker after header
        app.layoutLines.push_back({D2D1::Point2F(indent, lineY),
                                   D2D1::Point2F(indent + totalWidth, lineY),
                                   borderColor, stroke});
    }

    // Grid lines: vertical
    {
        float tableEndY = tableStartY;
        for (size_t r = 0; r < rows.size(); r++) tableEndY += rowHeights[r];
        float vx = indent;
        for (int c = 0; c <= colCount; c++) {
            app.layoutLines.push_back({D2D1::Point2F(vx, tableStartY),
                                       D2D1::Point2F(vx, tableEndY),
                                       borderColor, borderStroke});
            if (c < colCount) vx += colWidths[c];
        }
    }

    app.docText += L"\n";
    y += 14 * scale;
}

static void layoutHorizontalRule(App& app, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    y += 16 * scale;
    app.layoutLines.push_back({D2D1::Point2F(indent, y),
                               D2D1::Point2F(indent + maxWidth, y),
                               app.theme.blockquoteBorder, scale});
    y += 16 * scale;
}

// Frontmatter properties strip: the title as muted text and the tags as
// small #chips, one row above the document (wrapping when needed)
static void layoutProperties(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    IDWriteTextFormat* chipFormat = app.supSubFormat ? app.supSubFormat : app.textFormat;
    float chipH = 22.0f * scale;
    float padX = 8.0f * scale;
    float gap = 6.0f * scale;
    float x = indent;

    auto chip = [&](const std::wstring& text, D2D1_COLOR_F textColor, bool pill) {
        LayoutInfo info = createLayout(app, text, chipFormat, chipH, app.bodyTypography);
        float w = info.width;
        if (x + w + padX * 2 > indent + maxWidth && x > indent) {
            x = indent;
            y += chipH + gap;
        }
        if (pill) {
            D2D1_COLOR_F bgc = app.theme.codeBackground;
            app.layoutRects.push_back({D2D1::RectF(x, y, x + w + padX * 2, y + chipH), bgc});
        }
        float textY = y + (chipH - info.height) / 2;
        D2D1_POINT_2F pos = D2D1::Point2F(x + (pill ? padX : 0), textY);
        D2D1_RECT_F bounds = D2D1::RectF(pos.x, y, pos.x + w, y + chipH);
        addTextRun(app, std::move(info), pos, bounds, textColor, 0, 0, false);
        x += w + (pill ? padX * 2 : 0) + gap;
    };

    if (!elem->text.empty()) {
        D2D1_COLOR_F titleColor = app.theme.text;
        titleColor.a = 0.65f;
        chip(toWide(elem->text), titleColor, false);
    }
    for (const auto& tag : elem->children) {
        if (tag->type != ElementType::Text || tag->text.empty()) continue;
        chip(L"#" + toWide(tag->text), app.theme.accent, true);
    }
    y += chipH + 14.0f * scale;
}

static void layoutElement(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    if (!elem) return;

    switch (elem->type) {
        case ElementType::Properties:
            layoutProperties(app, elem, y, indent, maxWidth);
            break;
        case ElementType::Paragraph:
            layoutParagraph(app, elem, y, indent, maxWidth);
            break;
        case ElementType::Heading:
            layoutHeading(app, elem, y, indent, maxWidth);
            break;
        case ElementType::CodeBlock:
            layoutCodeBlock(app, elem, y, indent, maxWidth);
            break;
        case ElementType::MermaidDiagram:
            if (!layoutMermaidDiagram(
                    app, elem->text, elem->sourceOffset, y, indent, maxWidth)) {
                app.focusMermaidOnNextLayout = false;
                auto fallback = std::make_shared<Element>(ElementType::CodeBlock);
                auto text = std::make_shared<Element>(ElementType::Text);
                text->text = elem->text;
                text->parent = fallback.get();
                fallback->children.push_back(std::move(text));
                fallback->sourceOffset = elem->sourceOffset;
                layoutCodeBlock(app, fallback, y, indent, maxWidth);
            }
            break;
        case ElementType::BlockQuote:
            layoutBlockquote(app, elem, y, indent, maxWidth);
            break;
        case ElementType::List:
            layoutList(app, elem, y, indent, maxWidth);
            break;
        case ElementType::HorizontalRule:
            layoutHorizontalRule(app, y, indent, maxWidth);
            break;
        case ElementType::Table:
            layoutTable(app, elem, y, indent, maxWidth);
            break;
        case ElementType::HtmlBlock: {
            // HtmlBlock can contain both block elements (Paragraph, List, etc.)
            // and inline elements (Text, Ruby, Link, etc.). Collect consecutive
            // inline children and render them through layoutInlineContent.
            std::vector<ElementPtr> inlineBuffer;
            auto flushInline = [&]() {
                if (!inlineBuffer.empty()) {
                    layoutInlineContent(app, inlineBuffer, indent, y, maxWidth,
                                        app.textFormat, app.theme.text);
                    app.docText += L"\n\n";
                    float s = app.contentScale * app.zoomFactor;
                    y += 14 * s;
                    inlineBuffer.clear();
                }
            };
            for (const auto& child : elem->children) {
                bool isBlock = (child->type == ElementType::Paragraph ||
                                child->type == ElementType::Heading ||
                                child->type == ElementType::CodeBlock ||
                                child->type == ElementType::BlockQuote ||
                                child->type == ElementType::List ||
                                child->type == ElementType::HorizontalRule ||
                                child->type == ElementType::HtmlBlock ||
                                child->type == ElementType::Table);
                if (isBlock) {
                    flushInline();
                    layoutElement(app, child, y, indent, maxWidth);
                } else {
                    inlineBuffer.push_back(child);
                }
            }
            flushInline();
            break;
        }
        default:
            for (const auto& child : elem->children) {
                layoutElement(app, child, y, indent, maxWidth);
            }
            break;
    }
}

// Find first valid sourceOffset in an element subtree
static size_t findFirstSourceOffset(const ElementPtr& elem) {
    if (!elem) return SIZE_MAX;
    if (elem->sourceOffset != SIZE_MAX) return elem->sourceOffset;
    for (const auto& child : elem->children) {
        size_t off = findFirstSourceOffset(child);
        if (off != SIZE_MAX) return off;
    }
    return SIZE_MAX;
}

// Count total elements in AST for vector pre-allocation
static size_t countElements(const ElementPtr& elem) {
    if (!elem) return 0;
    size_t count = 1;
    for (const auto& child : elem->children) {
        count += countElements(child);
    }
    return count;
}

} // namespace

namespace {

// Reset layout state and prepare for laying out blocks. Returns false when
// there is nothing to lay out (no document).
bool layoutBegin(App& app) {
    app.clearLayoutCache();
    app.layoutTimeUs = 0;

    if (!app.root) {
        app.contentHeight = 0;
        app.contentWidth = app.width;
        app.layoutComplete = true;
        return false;
    }

    // Pre-allocate vectors based on estimated element count
    size_t elemCount = countElements(app.root);
    app.layoutTextRuns.reserve(elemCount * 2);
    app.layoutRects.reserve(elemCount);
    app.layoutLines.reserve(elemCount);
    app.layoutShapes.reserve(elemCount);
    app.layoutConnectors.reserve(elemCount);
    app.linkRects.reserve(elemCount / 4);
    app.textRects.reserve(elemCount * 2);
    app.lineBuckets.reserve(elemCount);
    app.docText.reserve(elemCount * 20);  // ~20 chars per element average

    float scale = app.contentScale * app.zoomFactor;

    float layoutWidth = documentViewportWidth(app);

    app.layoutIndent = 40.0f * scale;
    app.layoutMaxWidth = layoutWidth - app.layoutIndent * 2;
    // Reading column (#82): a centered percentage of the window, with a
    // separate preference for fullscreen (zen). Edit-mode panes are exempt.
    {
        int pct = app.zenMode ? app.zenWidthPct : app.readingWidthPct;
        if (pct < 100 && !app.editMode) {
            float column = app.layoutMaxWidth * (float)pct / 100.0f;
            app.layoutIndent += (app.layoutMaxWidth - column) / 2.0f;
            app.layoutMaxWidth = column;
        }
    }
    app.layoutCursorY = 20.0f * scale;
    app.layoutNextBlock = 0;
    app.layoutComplete = false;
    app.contentWidth = layoutWidth;
    app.scrollAnchors.clear();
    return true;
}

// Lay out top-level blocks until targetY is passed (targetY < 0: no limit) or
// budgetUs is exhausted (budgetUs <= 0: no limit). Returns true when all
// blocks are done.
bool layoutStep(App& app, float targetY, int64_t budgetUs) {
    auto t0 = Clock::now();
    const auto& children = app.root->children;
    float y = app.layoutCursorY;

    while (app.layoutNextBlock < children.size()) {
        if (targetY >= 0.0f && y > targetY) break;
        if (budgetUs > 0 && usElapsed(t0) > budgetUs) break;

        const auto& child = children[app.layoutNextBlock];
        // Record scroll anchor from source offset
        size_t offset = findFirstSourceOffset(child);
        if (offset != SIZE_MAX) {
            app.scrollAnchors.push_back({offset, y});
        }
        layoutElement(app, child, y, app.layoutIndent, app.layoutMaxWidth);
        app.layoutNextBlock++;
    }

    app.layoutCursorY = y;
    // Partial content height grows as layout fills in (keeps scrollbar sane)
    float scale = app.contentScale * app.zoomFactor;
    app.contentHeight = y + 40.0f * scale;
    return app.layoutNextBlock >= children.size();
}

void layoutFinish(App& app) {
    // Defer toLower to when search actually needs it (lazy rebuild)
    app.docTextLower.clear();
    mapSearchMatchesToLayout(app);
    app.layoutComplete = true;
}

} // namespace

void layoutDocument(App& app) {
    auto t0 = Clock::now();
    if (layoutBegin(app)) {
        layoutStep(app, -1.0f, -1);
        layoutFinish(app);
    }
    app.layoutDirty = false;
    app.layoutTimeUs += (size_t)usElapsed(t0);
}

void layoutDocumentViewportFirst(App& app) {
    auto t0 = Clock::now();
    if (layoutBegin(app)) {
        // Lay out through two viewports past the current scroll so the first
        // frame presents immediately; the rest continues in chunks.
        float targetY = app.scrollY + (float)app.height * 2.0f;
        if (layoutStep(app, targetY, -1)) {
            layoutFinish(app);
        }
    }
    app.layoutDirty = false;
    app.layoutTimeUs += (size_t)usElapsed(t0);
}

bool layoutDocumentContinue(App& app, int64_t budgetUs) {
    if (app.layoutComplete) return true;
    auto t0 = Clock::now();
    bool done = layoutStep(app, -1.0f, budgetUs);
    if (done) layoutFinish(app);
    app.layoutTimeUs += (size_t)usElapsed(t0);
    return done;
}

void completeAsyncImage(App& app, void* asyncResult) {
    auto* result = static_cast<AsyncImageResult*>(asyncResult);
    if (!result) return;

    App::ImageEntry entry;
    entry.failed = true;
    if (result->ok && app.renderTarget) {
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        ID2D1Bitmap* bitmap = nullptr;
        if (SUCCEEDED(app.renderTarget->CreateBitmap(
                D2D1::SizeU(result->width, result->height),
                result->pixels.data(), result->width * 4, props, &bitmap)) && bitmap) {
            entry.bitmap = bitmap;
            entry.width = (int)result->width;
            entry.height = (int)result->height;
            entry.failed = false;
        }
    }

    app.storeImageCacheEntry(result->src, std::move(entry));
    delete result;

    // Reflow with the real image dimensions — but coalesced: several images
    // finishing close together (the common case when a document loads) get
    // one relayout on the timer instead of a full document layout each
    if (app.hwnd) SetTimer(app.hwnd, TIMER_IMAGE_REFLOW, 60, nullptr);
}

void ensureLayoutComplete(App& app) {
    if (app.layoutDirty) {
        layoutDocument(app);
        return;
    }
    if (!app.layoutComplete) {
        auto t0 = Clock::now();
        layoutStep(app, -1.0f, -1);
        layoutFinish(app);
        app.layoutTimeUs += (size_t)usElapsed(t0);
    }
}
