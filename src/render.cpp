#include "render.h"
#include "utils.h"
#include "syntax.h"
#include "search.h"

#include <algorithm>

void renderInlineContent(App& app, const std::vector<ElementPtr>& elements,
                         float startX, float& y, float maxWidth,
                         IDWriteTextFormat* baseFormat, D2D1_COLOR_F baseColor,
                         const std::string& baseLinkUrl, float customLineHeight) {
    float x = startX;
    // Use custom line height if provided, otherwise calculate from font size
    // Use 1.7x multiplier to accommodate scripts with stacking diacritics (Vietnamese, etc.)
    float lineHeight = customLineHeight > 0 ? customLineHeight : baseFormat->GetFontSize() * 1.7f;
    float maxX = startX + maxWidth;
    float spaceWidth = measureText(app, L" ", baseFormat);

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

                // Draw background
                float textWidth = measureText(app, text, format);
                if (x + textWidth > maxX && x > startX) {
                    x = startX;
                    y += lineHeight;
                }

                float renderY = y - app.scrollY;
                app.brush->SetColor(app.theme.codeBackground);
                app.renderTarget->FillRectangle(
                    D2D1::RectF(x - 2, renderY, x + textWidth + 4, renderY + lineHeight),
                    app.brush);
                app.drawCalls++;

                // Draw text vertically centered in the box
                if (renderY > -lineHeight && renderY < app.height + lineHeight) {
                    float codeFontHeight = format->GetFontSize() * 1.2f;  // Approximate line height for code font
                    float verticalOffset = (lineHeight - codeFontHeight) / 2.0f;

                    app.brush->SetColor(color);
                    IDWriteTextLayout* layout = nullptr;
                    app.dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.length(),
                        format, textWidth + 50, lineHeight, &layout);
                    if (layout) {
                        if (app.codeTypography) {
                            layout->SetTypography(app.codeTypography, {0, (UINT32)text.length()});
                        }
                        app.renderTarget->DrawTextLayout(D2D1::Point2F(x, renderY + verticalOffset), layout, app.brush);
                        layout->Release();
                    } else {
                        app.renderTarget->DrawText(text.c_str(), (UINT32)text.length(), format,
                            D2D1::RectF(x, renderY + verticalOffset, x + textWidth + 50, renderY + lineHeight),
                            app.brush);
                    }
                    app.drawCalls++;

                    // Track text bounds
                    app.textRects.push_back({D2D1::RectF(x, renderY, x + textWidth, renderY + lineHeight), text, codeDocStart});
                }

                recordSearchMatchPositions(app, codeDocStart, codeDocStart + text.length(), y);
                app.docText += text;
                x += textWidth + spaceWidth;
                continue;  // Skip common text drawing code
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

            default:
                renderInlineContent(app, elem->children, x, y, maxWidth - (x - startX), format, color, linkUrl);
                continue;
        }

        if (text.empty()) continue;

        size_t textDocStart = app.docText.size();

        // For links, track start position for continuous underline
        float linkLineStartX = x;
        float linkLineY = y;

        // Word wrap
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
            float wordWidth = measureText(app, word, format);

            // Check if need to wrap
            if (x + wordWidth > maxX && x > startX) {
                // Draw underline for link segment before wrapping
                if (isLink && x > linkLineStartX) {
                    float renderY = linkLineY - app.scrollY;
                    if (renderY > -lineHeight && renderY < app.height + lineHeight) {
                        float underlineY = renderY + lineHeight - 2;
                        app.brush->SetColor(color);
                        app.renderTarget->DrawLine(
                            D2D1::Point2F(linkLineStartX, underlineY),
                            D2D1::Point2F(x, underlineY),
                            app.brush, 1.0f);
                        app.drawCalls++;

                        // Track link bounds for this line segment
                        App::LinkRect lr;
                        lr.bounds = D2D1::RectF(linkLineStartX, renderY, x, renderY + lineHeight);
                        lr.url = linkUrl;
                        app.linkRects.push_back(lr);
                    }
                }

                x = startX;
                y += lineHeight;
                linkLineStartX = x;
                linkLineY = y;
            }

            recordSearchMatchPositions(app, wordDocStart, wordDocStart + word.length(), y);

            // Draw word with typography
            float renderY = y - app.scrollY;
            if (renderY > -lineHeight && renderY < app.height + lineHeight) {
                app.brush->SetColor(color);

                // Use TextLayout for typography support
                IDWriteTextLayout* layout = nullptr;
                app.dwriteFactory->CreateTextLayout(word.c_str(), (UINT32)word.length(),
                    format, wordWidth + 100, lineHeight, &layout);
                if (layout) {
                    // Apply typography (body typography for regular text)
                    if (app.bodyTypography) {
                        layout->SetTypography(app.bodyTypography, {0, (UINT32)word.length()});
                    }
                    app.renderTarget->DrawTextLayout(D2D1::Point2F(x, renderY), layout, app.brush);
                    layout->Release();
                } else {
                    // Fallback to DrawText if layout creation fails
                    app.renderTarget->DrawText(word.c_str(), (UINT32)word.length(), format,
                        D2D1::RectF(x, renderY, x + wordWidth + 100, renderY + lineHeight), app.brush);
                }
                app.drawCalls++;

                // Track text bounds for cursor and selection (add small buffer for selection highlight)
                app.textRects.push_back({D2D1::RectF(x, renderY, x + wordWidth + 2, renderY + lineHeight), word, wordDocStart});
            }

            x += wordWidth;

            // Add space (include in link extent)
            if (spacePos < text.length()) {
                x += spaceWidth;
                pos = spacePos + 1;
            } else {
                pos = spacePos;
            }
        }

        app.docText += text;

        // Draw final underline segment for link
        if (isLink && x > linkLineStartX) {
            float renderY = linkLineY - app.scrollY;
            if (renderY > -lineHeight && renderY < app.height + lineHeight) {
                float underlineY = renderY + lineHeight - 2;
                // Trim trailing space from underline
                float underlineEndX = x;
                if (text.length() > 0 && text.back() != L' ') {
                    underlineEndX = x;
                } else {
                    underlineEndX = x - spaceWidth;
                }
                app.brush->SetColor(color);
                app.renderTarget->DrawLine(
                    D2D1::Point2F(linkLineStartX, underlineY),
                    D2D1::Point2F(underlineEndX, underlineY),
                    app.brush, 1.0f);
                app.drawCalls++;

                // Track link bounds
                App::LinkRect lr;
                lr.bounds = D2D1::RectF(linkLineStartX, renderY, underlineEndX, renderY + lineHeight);
                lr.url = linkUrl;
                app.linkRects.push_back(lr);
            }
        }
    }

    y += lineHeight;
}

void renderParagraph(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    renderInlineContent(app, elem->children, indent, y, maxWidth, app.textFormat, app.theme.text);
    app.docText += L"\n\n";
    float scale = app.contentScale * app.zoomFactor;
    y += 14 * scale;  // Increased paragraph spacing
}

void renderHeading(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    float sizes[] = {32, 26, 22, 18, 16, 14};
    float size = sizes[std::min(elem->level - 1, 5)] * scale;

    IDWriteTextFormat* format = nullptr;
    app.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        size, L"en-us", &format);

    // Add top margin for headings (more for H1)
    if (elem->level == 1) {
        y += 16 * scale;
    } else {
        y += 20 * scale;
    }

    renderInlineContent(app, elem->children, indent, y, maxWidth, format, app.theme.heading);

    // Underline for H1/H2 - add gap before the line
    if (elem->level <= 2) {
        y += 6 * scale;  // Gap between text and underline
        float renderY = y - app.scrollY;
        app.brush->SetColor(D2D1::ColorF(app.theme.heading.r, app.theme.heading.g, app.theme.heading.b, 0.3f));
        float lineWidth = (elem->level == 1) ? 2.0f * scale : 1.0f * scale;
        app.renderTarget->DrawLine(
            D2D1::Point2F(indent, renderY),
            D2D1::Point2F(indent + maxWidth, renderY),
            app.brush, lineWidth);
        y += lineWidth;
        app.drawCalls++;
    }

    app.docText += L"\n\n";
    format->Release();
    y += 12 * scale;  // Bottom margin after heading
}

void renderCodeBlock(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    std::string code;
    for (const auto& child : elem->children) {
        if (child->type == ElementType::Text) {
            code += child->text;
        }
    }

    // Get language from element
    std::wstring langHint = toWide(elem->language);
    int language = detectLanguage(langHint);

    float scale = app.contentScale * app.zoomFactor;
    float lineHeight = 20.0f * scale;
    float padding = 12.0f * scale;

    // Count lines
    int lineCount = 1;
    for (char c : code) if (c == '\n') lineCount++;

    app.docText += L"\n";

    float blockHeight = lineCount * lineHeight + padding * 2;

    // Background
    float renderY = y - app.scrollY;
    app.brush->SetColor(app.theme.codeBackground);
    app.renderTarget->FillRectangle(
        D2D1::RectF(indent, renderY, indent + maxWidth, renderY + blockHeight),
        app.brush);
    app.drawCalls++;

    // Render lines with syntax highlighting
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
        recordSearchMatchPositions(app, lineDocStart, lineDocStart + wline.length(), textY);

        float lineRenderY = textY - app.scrollY;
        if (lineRenderY > -lineHeight && lineRenderY < app.height + lineHeight) {
            float textWidth = measureText(app, wline, app.codeFormat);

            // Apply syntax highlighting if language is known
            if (language > 0) {
                std::vector<SyntaxToken> tokens = tokenizeLine(wline, language, inBlockComment);
                float tokenX = indent + padding;

                for (const auto& token : tokens) {
                    if (token.text.empty()) continue;

                    D2D1_COLOR_F tokenColor = getTokenColor(app.theme, token.tokenType);
                    app.brush->SetColor(tokenColor);

                    float tokenWidth = measureText(app, token.text, app.codeFormat);

                    // Use TextLayout for code typography
                    IDWriteTextLayout* layout = nullptr;
                    app.dwriteFactory->CreateTextLayout(token.text.c_str(), (UINT32)token.text.length(),
                        app.codeFormat, tokenWidth + 50, lineHeight, &layout);
                    if (layout) {
                        if (app.codeTypography) {
                            layout->SetTypography(app.codeTypography, {0, (UINT32)token.text.length()});
                        }
                        app.renderTarget->DrawTextLayout(D2D1::Point2F(tokenX, lineRenderY), layout, app.brush);
                        layout->Release();
                    } else {
                        app.renderTarget->DrawText(token.text.c_str(), (UINT32)token.text.length(), app.codeFormat,
                            D2D1::RectF(tokenX, lineRenderY, tokenX + tokenWidth + 50, lineRenderY + lineHeight),
                            app.brush);
                    }
                    app.drawCalls++;
                    tokenX += tokenWidth;
                }
            } else {
                // No syntax highlighting - just render plain text
                app.brush->SetColor(app.theme.code);
                app.renderTarget->DrawText(wline.c_str(), (UINT32)wline.length(), app.codeFormat,
                    D2D1::RectF(indent + padding, lineRenderY, indent + maxWidth - padding, lineRenderY + lineHeight),
                    app.brush);
                app.drawCalls++;
            }

            // Track text bounds for selection
            if (!wline.empty()) {
                app.textRects.push_back({D2D1::RectF(indent + padding, lineRenderY,
                    indent + padding + textWidth, lineRenderY + lineHeight), wline, lineDocStart});
            }
        }

        textY += lineHeight;

        if (lineEnd == wcode.length()) break;
        lineStart = lineEnd + 1;
    }

    app.docText += wcode;
    app.docText += L"\n\n";

    y += blockHeight + 14 * scale;  // Increased spacing after code blocks
}

void renderBlockquote(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    float quoteIndent = 20.0f * scale;
    float startY = y;

    for (const auto& child : elem->children) {
        renderElement(app, child, y, indent + quoteIndent, maxWidth - quoteIndent);
    }

    // Border
    app.brush->SetColor(app.theme.blockquoteBorder);
    app.renderTarget->FillRectangle(
        D2D1::RectF(indent, startY - app.scrollY, indent + 4, y - app.scrollY),
        app.brush);
    app.drawCalls++;
}

void renderList(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    float listIndent = 24.0f * scale;
    int itemNum = elem->start;

    for (const auto& child : elem->children) {
        if (child->type != ElementType::ListItem) continue;

        // Draw bullet/number
        std::wstring marker = elem->ordered ?
            std::to_wstring(itemNum++) + L"." : L"\x2022";

        float renderY = y - app.scrollY;
        if (renderY > -30 && renderY < app.height + 30) {
            app.brush->SetColor(app.theme.text);
            app.renderTarget->DrawText(marker.c_str(), (UINT32)marker.length(), app.textFormat,
                D2D1::RectF(indent, renderY, indent + listIndent, renderY + 24),
                app.brush);
            app.drawCalls++;
        }

        // Check for block vs inline content
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
                renderInlineContent(app, inlineElements, indent + listIndent, y,
                    maxWidth - listIndent, app.textFormat, app.theme.text);
            }

            for (const auto& blockChild : blockElements) {
                renderElement(app, blockChild, y, indent + listIndent, maxWidth - listIndent);
            }
        } else {
            renderInlineContent(app, child->children, indent + listIndent, y,
                maxWidth - listIndent, app.textFormat, app.theme.text);
        }

        app.docText += L"\n\n";

        if (y < itemStartY + 28 * scale) {
            y = itemStartY + 28 * scale;  // Slightly more space per list item
        }
    }
    y += 8 * scale;  // Extra spacing after list
}

void renderHorizontalRule(App& app, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    y += 16 * scale;  // Increased spacing before rule
    float renderY = y - app.scrollY;

    app.brush->SetColor(app.theme.blockquoteBorder);
    app.renderTarget->DrawLine(
        D2D1::Point2F(indent, renderY),
        D2D1::Point2F(indent + maxWidth, renderY),
        app.brush, scale);
    app.drawCalls++;

    y += 16 * scale;  // Increased spacing after rule
}

void renderElement(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    if (!elem) return;

    switch (elem->type) {
        case ElementType::Paragraph:
            renderParagraph(app, elem, y, indent, maxWidth);
            break;
        case ElementType::Heading:
            renderHeading(app, elem, y, indent, maxWidth);
            break;
        case ElementType::CodeBlock:
            renderCodeBlock(app, elem, y, indent, maxWidth);
            break;
        case ElementType::BlockQuote:
            renderBlockquote(app, elem, y, indent, maxWidth);
            break;
        case ElementType::List:
            renderList(app, elem, y, indent, maxWidth);
            break;
        case ElementType::HorizontalRule:
            renderHorizontalRule(app, y, indent, maxWidth);
            break;
        case ElementType::HtmlBlock:
            // HTML blocks are parsed into child elements, render them
            for (const auto& child : elem->children) {
                renderElement(app, child, y, indent, maxWidth);
            }
            break;
        default:
            for (const auto& child : elem->children) {
                renderElement(app, child, y, indent, maxWidth);
            }
            break;
    }
}
