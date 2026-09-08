#include "math_render.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <wrl/client.h>
#include <algorithm>
#include <vector>
#include <functional>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
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
