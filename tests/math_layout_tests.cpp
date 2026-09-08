#include "math_render.h"

#include <cmath>
#include <fstream>
#include <iostream>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main(int argc, char** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        App app;
        HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                        reinterpret_cast<IUnknown**>(&app.dwriteFactory));
        if (FAILED(hr)) return 2;
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
        auto textSvg = mathBoxSvg(mathParse(app, LR"(\text{for all }x\in\mathbb{R})", 24, true), "#111", "Segoe UI");
        check(textSvg.find("for all ") != std::string::npos && textSvg.find("xml:space=\"preserve\"") != std::string::npos,
              "SVG preserves text spaces");
        mathBoxRetain(app, tall, 5, 10, D2D1::ColorF(D2D1::ColorF::Black));
        check(!app.layoutTextRuns.empty() && !app.layoutLines.empty(), "retained viewer receives same primitives");
        app.clearLayoutCache();
        if (argc > 1) {
            std::ofstream out(argv[1]);
            out << "<!doctype html><meta charset=utf-8><style>body{font:18px Segoe UI;padding:30px}section{margin:25px}</style>";
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
                LR"(\sum_{\substack{i>0\\j>0}}a_{ij}\qquad x^{y^{z^2}})"}) {
                out << "<section>" << mathBoxSvg(mathParse(app, tex, 24, true), "#17212b", "Segoe UI") << "</section>";
            }
        }
        mathClearCache();
    }
    CoUninitialize();
    std::cout << "Math layout: " << failures << " failures\n";
    return failures ? 1 : 0;
}
