#include "inline_style.h"

IDWriteTextFormat* emphasisFormat(App& app, IDWriteTextFormat* base, bool bold, bool italic) {
    IDWriteTextFormat* fmt = nullptr;
    if (bold && italic) fmt = app.boldItalicFormat ? app.boldItalicFormat : app.boldFormat;
    else if (bold) fmt = app.boldFormat;
    else if (italic) fmt = app.italicFormat;
    return fmt ? fmt : base;
}

IDWriteTextFormat* inlineCodeFormat(App& app, bool bold, bool italic) {
    IDWriteTextFormat* fmt = nullptr;
    if (bold && italic) fmt = app.codeBoldItalicFormat;
    else if (bold) fmt = app.codeBoldFormat;
    else if (italic) fmt = app.codeItalicFormat;
    return fmt ? fmt : app.codeFormat;
}

void flattenInline(App& app, const std::vector<ElementPtr>& elements,
                   const InlineStyle& inherited, float lineHeight,
                   std::vector<StyledRun>& out) {
    for (const auto& elem : elements) {
        InlineStyle st = inherited;
        switch (elem->type) {
            case ElementType::Text:
            case ElementType::Code:
            case ElementType::Image:
            case ElementType::Ruby:
            case ElementType::SoftBreak:
            case ElementType::HardBreak:
            case ElementType::MathInline:
            case ElementType::MathDisplay:
                out.push_back({elem, st});
                continue;

            case ElementType::Strong:
            case ElementType::Emphasis:
                st.bold = st.bold || elem->type == ElementType::Strong;
                st.italic = st.italic || elem->type == ElementType::Emphasis;
                if (!st.fixedSize) st.format = emphasisFormat(app, st.format, st.bold, st.italic);
                break;

            case ElementType::Strikethrough:
                st.hasStrike = true;
                break;

            case ElementType::Highlight:
                // ==text== renders on a marker-pen background
                st.hasBg = true;
                st.bgColor = app.theme.isDark
                    ? D2D1::ColorF(0.98f, 0.80f, 0.25f, 0.28f)
                    : D2D1::ColorF(1.00f, 0.88f, 0.20f, 0.45f);
                break;

            case ElementType::Superscript:
            case ElementType::Subscript:
                // Small text; NEAR alignment already sits sup at the top of the line
                if (app.supSubFormat) {
                    st.format = app.supSubFormat;
                    st.fixedSize = true;
                }
                if (elem->type == ElementType::Subscript) st.drawYOffset = lineHeight * 0.38f;
                break;

            case ElementType::Link:
                st.color = app.theme.link;
                st.linkUrl = elem->url;
                st.isLink = true;
                break;

            default:
                break;  // unknown wrapper: keep the style and descend
        }
        flattenInline(app, elem->children, st, lineHeight, out);
    }
}
