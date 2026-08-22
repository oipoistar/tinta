// HTML export (#export_as): walks the parsed element tree and emits one
// standalone .html with the active theme's palette inlined. Mermaid
// diagrams re-run their native parsers and come out as inline SVG; math
// re-uses the TeX layout engine through mathBoxSvg. No third-party code,
// no external assets: local images are embedded as data URIs.

#include "export.h"

#include "editor.h"
#include "i18n.h"
#include "math_render.h"
#include "mermaid_ext.h"
#include "render.h"
#include "utils.h"

#include <commdlg.h>
#include <cmath>
#include <fstream>
#include <sstream>

using namespace qmd;

namespace {

constexpr float kDiagramFontSize = 14.0f;

// --- small emit helpers ---

std::string htmlEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string num(float v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

std::string colorCss(D2D1_COLOR_F c) {
    char buf[48];
    if (c.a >= 0.999f) {
        snprintf(buf, sizeof(buf), "#%02x%02x%02x", (int)(c.r * 255 + 0.5f),
                 (int)(c.g * 255 + 0.5f), (int)(c.b * 255 + 0.5f));
    } else {
        snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%.2f)",
                 (int)(c.r * 255 + 0.5f), (int)(c.g * 255 + 0.5f),
                 (int)(c.b * 255 + 0.5f), c.a);
    }
    return buf;
}

std::string base64(const std::string& data) {
    static const char* alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    while (i + 2 < data.size()) {
        unsigned v = ((unsigned char)data[i] << 16) |
                     ((unsigned char)data[i + 1] << 8) |
                     (unsigned char)data[i + 2];
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += alphabet[(v >> 6) & 63];
        out += alphabet[v & 63];
        i += 3;
    }
    if (i + 1 == data.size()) {
        unsigned v = (unsigned char)data[i] << 16;
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == data.size()) {
        unsigned v = ((unsigned char)data[i] << 16) |
                     ((unsigned char)data[i + 1] << 8);
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += alphabet[(v >> 6) & 63];
        out += "=";
    }
    return out;
}

const char* mimeForPath(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return nullptr;
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) c = (char)tolower((unsigned char)c);
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "webp") return "image/webp";
    if (ext == "bmp") return "image/bmp";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "ico") return "image/x-icon";
    return nullptr;
}

bool readBinary(const std::wstring& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return !out.empty() && out.size() < 20u * 1024 * 1024;
}

// --- diagram SVG ---

struct DiagramCtx {
    App& app;
    std::string bodyFontCss;
    std::string monoFontCss;
};

std::string prpaint(const App& app, const mermaidext::Prim& p) {
    std::string s;
    if (p.fill != mermaidext::Role::None) {
        s += " fill=\"" + colorCss(resolveDiagramRole(app, p, p.fill)) + "\"";
    } else {
        s += " fill=\"none\"";
    }
    if (p.stroke != mermaidext::Role::None) {
        s += " stroke=\"" + colorCss(resolveDiagramRole(app, p, p.stroke)) +
             "\" stroke-width=\"" +
             num(p.strokeWidth > 0 ? p.strokeWidth : 1.2f) + "\"";
        if (p.dashed) s += " stroke-dasharray=\"4 3\"";
    }
    return s;
}

void emitDiagramText(DiagramCtx& ctx, const mermaidext::Prim& p,
                     std::string& s) {
    float fontSize = kDiagramFontSize * (p.style.scale > 0 ? p.style.scale : 1.0f);
    float lineH = fontSize * 1.35f;

    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : p.text) {
            if (c == '\n') {
                lines.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        lines.push_back(cur);
    }
    float blockH = lines.size() * lineH;
    float top;
    if (p.alignV < 0) top = p.y1;
    else if (p.alignV > 0) top = p.y2 - blockH;
    else top = (p.y1 + p.y2) * 0.5f - blockH * 0.5f;

    float x;
    const char* anchor;
    if (p.alignH < 0) { x = p.x1; anchor = "start"; }
    else if (p.alignH > 0) { x = p.x2; anchor = "end"; }
    else { x = (p.x1 + p.x2) * 0.5f; anchor = "middle"; }

    D2D1_COLOR_F color = resolveDiagramRole(
        ctx.app, p,
        p.fill != mermaidext::Role::None ? p.fill : mermaidext::Role::Text);
    const std::string& family =
        p.style.mono ? ctx.monoFontCss : ctx.bodyFontCss;

    for (size_t i = 0; i < lines.size(); i++) {
        if (lines[i].empty()) continue;
        float baseline = top + lineH * i + fontSize * 0.85f;
        s += "<text x=\"" + num(x) + "\" y=\"" + num(baseline) +
             "\" text-anchor=\"" + anchor + "\" font-size=\"" +
             num(fontSize) + "\" font-family=\"" + family + "\" fill=\"" +
             colorCss(color) + "\"";
        if (p.style.bold) s += " font-weight=\"bold\"";
        if (p.style.italic) s += " font-style=\"italic\"";
        s += ">" + htmlEscape(lines[i]) + "</text>";
    }
}

void emitArrowHead(const App& app, const mermaidext::Prim& p, std::string& s) {
    float dx = p.x2 - p.x1, dy = p.y2 - p.y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f) return;
    float ux = dx / len, uy = dy / len;
    float size = 8.0f, half = 3.6f;
    float bx = p.x2 - ux * size, by = p.y2 - uy * size;
    float px = -uy, py = ux;
    D2D1_COLOR_F color = resolveDiagramRole(
        app, p,
        p.stroke != mermaidext::Role::None ? p.stroke
                                           : mermaidext::Role::Text);
    if (p.openArrow) {
        s += "<path d=\"M " + num(bx + px * half) + " " + num(by + py * half) +
             " L " + num(p.x2) + " " + num(p.y2) + " L " +
             num(bx - px * half) + " " + num(by - py * half) +
             "\" fill=\"none\" stroke=\"" + colorCss(color) +
             "\" stroke-width=\"1.2\"/>";
    } else {
        s += "<polygon points=\"" + num(p.x2) + "," + num(p.y2) + " " +
             num(bx + px * half) + "," + num(by + py * half) + " " +
             num(bx - px * half) + "," + num(by - py * half) + "\" fill=\"" +
             colorCss(color) + "\"/>";
    }
}

// Native diagram -> standalone SVG; empty string = caller shows the source
std::string diagramSvg(App& app, const std::string& sourceUtf8,
                       const std::string& bodyFontCss,
                       const std::string& monoFontCss) {
    mermaidext::Kind kind = mermaidext::detectKind(sourceUtf8);
    if (kind == mermaidext::Kind::None || kind == mermaidext::Kind::Flowchart)
        return {};

    auto measure = [&](const std::string& text,
                       const mermaidext::TextStyle& style,
                       float wrapWidth) -> mermaidext::Size {
        if (text.empty() || !app.dwriteFactory) return {};
        std::wstring wide = toWide(text);
        IDWriteTextFormat* format = nullptr;
        const wchar_t* family =
            style.mono ? app.theme.codeFontFamily : app.theme.fontFamily;
        app.dwriteFactory->CreateTextFormat(
            family, nullptr,
            style.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            style.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            kDiagramFontSize * (style.scale > 0 ? style.scale : 1.0f),
            L"en-us", &format);
        if (!format) return {};
        constexpr float kHuge = 100000.0f;
        IDWriteTextLayout* layout = nullptr;
        app.dwriteFactory->CreateTextLayout(
            wide.c_str(), (UINT32)wide.size(), format,
            wrapWidth > 0.0f ? wrapWidth : kHuge, kHuge, &layout);
        format->Release();
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
        mermaidext::build(kind, sourceUtf8, measure, 1.0f);
    if (!built.ok || built.prims.empty()) return {};

    DiagramCtx ctx{app, bodyFontCss, monoFontCss};
    std::string s = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" +
                    num(built.width) + "\" height=\"" + num(built.height) +
                    "\" viewBox=\"0 0 " + num(built.width) + " " +
                    num(built.height) + "\">";
    for (const auto& p : built.prims) {
        switch (p.type) {
            case mermaidext::PrimType::Rect:
                s += "<rect x=\"" + num(p.x1) + "\" y=\"" + num(p.y1) +
                     "\" width=\"" + num(p.x2 - p.x1) + "\" height=\"" +
                     num(p.y2 - p.y1) + "\"" + prpaint(app, p) + "/>";
                break;
            case mermaidext::PrimType::RoundRect:
                s += "<rect x=\"" + num(p.x1) + "\" y=\"" + num(p.y1) +
                     "\" width=\"" + num(p.x2 - p.x1) + "\" height=\"" +
                     num(p.y2 - p.y1) + "\" rx=\"" + num(p.radius) + "\"" +
                     prpaint(app, p) + "/>";
                break;
            case mermaidext::PrimType::Ellipse:
                s += "<ellipse cx=\"" + num((p.x1 + p.x2) * 0.5f) +
                     "\" cy=\"" + num((p.y1 + p.y2) * 0.5f) + "\" rx=\"" +
                     num((p.x2 - p.x1) * 0.5f) + "\" ry=\"" +
                     num((p.y2 - p.y1) * 0.5f) + "\"" + prpaint(app, p) +
                     "/>";
                break;
            case mermaidext::PrimType::Line:
                s += "<line x1=\"" + num(p.x1) + "\" y1=\"" + num(p.y1) +
                     "\" x2=\"" + num(p.x2) + "\" y2=\"" + num(p.y2) + "\"" +
                     prpaint(app, p) + "/>";
                if (p.arrow || p.openArrow) emitArrowHead(app, p, s);
                break;
            case mermaidext::PrimType::Polygon: {
                s += "<polygon points=\"";
                for (size_t i = 0; i < p.pts.size(); i++) {
                    if (i) s += " ";
                    s += num(p.pts[i].x) + "," + num(p.pts[i].y);
                }
                s += "\"" + prpaint(app, p) + "/>";
                break;
            }
            case mermaidext::PrimType::Slice: {
                float r = p.radius;
                float sx = p.x1 + r * std::cos(p.a0);
                float sy = p.y1 + r * std::sin(p.a0);
                float ex = p.x1 + r * std::cos(p.a1);
                float ey = p.y1 + r * std::sin(p.a1);
                int large = (p.a1 - p.a0) > 3.14159265f ? 1 : 0;
                s += "<path d=\"M " + num(p.x1) + " " + num(p.y1) + " L " +
                     num(sx) + " " + num(sy) + " A " + num(r) + " " + num(r) +
                     " 0 " + std::to_string(large) + " 1 " + num(ex) + " " +
                     num(ey) + " Z\"" + prpaint(app, p) + "/>";
                break;
            }
            case mermaidext::PrimType::Text:
                emitDiagramText(ctx, p, s);
                break;
        }
    }
    s += "</svg>";
    return s;
}

// --- element tree walker ---

struct ExportCtx {
    App& app;
    std::string out;
    std::wstring baseDir;      // for resolving relative image paths
    std::string bodyFontCss;
    std::string monoFontCss;
    std::string textColorCss;
};

void walk(ExportCtx& ctx, const ElementPtr& elem);

void walkChildren(ExportCtx& ctx, const ElementPtr& elem) {
    for (const auto& child : elem->children) walk(ctx, child);
}

void emitImage(ExportCtx& ctx, const ElementPtr& elem) {
    std::string src = elem->url;
    bool remote = src.rfind("http://", 0) == 0 || src.rfind("https://", 0) == 0;
    if (!remote) {
        std::wstring widePath = toWide(src);
        // Resolve relative to the document folder
        if (widePath.size() < 2 || widePath[1] != L':') {
            widePath = ctx.baseDir + L"\\" + widePath;
        }
        std::string data;
        const char* mime = mimeForPath(src);
        if (mime && readBinary(widePath, data)) {
            src = std::string("data:") + mime + ";base64," + base64(data);
        }
    }
    std::string alt;
    for (const auto& child : elem->children) {
        if (child->type == ElementType::Text) alt += child->text;
    }
    ctx.out += "<img src=\"" + htmlEscape(src) + "\" alt=\"" +
               htmlEscape(alt) + "\"/>";
}

void emitMath(ExportCtx& ctx, const ElementPtr& elem, bool display) {
    std::wstring tex = toWide(elem->text);
    MathBoxPtr box = mathParse(ctx.app, tex, display ? 18.0f : 16.0f, display);
    if (box) {
        std::string svg =
            mathBoxSvg(box, ctx.textColorCss, ctx.bodyFontCss);
        if (display) {
            ctx.out += "<div class=\"math-display\">" + svg + "</div>";
        } else {
            ctx.out += "<span class=\"math-inline\">" + svg + "</span>";
        }
        return;
    }
    // Unsupported TeX falls back to the raw source, like the viewer
    if (display) {
        ctx.out += "<pre><code>$$" + htmlEscape(elem->text) + "$$</code></pre>";
    } else {
        ctx.out += "<code>$" + htmlEscape(elem->text) + "$</code>";
    }
}

void walk(ExportCtx& ctx, const ElementPtr& elem) {
    if (!elem) return;
    switch (elem->type) {
        case ElementType::Document:
            walkChildren(ctx, elem);
            break;
        case ElementType::Properties:
            break;  // frontmatter stays out of the export, like print
        case ElementType::Paragraph:
            ctx.out += "<p>";
            walkChildren(ctx, elem);
            ctx.out += "</p>\n";
            break;
        case ElementType::Heading: {
            int level = std::max(1, std::min(6, elem->level));
            ctx.out += "<h" + std::to_string(level) + ">";
            walkChildren(ctx, elem);
            ctx.out += "</h" + std::to_string(level) + ">\n";
            break;
        }
        case ElementType::CodeBlock: {
            // Mermaid fences stay CodeBlocks in the parse tree; the viewer
            // (and this exporter) decide at render time, like layoutCodeBlock
            if (elem->language == "mermaid") {
                std::string source;
                for (const auto& child : elem->children) {
                    if (child->type == ElementType::Text) {
                        source += child->text;
                    }
                }
                std::string svg = diagramSvg(ctx.app, source, ctx.bodyFontCss,
                                             ctx.monoFontCss);
                if (!svg.empty()) {
                    ctx.out += "<div class=\"diagram\">" + svg + "</div>\n";
                    break;
                }
            }
            ctx.out += "<pre><code";
            if (!elem->language.empty()) {
                ctx.out +=
                    " class=\"language-" + htmlEscape(elem->language) + "\"";
            }
            ctx.out += ">";
            for (const auto& child : elem->children) {
                if (child->type == ElementType::Text) {
                    ctx.out += htmlEscape(child->text);
                }
            }
            ctx.out += "</code></pre>\n";
            break;
        }
        case ElementType::MermaidDiagram: {
            std::string svg = diagramSvg(ctx.app, elem->text, ctx.bodyFontCss,
                                         ctx.monoFontCss);
            if (!svg.empty()) {
                ctx.out += "<div class=\"diagram\">" + svg + "</div>\n";
            } else {
                ctx.out += "<pre><code class=\"language-mermaid\">" +
                           htmlEscape(elem->text) + "</code></pre>\n";
            }
            break;
        }
        case ElementType::BlockQuote: {
            // GitHub alert callouts: class for the palette plus the same
            // icon-and-label title line the viewer draws
            static const struct {
                const char* cls;
                const char* title;  // icon (text presentation) + label
            } kAlert[] = {
                {"note", "\xE2\x93\x98\xEF\xB8\x8E  Note"},
                {"tip", "\xF0\x9F\x92\xA1\xEF\xB8\x8E  Tip"},
                {"important", "\xE2\x9D\x97\xEF\xB8\x8E  Important"},
                {"warning", "\xE2\x9A\xA0\xEF\xB8\x8E  Warning"},
                {"caution", "\xE2\x9B\x94\xEF\xB8\x8E  Caution"},
            };
            int kind =
                elem->alertKind >= 1 && elem->alertKind <= 5 ? elem->alertKind
                                                             : 0;
            if (kind) {
                const auto& a = kAlert[kind - 1];
                ctx.out += std::string("<blockquote class=\"alert-") + a.cls +
                           "\"><p class=\"alert-title\">" + a.title + "</p>";
            } else {
                ctx.out += "<blockquote>";
            }
            walkChildren(ctx, elem);
            ctx.out += "</blockquote>\n";
            break;
        }
        case ElementType::List:
            if (elem->ordered) {
                ctx.out += "<ol";
                if (elem->start != 1) {
                    ctx.out += " start=\"" + std::to_string(elem->start) + "\"";
                }
                ctx.out += ">";
            } else {
                ctx.out += "<ul>";
            }
            walkChildren(ctx, elem);
            ctx.out += elem->ordered ? "</ol>\n" : "</ul>\n";
            break;
        case ElementType::ListItem:
            if (elem->isTask) {
                ctx.out += "<li class=\"task\"><input type=\"checkbox\" "
                           "disabled";
                if (elem->taskChecked) ctx.out += " checked";
                ctx.out += "/> ";
            } else {
                ctx.out += "<li>";
            }
            walkChildren(ctx, elem);
            ctx.out += "</li>";
            break;
        case ElementType::HorizontalRule:
            ctx.out += "<hr/>\n";
            break;
        case ElementType::Table:
            ctx.out += "<table>\n";
            for (size_t i = 0; i < elem->children.size(); i++) {
                const auto& row = elem->children[i];
                if (row->type != ElementType::TableRow) continue;
                ctx.out += "<tr>";
                const char* cellTag = i == 0 ? "th" : "td";
                for (const auto& cell : row->children) {
                    if (cell->type != ElementType::TableCell) continue;
                    ctx.out += std::string("<") + cellTag;
                    if (cell->align == 2) ctx.out += " align=\"center\"";
                    else if (cell->align == 3) ctx.out += " align=\"right\"";
                    ctx.out += ">";
                    walkChildren(ctx, cell);
                    ctx.out += std::string("</") + cellTag + ">";
                }
                ctx.out += "</tr>\n";
            }
            ctx.out += "</table>\n";
            break;
        case ElementType::TableRow:
        case ElementType::TableCell:
            walkChildren(ctx, elem);  // reached only outside a Table
            break;
        case ElementType::HtmlBlock:
            walkChildren(ctx, elem);
            break;
        case ElementType::Text:
            ctx.out += htmlEscape(elem->text);
            break;
        case ElementType::Code:
            ctx.out += "<code>";
            ctx.out += htmlEscape(elem->text);
            walkChildren(ctx, elem);
            ctx.out += "</code>";
            break;
        case ElementType::Emphasis:
            ctx.out += "<em>";
            walkChildren(ctx, elem);
            ctx.out += "</em>";
            break;
        case ElementType::Strong:
            ctx.out += "<strong>";
            walkChildren(ctx, elem);
            ctx.out += "</strong>";
            break;
        case ElementType::Highlight:
            ctx.out += "<mark>";
            walkChildren(ctx, elem);
            ctx.out += "</mark>";
            break;
        case ElementType::Strikethrough:
            ctx.out += "<del>";
            walkChildren(ctx, elem);
            ctx.out += "</del>";
            break;
        case ElementType::Superscript:
            ctx.out += "<sup>";
            walkChildren(ctx, elem);
            ctx.out += "</sup>";
            break;
        case ElementType::Subscript:
            ctx.out += "<sub>";
            walkChildren(ctx, elem);
            ctx.out += "</sub>";
            break;
        case ElementType::Link: {
            bool wiki = elem->url.rfind("wiki:", 0) == 0;
            if (wiki) {
                // Single-file export: wiki targets stay as styled text
                ctx.out += "<span class=\"wikilink\">";
                walkChildren(ctx, elem);
                ctx.out += "</span>";
            } else {
                ctx.out += "<a href=\"" + htmlEscape(elem->url) + "\"";
                if (!elem->title.empty()) {
                    ctx.out += " title=\"" + htmlEscape(elem->title) + "\"";
                }
                ctx.out += ">";
                walkChildren(ctx, elem);
                ctx.out += "</a>";
            }
            break;
        }
        case ElementType::Image:
            emitImage(ctx, elem);
            break;
        case ElementType::SoftBreak:
            ctx.out += "\n";
            break;
        case ElementType::HardBreak:
            ctx.out += "<br/>\n";
            break;
        case ElementType::Ruby:
            ctx.out += "<ruby>";
            for (const auto& child : elem->children) {
                if (child->type == ElementType::RubyText) {
                    ctx.out += "<rt>" + htmlEscape(child->text);
                    for (const auto& g : child->children) walk(ctx, g);
                    ctx.out += "</rt>";
                } else {
                    walk(ctx, child);
                }
            }
            ctx.out += "</ruby>";
            break;
        case ElementType::RubyText:
            break;  // handled inside Ruby
        case ElementType::MathInline:
            emitMath(ctx, elem, false);
            break;
        case ElementType::MathDisplay:
            emitMath(ctx, elem, true);
            break;
    }
}

std::string themeCss(const App& app, const std::string& bodyFont,
                     const std::string& monoFont) {
    const D2DTheme& t = app.theme;
    D2D1_COLOR_F border = t.text;
    border.a = 0.18f;
    D2D1_COLOR_F mutedText = t.text;
    mutedText.a = 0.65f;
    std::string css;
    css += "body{background:" + colorCss(t.background) + ";color:" +
           colorCss(t.text) + ";font-family:" + bodyFont +
           ";line-height:1.6;max-width:820px;margin:0 auto;"
           "padding:32px 24px;}";
    css += "h1,h2,h3,h4,h5,h6{color:" + colorCss(t.heading) +
           ";line-height:1.25;}";
    css += "h1{border-bottom:2px solid " + colorCss(t.accent) +
           ";padding-bottom:8px;}";
    css += "a{color:" + colorCss(t.link) + ";}";
    css += ".wikilink{color:" + colorCss(t.link) +
           ";border-bottom:1px dashed " + colorCss(t.link) + ";}";
    css += "code{font-family:" + monoFont + ";background:" +
           colorCss(t.codeBackground) + ";color:" + colorCss(t.code) +
           ";padding:2px 6px;border-radius:4px;font-size:0.9em;}";
    css += "pre{background:" + colorCss(t.codeBackground) +
           ";padding:14px 16px;border-radius:8px;overflow-x:auto;}";
    css += "pre code{background:none;padding:0;}";
    css += "blockquote{border-left:4px solid " +
           colorCss(t.blockquoteBorder) +
           ";margin:1em 0;padding:2px 18px;color:" + colorCss(mutedText) +
           ";}";
    // github.com's alert accents, light or dark side picked by the theme
    // (the same table the viewer uses)
    {
        static const struct {
            const char* cls;
            uint32_t light;
            uint32_t dark;
        } kAlertCss[] = {
            {"note", 0x0969DA, 0x4493F8},
            {"tip", 0x1A7F37, 0x3FB950},
            {"important", 0x8250DF, 0xAB7DF8},
            {"warning", 0x9A6700, 0xD29922},
            {"caution", 0xCF222E, 0xF85149},
        };
        css += ".alert-title{font-weight:600;margin:0 0 4px 0;}";
        for (const auto& a : kAlertCss) {
            std::string hex = colorCss(hexColor(t.isDark ? a.dark : a.light));
            css += std::string("blockquote.alert-") + a.cls +
                   "{border-color:" + hex + ";}";
            css += std::string("blockquote.alert-") + a.cls +
                   " .alert-title{color:" + hex + ";}";
        }
    }
    css += "table{border-collapse:collapse;margin:1em 0;}";
    css += "th,td{border:1px solid " + colorCss(border) +
           ";padding:6px 13px;}";
    css += "th{background:" + colorCss(t.codeBackground) + ";}";
    css += "hr{border:none;border-top:1px solid " + colorCss(border) +
           ";margin:24px 0;}";
    css += "img{max-width:100%;}";
    css += "li.task{list-style:none;margin-left:-20px;}";
    css += ".diagram{text-align:center;margin:1em 0;overflow-x:auto;}";
    css += ".math-display{text-align:center;margin:1em 0;}";
    css += ".math-inline svg{vertical-align:middle;}";
    // ==highlight== uses the viewer's marker-pen yellow, not the accent
    css += "mark{background:" +
           colorCss(t.isDark ? D2D1::ColorF(0.98f, 0.80f, 0.25f, 0.28f)
                             : D2D1::ColorF(1.00f, 0.88f, 0.20f, 0.45f)) +
           ";color:inherit;padding:0 2px;}";
    return css;
}

std::string cssFontList(const std::wstring& family, bool mono) {
    std::string name = toUtf8(family);
    std::string list = "'" + name + "'";
    list += mono ? ",Consolas,monospace" : ",'Segoe UI',sans-serif";
    return list;
}

}  // namespace

bool exportHtmlFile(App& app, const std::wstring& path) {
    if (!app.root) return false;

    std::string bodyFont = cssFontList(app.theme.fontFamily, false);
    std::string monoFont = cssFontList(app.theme.codeFontFamily, true);

    ExportCtx ctx{app, {}, {}, bodyFont, monoFont, colorCss(app.theme.text)};
    if (!app.currentFile.empty()) {
        std::wstring wide = toWide(app.currentFile);
        size_t slash = wide.find_last_of(L"/\\");
        if (slash != std::wstring::npos) ctx.baseDir = wide.substr(0, slash);
    }

    std::string title = "Tinta export";
    if (!app.currentFile.empty()) {
        size_t slash = app.currentFile.find_last_of("/\\");
        title = slash == std::string::npos ? app.currentFile
                                           : app.currentFile.substr(slash + 1);
    }

    ctx.out += "<!doctype html>\n<html>\n<head>\n<meta charset=\"utf-8\"/>\n";
    ctx.out += "<meta name=\"viewport\" content=\"width=device-width, "
               "initial-scale=1\"/>\n";
    ctx.out += "<title>" + htmlEscape(title) + "</title>\n";
    ctx.out += "<style>" + themeCss(app, bodyFont, monoFont) + "</style>\n";
    ctx.out += "</head>\n<body>\n";
    walk(ctx, app.root);
    ctx.out += "</body>\n</html>\n";

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(ctx.out.data(), ctx.out.size());
    return out.good();
}

void exportDocumentAs(App& app, HWND hwnd) {
    if (!app.root) return;

    wchar_t path[MAX_PATH] = L"";
    // Default name: the document's stem
    if (!app.currentFile.empty()) {
        std::wstring wide = toWide(app.currentFile);
        size_t slash = wide.find_last_of(L"/\\");
        std::wstring stem =
            slash == std::wstring::npos ? wide : wide.substr(slash + 1);
        size_t dot = stem.find_last_of(L'.');
        if (dot != std::wstring::npos) stem = stem.substr(0, dot);
        wcsncpy_s(path, stem.c_str(), _TRUNCATE);
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Web page (*.html)\0*.html\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"html";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return;

    bool ok = exportHtmlFile(app, path);

    app.copiedNotificationKey = ok ? "toast.exported" : "toast.save_failed";
    app.showCopiedNotification = true;
    app.copiedNotificationStart = std::chrono::steady_clock::now();
    startNotificationTimer(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}
