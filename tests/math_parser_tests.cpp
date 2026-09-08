#include "math_parser.h"

#include <iostream>

using namespace tinta_math;
namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
std::wstring textOf(const MNodePtr& node) {
    if (!node) return {};
    std::wstring result = node->text;
    for (const auto& child : node->kids) result += textOf(child);
    for (const auto& row : node->cells) for (const auto& cell : row) result += textOf(cell);
    return result;
}
}

int main() {
    const std::wstring example = LR"(\begin{bmatrix}1&2\\5&6\end{bmatrix}\longrightarrow 6,
        \qquad\begin{bmatrix}3&4\\7&8\end{bmatrix}\longrightarrow 8)";
    auto root = parseLatex(example);
    check(root != nullptr, "issue #190 example parses");
    if (root) {
        auto matrix = root->kids.front()->kids.front();
        check(matrix->kind == MNode::Grid && matrix->cells.size() == 2 &&
              matrix->cells[0].size() == 2 && matrix->cells[1][1]->kids[0]->text == L"6",
              "matrix preserves cell boundaries and values");
    }
    for (const auto* env : {L"matrix", L"pmatrix", L"bmatrix", L"Bmatrix", L"vmatrix",
                            L"Vmatrix", L"smallmatrix", L"cases", L"rcases", L"aligned", L"split"}) {
        check(parseLatex(L"\\begin{" + std::wstring(env) + L"}x&y\\\\z&w\\end{" + env + L"}") != nullptr,
              "supported grid environment parses");
    }
    check(parseLatex(LR"(\begin{array}{rl}x&y\\z&w\end{array})") != nullptr, "array alignment");
    check(parseLatex(LR"(\begin{gathered}x=1\\y=2\end{gathered})") != nullptr, "gathered");
    check(parseLatex(LR"(\begin{matrix}\frac{a}{b}&\begin{pmatrix}x\\y\end{pmatrix}\\z&\&\end{matrix})") != nullptr,
          "nested matrices, fractions and escaped ampersand");
    check(parseLatex(LR"(\begin{matrix}1&2\\\end{matrix})") != nullptr, "trailing row separator");
    for (const auto* bad : {LR"(\begin{matrix}1&2)", LR"(\begin{matrix}1\end{pmatrix})",
                           LR"(\begin{unknown}1\end{unknown})", LR"(a&b)", LR"(a\\b)",
                           LR"(\begin{matrix}{a&b}\end{matrix})", LR"(a})", LR"(\frac{1})",
                           LR"(\begin{array}{xx}a&b\end{array})", LR"(\end{matrix})"}) {
        check(!parseLatex(bad), "malformed/unsupported input falls back");
    }
    check(!parseLatex(std::wstring(200, L'{') + L"x" + std::wstring(200, L'}')), "nesting is bounded");
    check(!parseLatex(std::wstring(40000, L'x')), "input size is bounded");
    check(parseLatex(L"x % ignored command \\unknown\n + y") != nullptr, "TeX comments");
    check(textOf(parseLatex(LR"(\text{for all }x\text{ and }y)")) == L"for all x and y", "text spaces survive");
    check(textOf(parseLatex(LR"(\text{a \textbf{bold} word \& \{x\}})")) == L"a bold word & {x}", "nested text and escapes");
    check(textOf(parseLatex(LR"(\mathbb{RNCZ})")) == L"\u211D\u2115\u2102\u2124", "braced blackboard alphabet");
    check(textOf(parseLatex(LR"(\mathcal{BL}\mathfrak{CH})")) == L"\u212C\u2112\u212D\u210C", "script and fraktur exceptions");
    check(textOf(parseLatex(LR"(\epsilon\varepsilon\phi\varphi)")) == L"\u03F5\u03B5\u03D5\u03C6", "Greek variants remain distinct");
    for (const auto* tex : {
        LR"(\sqrt[3]{x+1})", LR"(\binom{n}{k}+\dbinom{n}{2})", LR"(\cfrac{1}{1+\cfrac{1}{x}})",
        LR"(\sum\limits_{i=1}^n x_i)", LR"(\int\nolimits_0^1 f(x))", LR"(\operatorname*{arg max}_{x}f(x))",
        LR"(\sum_{\substack{i>0\\j>0}}a_{ij})", LR"(\overset{!}{=}\underset{x}{y})",
        LR"(A\xrightarrow[n\to\infty]{f}B)", LR"(\underbrace{a+b}_{\text{sum}})",
        LR"(\widetilde{xy}+\dot{x}+\ddot{x}+\underline{y}+\boxed{x=2}+\cancel{x})",
        LR"(\mathbb{ABCabc123}\mathsf{XYZ}\mathtt{abc}\boldsymbol{\alpha})"}) {
        check(parseLatex(tex) != nullptr, "common extended notation parses");
    }
    for (const auto* tex : {LR"(\sqrt[3{x})", LR"(\text{unterminated)", LR"(x^2^3)",
                            LR"(\overset{a})", LR"(\limits x)", LR"(\overline)", LR"(\mathbf)"})
        check(!parseLatex(tex), "invalid extended notation fails cleanly");
    std::cout << "Math parser: " << failures << " failures\n";
    return failures ? 1 : 0;
}
