#include "render.h"
#include "utils.h"
#include "syntax.h"
#include "search.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace {
constexpr float kHugeWidth = 100000.0f;
constexpr float kLineBucketTolerance = 5.0f;

struct LayoutInfo {
    IDWriteTextLayout* layout = nullptr;
    float width = 0.0f;
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

static float getSpaceWidth(App& app, IDWriteTextFormat* format) {
    if (format == app.textFormat) return app.spaceWidthText;
    if (format == app.boldFormat) return app.spaceWidthBold;
    if (format == app.italicFormat) return app.spaceWidthItalic;
    if (format == app.codeFormat) return app.spaceWidthCode;
    return measureText(app, L" ", format);
}

static void layoutElement(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth);

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

        size_t pos = 0;
        while (pos < text.length()) {
            size_t spacePos = text.find(L' ', pos);
            if (spacePos == std::wstring::npos) spacePos = text.length();

            size_t wordStart = pos;
            std::wstring word = text.substr(wordStart, spacePos - wordStart);
            if (word.empty()) {
                if (spacePos < text.length()) {
                    x += spaceWidth;
                    pos = spacePos + 1;
                } else {
                    pos = spacePos;
                }
                continue;
            }

            size_t wordDocStart = textDocStart + wordStart;
            LayoutInfo info = createLayout(app, word, format, lineHeight, app.bodyTypography);
            float wordWidth = info.width;

            if (x + wordWidth > maxX && x > startX) {
                if (isLink && x > linkLineStartX) {
                    addLinkSegment(linkLineStartX, x, linkLineY, linkUrl, color);
                }
                x = startX;
                y += lineHeight;
                linkLineStartX = x;
                linkLineY = y;
            }

            D2D1_POINT_2F posPoint = D2D1::Point2F(x, y);
            D2D1_RECT_F bounds = D2D1::RectF(x, y, x + wordWidth, y + lineHeight);
            addTextRun(app, std::move(info), posPoint, bounds, color,
                       wordDocStart, word.length(), true);

            x += wordWidth;

            if (spacePos < text.length()) {
                x += spaceWidth;
                pos = spacePos + 1;
            } else {
                pos = spacePos;
            }
        }

        app.docText += text;

        if (isLink && x > linkLineStartX) {
            float underlineEndX = x;
            if (!text.empty() && text.back() == L' ') {
                underlineEndX = x - spaceWidth;
            }
            addLinkSegment(linkLineStartX, underlineEndX, linkLineY, linkUrl, color);
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

static void layoutCodeBlock(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    std::string code;
    for (const auto& child : elem->children) {
        if (child->type == ElementType::Text) {
            code += child->text;
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
    app.layoutRects.push_back({D2D1::RectF(indent, y, indent + maxWidth, y + blockHeight),
                               app.theme.codeBackground});

    std::wstring wcode = toWide(code);
    float textY = y + padding;
    bool inBlockComment = false;
    size_t codeDocStart = app.docText.size();
    size_t lineStart = 0;

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

            for (const auto& token : tokens) {
                if (token.text.empty()) continue;

                D2D1_COLOR_F tokenColor = getTokenColor(app.theme, token.tokenType);
                LayoutInfo info = createLayout(app, token.text, app.codeFormat, lineHeight, app.codeTypography);
                float tokenWidth = info.width;

                D2D1_POINT_2F pos = D2D1::Point2F(tokenX, textY);
                D2D1_RECT_F bounds = D2D1::RectF(tokenX, textY, tokenX + tokenWidth, textY + lineHeight);
                addTextRun(app, std::move(info), pos, bounds, tokenColor,
                           lineDocStart, 0, false);

                tokenX += tokenWidth;
                lineWidth += tokenWidth;
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

        textY += lineHeight;
        if (lineEnd == wcode.length()) break;
        lineStart = lineEnd + 1;
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
        case ElementType::BlockQuote:
            layoutBlockquote(app, elem, y, indent, maxWidth);
            break;
        case ElementType::List:
            layoutList(app, elem, y, indent, maxWidth);
            break;
        case ElementType::HorizontalRule:
            layoutHorizontalRule(app, y, indent, maxWidth);
            break;
        case ElementType::HtmlBlock:
            for (const auto& child : elem->children) {
                layoutElement(app, child, y, indent, maxWidth);
            }
            break;
        default:
            for (const auto& child : elem->children) {
                layoutElement(app, child, y, indent, maxWidth);
            }
            break;
    }
}

} // namespace

void layoutDocument(App& app) {
    app.clearLayoutCache();

    if (!app.root) {
        app.contentHeight = 0;
        app.contentWidth = app.width;
        app.layoutDirty = false;
        return;
    }

    float scale = app.contentScale * app.zoomFactor;
    float y = 20.0f * scale;
    float indent = 40.0f * scale;
    float maxWidth = app.width - indent * 2;

    app.contentWidth = app.width;

    for (const auto& child : app.root->children) {
        layoutElement(app, child, y, indent, maxWidth);
    }

    app.contentHeight = y + 40.0f * scale;
    app.docTextLower = toLower(app.docText);
    mapSearchMatchesToLayout(app);
    app.layoutDirty = false;
}
