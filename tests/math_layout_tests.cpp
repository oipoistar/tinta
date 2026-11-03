#include "math_render.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <wrl/client.h>
#include <algorithm>
#include <vector>
#include <functional>
#include <filesystem>
#include <map>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

void checkMarkdownFixture(App& app, const char* filename, size_t minimumMath,
                          bool mixed, int tableColumns = 0) {
    using qmd::ElementType;
    auto path = std::filesystem::path(TINTA_MATH_FIXTURE).parent_path() / filename;
    std::ifstream file(path, std::ios::binary);
    check(file.good(), "Markdown fixture is available");
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto doc = app.parser.parse(source);
    check(doc.success, "mixed Markdown parses");
    std::map<ElementType, size_t> counts, mathWithin;
    std::vector<ElementType> ancestors;
    size_t equations = 0;
    bool literalCode = false;
    std::function<void(const qmd::ElementPtr&)> visit = [&](const qmd::ElementPtr& element) {
        if (!element) return;
        ++counts[element->type];
        if (element->type == ElementType::TableRow && tableColumns > 0)
            check(element->children.size() == static_cast<size_t>(tableColumns),
                  "math separators do not split Markdown table cells");
        if (element->type == ElementType::Text &&
            std::find(ancestors.begin(), ancestors.end(), ElementType::Code) != ancestors.end() &&
            element->text.find('$') != std::string::npos) literalCode = true;
        if (element->type == ElementType::MathInline || element->type == ElementType::MathDisplay) {
            ++equations;
            for (auto ancestor : ancestors) ++mathWithin[ancestor];
            int length = MultiByteToWideChar(CP_UTF8, 0, element->text.data(),
                                            static_cast<int>(element->text.size()), nullptr, 0);
            std::wstring tex(length, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, element->text.data(),
                               static_cast<int>(element->text.size()), tex.data(), length);
            if (!mathParse(app, tex, 24, element->type == ElementType::MathDisplay)) {
                std::cerr << filename << ": unrendered equation: " << element->text << '\n';
                check(false, "every mixed fixture equation renders");
            }
        }
        ancestors.push_back(element->type);
        for (const auto& child : element->children) visit(child);
        ancestors.pop_back();
    };
    if (doc.success) visit(doc.root);
    check(minimumMath ? equations >= minimumMath : equations == 0,
          "fixture math spans are recognized without parsing literal code");
    check(mathWithin[ElementType::Code] == 0 && mathWithin[ElementType::CodeBlock] == 0,
          "inline and fenced code remain literal");
    if (mixed) {
        for (auto type : {ElementType::Heading, ElementType::TableCell,
                          ElementType::ListItem, ElementType::BlockQuote})
            check(mathWithin[type] > 0, "math survives inside existing block structures");
        for (auto type : {ElementType::Strong, ElementType::Emphasis, ElementType::Code,
                          ElementType::Link, ElementType::Highlight, ElementType::Strikethrough})
            if (counts[type] == 0) {
                std::cerr << filename << ": missing inline type " << static_cast<int>(type) << '\n';
                check(false, "surrounding inline formatting survives");
            }
    }
    if (std::string(filename) == "math-mixed-layout.md") {
        check(mathWithin[ElementType::Link] > 0 && mathWithin[ElementType::Strong] > 0 &&
              mathWithin[ElementType::Emphasis] > 0, "math preserves inline wrappers");
        check(literalCode && counts[ElementType::CodeBlock] == 2,
              "literal TeX and Mermaid fences survive mixed parsing");
    }
    std::cout << filename << ": " << equations << " equations\n";
}

// #314: a closing $ followed by a letter closes, as pandoc reads it; a digit
// after it, or a space before it, leaves it open
void closingDollars(App& app) {
    auto spans = [&](const std::string& source) {
        std::vector<std::string> found;
        auto doc = app.parser.parse(source);
        std::function<void(const qmd::ElementPtr&)> walk = [&](const qmd::ElementPtr& e) {
            if (!e) return;
            if (e->type == qmd::ElementType::MathInline) found.push_back(e->text);
            for (const auto& child : e->children) walk(child);
        };
        if (doc.success) walk(doc.root);
        return found;
    };
    using Spans = std::vector<std::string>;
    check(spans("$R=50\\,\\mu$m\xEF\xBC\x8C$\\gamma$\n") == Spans{"R=50\\,\\mu", "\\gamma"},
          "a $ before a unit's letters closes, and the next equation opens after it");
    check(spans("$x$y and *a* $b$c\n") == Spans{"x", "b"}, "a $ followed by a letter closes");
    check(spans("$x$, $y$ and ($z$)\n") == Spans{"x", "y", "z"}, "a $ before punctuation or a space still closes");
    check(spans("It costs $5 and $6, or $10.\n").empty(), "prices stay text");
    check(spans("Hello $name, your total is $amount.\n").empty(), "a $ after a space does not close");
    check(spans("$x$5 stays text\n").empty(), "a $ followed by a digit does not close");
    check(spans("`$a$b` and \\$c$d\n").empty(), "code and an escaped $ stay text");
}

// A box's text runs, and the corners of its strokes and rules, as its SVG
// has them
struct Drawn {
    std::vector<std::pair<std::string, float>> runs;  // text, x
    std::vector<std::pair<float, float>> strokes;     // x, y
};
Drawn drawnOf(const MathBoxPtr& box) {
    Drawn d;
    const std::string svg = box ? mathBoxSvg(box, "#000", "x") : std::string();
    auto value = [&](size_t at, const std::string& name) {
        const std::string key = " " + name + "=\"";
        return std::stof(svg.substr(svg.find(key, at) + key.size()));
    };
    for (size_t at = svg.find("<text "); at != std::string::npos; at = svg.find("<text ", at + 1)) {
        const size_t open = svg.find('>', at), close = svg.find("</text>", open);
        d.runs.push_back({svg.substr(open + 1, close - open - 1), value(at, "x")});
    }
    for (size_t at = svg.find("<line "); at != std::string::npos; at = svg.find("<line ", at + 1)) {
        d.strokes.push_back({value(at, "x1"), value(at, "y1")});
        d.strokes.push_back({value(at, "x2"), value(at, "y2")});
    }
    for (size_t at = svg.find("<rect "); at != std::string::npos; at = svg.find("<rect ", at + 1)) {
        const float x = value(at, "x"), y = value(at, "y");
        d.strokes.push_back({x, y});
        d.strokes.push_back({x + value(at, "width"), y + value(at, "height")});
    }
    return d;
}

// #314: the reported equations, signs, letters beside brackets, accents,
// numbers and references
void reportedLayouts(App& app) {
    constexpr float em = 24.0f;
    auto width = [&](const wchar_t* tex, bool display = false) { return mathBoxWidth(mathParse(app, tex, em, display)); };
    for (const wchar_t* tex : {
             LR"([\![-p + 2\mu\,\partial_n u_n]\!] + [\![\mathbf n\cdot\mathbf T_E\cdot\mathbf n]\!] = \gamma\kappa,
\qquad
[\![\mathbf n\cdot\mathbf T_E\cdot\mathbf n]\!] = \tfrac12\left[\!\left[\varepsilon\left(E_n^2 - E_t^2\right)\right]\!\right].
\tag{1.8})",
             LR"(\rho_{e,c} = \frac{\varepsilon_0E_c}{R},\quad q_c = \varepsilon_0E_c,\quad j_c = K_lE_c.
\tag{2.1})"}) {
        check(mathParse(app, tex, em, true) != nullptr, "the reported tagged equations render");
    }

    // A sign after an opening bracket, a comma or a relation, or before a
    // closing one, is unary: as tight as a sign set alone in braces
    for (const auto& pair : {std::pair<const wchar_t*, const wchar_t*>{LR"([-p])", LR"([{-}p])"},
                             {LR"((-x))", LR"(({-}x))"}, {LR"(\{-1\})", LR"(\{{-}1\})"},
                             {LR"(f(x,-y))", LR"(f(x,{-}y))"}, {LR"(x=\quad-1)", LR"(x=\quad{-}1)"},
                             {LR"((a-))", LR"((a{-}))"}}) {
        check(std::abs(width(pair.first) - width(pair.second)) < 0.01f, "a sign with nothing to bind is unary");
    }
    check(width(L"a-b") > width(L"a{-}b") + em * 0.3f, "a sign between two terms stays binary");
    check(std::abs(width(L"a,b") - width(L"a{,}b") - em * 0.12f) < 0.01f, "a comma takes its space after it only");

    // A slanted letter's lean keeps an upright bracket and a superscript clear
    auto gap = [&](const wchar_t* tex) {
        const Drawn d = drawnOf(mathParse(app, tex, em, false));
        return d.runs.size() >= 2 ? d.runs[1].second - d.runs[0].second : 0.0f;
    };
    check(gap(L"x)") > gap(LR"(\mathrm{x}))") + em * 0.05f, "a bracket clears a slanted letter's lean");
    check(gap(L"E^2") > gap(LR"(\mathrm{E}^2)") + em * 0.05f, "a superscript clears a slanted base's lean");
    check(std::abs(gap(L"E_n") - gap(LR"(\mathrm{E}_n)")) < 0.01f, "a subscript tucks in under the lean");

    // A drawn bracket keeps clear of what stands before it
    for (bool display : {false, true}) {
        const Drawn d = drawnOf(mathParse(app, LR"(\left(\frac{a}{b}\right))", em, display));
        float left = 1e9f;
        for (const auto& p : d.strokes) left = std::min(left, p.first);
        check(!d.strokes.empty() && left > em * (display ? 0.15f : 0.08f) + em * 0.05f,
              "a drawn bracket's bulge stands off the box's edge");
    }

    // An accent sits just over its letter, within the letter's own height
    const MathBoxPtr plain = mathParse(app, L"x", em, false);
    for (const wchar_t* tex : {LR"(\tilde{x})", LR"(\hat{x})", LR"(\bar{x})", LR"(\dot{x})"}) {
        const MathBoxPtr accented = mathParse(app, tex, em, false);
        const Drawn d = drawnOf(accented);
        float lowest = -1e9f;
        for (const auto& p : d.strokes) lowest = std::max(lowest, p.second);
        check(accented && !d.strokes.empty() && mathBoxBaseline(accented) - lowest < em * 0.85f,
              "an accent sits just over its letter");
        check(std::abs(mathBoxHeight(accented) - mathBoxHeight(plain)) < 0.5f, "an accented x stays within the line's ascent");
    }
    const Drawn phi = drawnOf(mathParse(app, LR"(\tilde\phi)", em, false));
    float lowest = -1e9f;
    for (const auto& p : phi.strokes) lowest = std::max(lowest, p.second);
    check(mathBoxBaseline(mathParse(app, LR"(\tilde\phi)", em, false)) - lowest < em * 1.0f, "the tilde sits on the phi");

    // \tag: inline, its number follows a quad on; on display it stands
    // apart for the line's right, the equation as wide as without it
    check(width(LR"(x=1\tag{1.8})") > width(L"x=1") + em, "an inline tag follows the equation");
    const MathBoxPtr shown = mathParse(app, LR"(x=1\label{eq:one}\tag{1.8})", em, true);
    check(mathBoxTag(shown) && mathBoxTagText(shown) == L"(1.8)" && mathBoxWidth(mathBoxTag(shown)) > em,
          "a display tag is a box of its own");
    check(std::abs(mathBoxWidth(shown) - width(L"x=1", true)) < 0.01f, "the equation keeps its own width");
    check(!mathBoxTag(mathParse(app, L"x=1", em, true)), "an untagged equation has no number");

    // \eqref and \ref read the number a \label's \tag gives it
    std::map<std::wstring, std::wstring> labels;
    mathCollectLabels(LR"(E=mc^2 \label{eq:energy} \tag{2.1})", labels);
    mathCollectLabels(LR"(F=ma \label{eq:force})", labels);
    check(labels.size() == 1 && labels[L"eq:energy"] == L"2.1", "a label takes its equation's tag");
    check(mathResolveRefs(LR"(\eqref{eq:energy}+\ref{eq:energy}+\eqref{eq:force})", labels) ==
              LR"(\text{(2.1)}+\text{2.1}+\eqref{eq:force})",
          "references read their number; one with none stays");

    // settings.ini's mathFont= when installed, else Cambria Math
    const std::wstring saved = app.mathFont;
    app.mathFont = L"Segoe UI";
    mathClearCache();
    check(mathBoxSvg(mathParse(app, L"x", em, false), "#000", "x").find("font-family=\"Segoe UI\"") != std::string::npos,
          "mathFont sets the equations' face");
    app.mathFont = L"No Such Math Face";
    mathClearCache();
    check(mathBoxSvg(mathParse(app, L"x", em, false), "#000", "x").find("font-family=\"Cambria Math\"") != std::string::npos,
          "a math face not installed falls back to Cambria Math");
    app.mathFont = saved;
    mathClearCache();
}

bool rasterize(const std::vector<MathBoxPtr>& boxes, const wchar_t* path = nullptr) {
    using Microsoft::WRL::ComPtr;
    UINT width = 32, height = 16;
    for (const auto& box : boxes) {
        width = std::max(width, static_cast<UINT>(std::ceil(mathBoxWidth(box))) + 32);
        height += static_cast<UINT>(std::ceil(mathBoxHeight(box))) + 24;
    }
    ComPtr<IWICImagingFactory> wic;
    ComPtr<ID2D1Factory> d2d;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&wic))) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf()))) return false;
    ComPtr<IWICBitmap> bitmap;
    if (FAILED(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap))) return false;
    ComPtr<ID2D1RenderTarget> target;
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE);
    if (FAILED(d2d->CreateWicBitmapRenderTarget(bitmap.Get(), props, &target))) return false;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &brush))) return false;
    target->BeginDraw();
    target->Clear(D2D1::ColorF(D2D1::ColorF::White));
    float y = 16;
    for (const auto& box : boxes) {
        mathBoxDrawTo(target.Get(), brush.Get(), box, 16, y, D2D1::ColorF(0x17212B));
        y += std::ceil(mathBoxHeight(box)) + 24;
    }
    if (FAILED(target->EndDraw())) return false;
    std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
    if (FAILED(bitmap->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) return false;
    size_t dark = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) if (pixels[i] < 200) ++dark;
    if (dark < 20) return false;
    if (!path) return true;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE)) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(width, height))) return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    if (FAILED(frame->SetPixelFormat(&format)) || FAILED(frame->WriteSource(bitmap.Get(), nullptr)) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit())) return false;
    return true;
}
}

int main(int argc, char** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        App app;
        HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                        reinterpret_cast<IUnknown**>(&app.dwriteFactory));
        if (FAILED(hr)) return 2;
        std::ifstream fixture(TINTA_MATH_FIXTURE, std::ios::binary);
        std::string fixtureText((std::istreambuf_iterator<char>(fixture)), std::istreambuf_iterator<char>());
        auto fixtureDoc = app.parser.parse(fixtureText);
        size_t equations = 0;
        std::function<void(const qmd::ElementPtr&)> visit = [&](const qmd::ElementPtr& element) {
            if (!element) return;
            if (element->type == qmd::ElementType::MathInline || element->type == qmd::ElementType::MathDisplay) {
                ++equations;
                std::wstring tex(element->text.begin(), element->text.end()); // fixture TeX is ASCII
                check(mathParse(app, tex, 24, element->type == qmd::ElementType::MathDisplay) != nullptr,
                      "Markdown compatibility fixture renders completely");
            }
            for (const auto& child : element->children) visit(child);
        };
        if (fixtureDoc.success) visit(fixtureDoc.root);
        check(equations >= 15, "all fixture equations were recognized by Markdown");
        checkMarkdownFixture(app, "math-mixed-layout.md", 25, true, 3);
        checkMarkdownFixture(app, "math-inline-stress.md", 9, true, 2);
        checkMarkdownFixture(app, "markdown-regression-control.md", 0, false, 3);
        checkMarkdownFixture(app, "application-menu.md", 5, false, 3);
        checkMarkdownFixture(app, "application-menu-companion.md", 1, false, 2);
        closingDollars(app);
        auto markdown = app.parser.parse("$$\n\\begin{bmatrix}1&2\\\\5&6\\end{bmatrix}\\longrightarrow 6\n$$");
        check(markdown.success && !markdown.root->children.empty(), "display math Markdown parses");
        if (markdown.success && !markdown.root->children.empty()) {
            const auto& children = markdown.root->children.front()->children;
            bool found = false;
            for (const auto& child : children) {
                if (child->type != qmd::ElementType::MathDisplay) continue;
                found = true;
                check(child->text.find("1&2\\\\5&6") != std::string::npos,
                      "Markdown preserves TeX alignment and row separators");
            }
            check(found, "display math reaches native math element");
        }
        auto simple = mathParse(app, LR"(\begin{matrix}1&2\\3&4\end{matrix})", 24, true);
        auto tall = mathParse(app, LR"(\begin{bmatrix}1&2\\3&4\\5&6\\7&8\\9&0\end{bmatrix})", 24, true);
        auto nested = mathParse(app, LR"(\begin{pmatrix}\frac{1}{x^2}&\sqrt{y}\\z&\begin{bmatrix}1\\2\end{bmatrix}\end{pmatrix})", 24, true);
        check(simple && tall && nested, "matrix layouts succeed");
        check(mathBoxHeight(tall) > mathBoxHeight(simple) * 1.8f, "all matrix rows contribute height");
        check(mathBoxWidth(tall) < mathBoxWidth(simple) * 2, "tall brackets retain modest width");
        for (const auto& box : {simple, tall, nested}) {
            check(std::isfinite(mathBoxWidth(box)) && mathBoxWidth(box) > 0 && mathBoxHeight(box) > 0,
                  "finite nonempty layout");
            check(mathBoxBaseline(box) > 0 && mathBoxBaseline(box) < mathBoxHeight(box), "baseline inside box");
        }
        float above = 0, below = 0;
        constexpr float textBaseline = 19.2f, normalLineHeight = 40.8f;
        for (const auto& box : {simple, tall, nested}) mathExpandLine(box, textBaseline, normalLineHeight, above, below);
        check(above > 0 && below > 0, "tall inline math reserves space above and below text");
        for (const auto& box : {simple, tall, nested}) {
            float top = above + textBaseline - mathBoxBaseline(box);
            check(top >= -0.01f && top + mathBoxHeight(box) <= normalLineHeight + above + below + 0.01f,
                  "every inline box fits between neighboring lines with a shared text baseline");
        }
        check(!mathParse(app, LR"(\begin{bmatrix}1&2\end{matrix})", 24, true), "invalid matrix falls back in renderer");
        auto svg = mathBoxSvg(tall, "#111111", "Segoe UI");
        check(svg.find("<line") != std::string::npos && svg.find("<text") != std::string::npos,
              "SVG exports tall bracket strokes and cells");
        auto height = [&](const wchar_t* tex, bool display = true) {
            return mathBoxHeight(mathParse(app, tex, 24, display));
        };
        check(height(LR"(\dfrac{1}{2})", false) > height(LR"(\tfrac{1}{2})", false) * 1.15f,
              "dfrac and tfrac have distinct sizes even inline");
        check(height(LR"(\sum_{i=1}^n x_i)") > height(LR"(\sum\nolimits_{i=1}^n x_i)"), "display limits are stacked");
        check(height(LR"(\int\limits_0^1 x)") > height(LR"(\int_0^1 x)"), "integrals obey explicit limits");
        check(height(LR"(\sum_{\substack{i>0\\j>0}}x)") > height(LR"(\sum_{i>0}x)"), "substack adds rows in limits");
        auto width = [&](const wchar_t* tex) { return mathBoxWidth(mathParse(app, tex, 24, false)); };
        check(width(LR"(x\!y)") < width(L"xy"), "negative spacing reduces width");
        check(width(LR"(\sqrt[123]{x})") > width(LR"(\sqrt{x})"), "radical index reserves width");
        check(std::abs(width(LR"(x\hspace{1em}y)") - width(L"xy") - 24) < 0.01f, "em spacing uses current size");
        check(std::abs(width(LR"(x\kern 3pt y)") - width(L"xy") - 4) < 0.01f, "point spacing uses logical pixels");
        check(height(LR"(\Bigg(x\Bigg))") > height(LR"(\big(x\big))"), "explicit delimiter sizes differ");
        auto textSvg = mathBoxSvg(mathParse(app, LR"(\text{for all }x\in\mathbb{R})", 24, true), "#111", "Segoe UI");
        check(textSvg.find("for all ") != std::string::npos && textSvg.find("xml:space=\"preserve\"") != std::string::npos,
              "SVG preserves text spaces");
        reportedLayouts(app);
        mathBoxRetain(app, tall, 5, 10, D2D1::ColorF(D2D1::ColorF::Black));
        check(!app.layoutTextRuns.empty() && !app.layoutLines.empty(), "retained viewer receives same primitives");
        app.clearLayoutCache();
        check(rasterize({simple, tall, nested}), "native Direct2D offscreen drawing produces pixels");
        if (argc > 1) {
            std::ofstream out(argv[1]);
            out << "<!doctype html><meta charset=utf-8><style>body{font:18px Segoe UI;padding:30px}section{margin:25px}</style>";
            std::vector<MathBoxPtr> samples;
            for (const auto* tex : {
                LR"(\begin{bmatrix}1&2\\5&6\end{bmatrix}\longrightarrow 6,\qquad\begin{bmatrix}3&4\\7&8\end{bmatrix}\longrightarrow 8)",
                LR"(\begin{bmatrix}1&2\\3&4\\5&6\\7&8\\9&0\end{bmatrix})",
                LR"(\begin{pmatrix}\frac{1}{x^2}&\sqrt{y}\\z&\begin{bmatrix}1\\2\end{bmatrix}\end{pmatrix})",
                LR"(f(x)=\begin{cases}x^2&x>0\\0&x=0\\-x&x<0\end{cases})",
                LR"(\begin{aligned}a+b&=c\\x&=y+z\end{aligned})",
                LR"(\sum_{i=1}^n x_i\qquad\int_0^1 f(x)\,dx\qquad\lim_{n\to\infty}\frac{1}{n}=0)",
                LR"(\dfrac{1}{2}+\tfrac{1}{2}\qquad\binom{n}{k}\qquad\sqrt[3]{\frac{x+1}{y}})",
                LR"(\text{for all }x\in\mathbb{R},\quad\mathcal{L}=\mathfrak{g}+\boldsymbol{\alpha})",
                LR"(A\xrightarrow[n\to\infty]{f}B\qquad\overset{!}{=}\qquad\underbrace{a+b+c}_{\text{sum}})",
                LR"(\widetilde{xyz}+\dot{x}+\ddot{x}+\underline{y}+\boxed{x=2}+\cancel{x})",
                LR"(\sum_{\substack{i>0\\j>0}}a_{ij}\qquad x^{y^{z^2}})",
                LR"(\left\{\frac{x}{y}\middle|x>0\right\}\qquad\bigl(x\bigr)\Bigl(x\Bigr)\Biggl(x\Biggr))",
                LR"(\begin{array}{r|l}\hline 1&22\\\hline 333&4\\\hline\end{array}\qquad a\equiv b\pmod{n})",
                LR"(\newcommand{\norm}[1]{\left\lVert#1\right\rVert}\norm{\frac{x}{y}}\geq0)"}) {
                auto box = mathParse(app, tex, 24, true);
                check(box != nullptr, "visual sample renders");
                samples.push_back(box);
                out << "<section>" << mathBoxSvg(box, "#17212b", "Segoe UI") << "</section>";
            }
            if (argc > 2) {
                std::string filename = argv[2];
                std::wstring path(filename.begin(), filename.end());
                check(rasterize(samples, path.c_str()), "native sample sheet exports to PNG");
            }
        }
        mathClearCache();
    }
    CoUninitialize();
    std::cout << "Math layout: " << failures << " failures\n";
    return failures ? 1 : 0;
}
