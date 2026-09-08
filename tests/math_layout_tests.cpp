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
                LR"(\begin{aligned}a+b&=c\\x&=y+z\end{aligned})"}) {
                out << "<section>" << mathBoxSvg(mathParse(app, tex, 24, true), "#17212b", "Segoe UI") << "</section>";
            }
        }
        mathClearCache();
    }
    CoUninitialize();
    std::cout << "Math layout: " << failures << " failures\n";
    return failures ? 1 : 0;
}
