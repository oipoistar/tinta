#include "render.h"
#include "utils.h"
#include "syntax.h"
#include "search.h"

#include <algorithm>
#include <cmath>
#include <functional>
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

struct LayoutSnapshot {
    size_t textRuns, rects, lines, links, textRects, lineBuckets, docTextLen;
};

static LayoutSnapshot takeSnapshot(App& app) {
    return {
        app.layoutTextRuns.size(),
        app.layoutRects.size(),
        app.layoutLines.size(),
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

        size_t pos = 0;
        while (pos < text.length()) {
            size_t spacePos = text.find(L' ', pos);
            if (spacePos == std::wstring::npos) spacePos = text.length();

            size_t wordStart = pos;
            std::wstring_view word(text.data() + wordStart, spacePos - wordStart);
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

void layoutDocument(App& app) {
    app.clearLayoutCache();

    if (!app.root) {
        app.contentHeight = 0;
        app.contentWidth = app.width;
        app.layoutDirty = false;
        return;
    }

    // Pre-allocate vectors based on estimated element count
    size_t elemCount = countElements(app.root);
    app.layoutTextRuns.reserve(elemCount * 2);
    app.layoutRects.reserve(elemCount);
    app.layoutLines.reserve(elemCount);
    app.linkRects.reserve(elemCount / 4);
    app.textRects.reserve(elemCount * 2);
    app.lineBuckets.reserve(elemCount);
    app.docText.reserve(elemCount * 20);  // ~20 chars per element average

    float scale = app.contentScale * app.zoomFactor;
    float y = 20.0f * scale;
    float indent = 40.0f * scale;

    // In edit mode, use preview pane width instead of full window
    float layoutWidth = app.width;
    if (app.editMode) {
        layoutWidth = app.width * (1.0f - app.editorSplitRatio) - 6;
    }
    float maxWidth = layoutWidth - indent * 2;

    app.contentWidth = layoutWidth;

    app.scrollAnchors.clear();
    for (const auto& child : app.root->children) {
        // Record scroll anchor from source offset
        size_t offset = findFirstSourceOffset(child);
        if (offset != SIZE_MAX) {
            app.scrollAnchors.push_back({offset, y});
        }
        layoutElement(app, child, y, indent, maxWidth);
    }

    app.contentHeight = y + 40.0f * scale;
    // Defer toLower to when search actually needs it (lazy rebuild)
    app.docTextLower.clear();
    mapSearchMatchesToLayout(app);
    app.layoutDirty = false;
}
