#include "render.h"
#include "utils.h"
#include "syntax.h"
#include "search.h"
#include "mermaid.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <string_view>
#include <filesystem>
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")

namespace {
constexpr float kHugeWidth = 100000.0f;
constexpr float kLineBucketTolerance = 5.0f;

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

static void addTextRect(App& app, const D2D1_RECT_F& rect, size_t docStart, size_t docLength) {
    size_t idx = app.textRects.size();
    app.textRects.push_back({rect, docStart, docLength});

    if (app.lineBuckets.empty() ||
        std::abs(rect.top - app.lineBuckets.back().top) > kLineBucketTolerance) {
        App::LineBucket bucket;
        bucket.top = rect.top;
        bucket.bottom = rect.bottom;
        bucket.minX = rect.left;
        bucket.maxX = rect.right;
        bucket.textRectIndices.push_back(idx);
        app.lineBuckets.push_back(std::move(bucket));
        return;
    }

    auto& bucket = app.lineBuckets.back();
    bucket.bottom = std::max(bucket.bottom, rect.bottom);
    bucket.minX = std::min(bucket.minX, rect.left);
    bucket.maxX = std::max(bucket.maxX, rect.right);
    bucket.textRectIndices.push_back(idx);
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
        addTextRect(app, bounds, docStart, docLength);
    }
}

struct LayoutSnapshot {
    size_t textRuns, rects, lines, shapes, connectors;
    size_t links, textRects, lineBuckets, docTextLen;
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
        app.docText.size()
    };
}

static void rollbackTo(App& app, const LayoutSnapshot& s) {
    for (size_t i = s.textRuns; i < app.layoutTextRuns.size(); i++) {
        if (app.layoutTextRuns[i].layout) {
            app.layoutTextRuns[i].layout->Release();
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
    for (size_t i = from.lineBuckets; i < app.lineBuckets.size(); i++) {
        auto& b = app.lineBuckets[i];
        b.minX += dx;
        b.maxX += dx;
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

    for (const auto& elem : elements) {
        IDWriteTextFormat* format = baseFormat;
        D2D1_COLOR_F color = baseColor;
        std::string linkUrl = baseLinkUrl;
        bool isLink = !baseLinkUrl.empty();

        std::wstring text;

        switch (elem->type) {
            case ElementType::Text:
                text = toWide(elem->text);
                break;

            case ElementType::Strong:
                format = app.boldFormat;
                for (const auto& child : elem->children) {
                    if (child->type == ElementType::Text) {
                        text += toWide(child->text);
                    }
                }
                break;

            case ElementType::Emphasis:
                format = app.italicFormat;
                for (const auto& child : elem->children) {
                    if (child->type == ElementType::Text) {
                        text += toWide(child->text);
                    }
                }
                break;

            case ElementType::Code: {
                format = app.codeFormat;
                color = app.theme.code;
                for (const auto& child : elem->children) {
                    if (child->type == ElementType::Text) {
                        text = toWide(child->text);
                    }
                }

                size_t codeDocStart = app.docText.size();
                LayoutInfo info = createLayout(app, text, format, lineHeight, app.codeTypography);
                float textWidth = info.width;

                if (x + textWidth > maxX && x > startX) {
                    x = startX;
                    y += lineHeight;
                }

                app.layoutRects.push_back({D2D1::RectF(x - 2, y, x + textWidth + 4, y + lineHeight),
                                           app.theme.codeBackground});

                float codeFontHeight = format->GetFontSize() * 1.2f;
                float verticalOffset = (lineHeight - codeFontHeight) / 2.0f;
                D2D1_POINT_2F pos = D2D1::Point2F(x, y + verticalOffset);
                D2D1_RECT_F bounds = D2D1::RectF(x, y, x + textWidth, y + lineHeight);
                addTextRun(app, std::move(info), pos, bounds, color,
                           codeDocStart, text.length(), true);

                app.docText += text;
                x += textWidth + spaceWidth;
                continue;
            }

            case ElementType::Link:
                color = app.theme.link;
                linkUrl = elem->url;
                isLink = true;
                for (const auto& child : elem->children) {
                    if (child->type == ElementType::Text) {
                        text += toWide(child->text);
                    }
                }
                break;

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
                layoutInlineContent(app, elem->children, x, y,
                                    maxWidth - (x - startX), format, color, linkUrl);
                continue;
        }

        if (text.empty()) continue;

        size_t textDocStart = app.docText.size();
        float linkLineStartX = x;
        float linkLineY = y;

        // Measure the whole element once: cluster metrics give every word
        // width without creating one IDWriteTextLayout per word. Word splits
        // happen at spaces, which are always cluster boundaries.
        std::vector<float> cumW(text.length() + 1, -1.0f);
        cumW[0] = 0.0f;
        {
            LayoutInfo measureInfo = createLayout(app, text, format, lineHeight, app.bodyTypography);
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
                            if (cpos <= text.length()) cumW[cpos] = w;
                        }
                    }
                }
                measureInfo.layout->Release();
            }
            // Fill boundaries that fall inside clusters (safety; word splits
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
            LayoutInfo info = createLayout(app, segText, format, lineHeight, app.bodyTypography);
            float segWidth = widthOf(segStart, segEnd);
            D2D1_POINT_2F segPos = D2D1::Point2F(segX, y);
            D2D1_RECT_F segBounds = D2D1::RectF(segX, y, segX + segWidth, y + lineHeight);
            addTextRun(app, std::move(info), segPos, segBounds, color,
                       textDocStart + segStart, segEnd - segStart, false);
            for (const auto& w : segWords) {
                float wx = segX + widthOf(segStart, w.start);
                D2D1_RECT_F wb = D2D1::RectF(wx, y, wx + widthOf(w.start, w.start + w.len),
                                             y + lineHeight);
                addTextRect(app, wb, textDocStart + w.start, w.len);
            }
            segWords.clear();
        };

        size_t pos = 0;
        size_t lastWordEnd = 0;
        while (pos < text.length()) {
            size_t spacePos = text.find(L' ', pos);
            if (spacePos == std::wstring::npos) spacePos = text.length();

            size_t wordLen = spacePos - pos;
            if (wordLen > 0) {
                float wordWidth = widthOf(pos, spacePos);
                float wordX = segOpen ? segX + widthOf(segStart, pos) : x;

                if (wordX + wordWidth > maxX && wordX > startX) {
                    // Word wraps: flush the current line's segment first
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
                    segStart = pos;
                    segX = wordX;
                }
                segWords.push_back({pos, wordLen});
                lastWordEnd = spacePos;
                lastWordEndX = segX + widthOf(segStart, spacePos);
                x = lastWordEndX;
            }

            if (spacePos < text.length()) {
                // Advance past the space (shaped width inside an open segment)
                x = segOpen ? segX + widthOf(segStart, spacePos + 1) : x + spaceWidth;
                pos = spacePos + 1;
            } else {
                pos = spacePos;
            }
        }
        flushSegment(lastWordEnd);

        app.docText += text;

        if (isLink && lastWordEndX > linkLineStartX) {
            addLinkSegment(linkLineStartX, lastWordEndX, linkLineY, linkUrl, color);
        }
    }

    y += lineHeight;
}

static void layoutParagraph(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
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
        app.headings.push_back({headingText, elem->level, y});
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
        if (layoutMermaidDiagram(
                app, code, elem->sourceOffset, y, indent, maxWidth,
                &renderedBounds)) {
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
            addTextRun(app, std::move(info), pos, bounds, app.theme.code,
                       lineDocStart, wline.length(), false);
        }

        if (!wline.empty()) {
            D2D1_RECT_F lineBounds = D2D1::RectF(indent + padding, textY,
                indent + padding + lineWidth, textY + lineHeight);
            addTextRect(app, lineBounds, lineDocStart, wline.length());
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

    for (const auto& child : elem->children) {
        layoutElement(app, child, y, indent + quoteIndent, maxWidth - quoteIndent);
    }

    app.layoutRects.push_back({D2D1::RectF(indent, startY, indent + 4, y),
                               app.theme.blockquoteBorder});
}

static void layoutList(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    float listIndent = 24.0f * scale;
    int itemNum = elem->start;

    for (const auto& child : elem->children) {
        if (child->type != ElementType::ListItem) continue;

        std::wstring marker = elem->ordered ?
            std::to_wstring(itemNum++) + L"." : L"\x2022";

        LayoutInfo info = createLayout(app, marker, app.textFormat, 24.0f, app.bodyTypography);
        D2D1_POINT_2F pos = D2D1::Point2F(indent, y);
        D2D1_RECT_F bounds = D2D1::RectF(indent, y, indent + listIndent, y + 24);
        addTextRun(app, std::move(info), pos, bounds, app.theme.text, 0, 0, false);

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

static App::ImageEntry& getOrLoadImage(App& app, const std::string& src) {
    auto it = app.imageCache.find(src);
    if (it != app.imageCache.end()) return it->second;

    App::ImageEntry entry;
    entry.failed = true;  // assume failure

    if (!app.wicFactory || !app.renderTarget) {
        app.imageCache[src] = entry;
        return app.imageCache[src];
    }

    std::wstring widePath;

    // Check if URL
    bool isUrl = (src.rfind("http://", 0) == 0 || src.rfind("https://", 0) == 0);
    if (isUrl) {
        // Download to temp file
        wchar_t tempPath[MAX_PATH] = {};
        std::wstring wideSrc = toWide(src);
        HRESULT hr = URLDownloadToCacheFileW(nullptr, wideSrc.c_str(), tempPath, MAX_PATH, 0, nullptr);
        if (FAILED(hr)) {
            app.imageCache[src] = entry;
            return app.imageCache[src];
        }
        widePath = tempPath;
    } else {
        // Resolve relative to current file's directory
        std::wstring wsrc = toWide(src);
        if (!app.currentFile.empty()) {
            std::filesystem::path basePath(app.currentFile);
            std::filesystem::path imgPath = basePath.parent_path() / src;
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
        app.imageCache[src] = entry;
        return app.imageCache[src];
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        decoder->Release();
        app.imageCache[src] = entry;
        return app.imageCache[src];
    }

    IWICFormatConverter* converter = nullptr;
    hr = app.wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        frame->Release();
        decoder->Release();
        app.imageCache[src] = entry;
        return app.imageCache[src];
    }

    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        app.imageCache[src] = entry;
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

    app.imageCache[src] = entry;
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
        }
    }

    // Distribute widths: if total exceeds maxWidth, scale proportionally
    float totalWidth = 0;
    for (int c = 0; c < colCount; c++) totalWidth += colWidths[c];

    if (totalWidth > maxWidth) {
        float ratio = maxWidth / totalWidth;
        for (int c = 0; c < colCount; c++) {
            colWidths[c] = std::max(minColWidth, colWidths[c] * ratio);
        }
        totalWidth = 0;
        for (int c = 0; c < colCount; c++) totalWidth += colWidths[c];
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

static void layoutElement(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    if (!elem) return;

    switch (elem->type) {
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
