#include "math_parser.h"

#include <iostream>

using namespace tinta_math;
namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
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
    std::cout << "Math parser: " << failures << " failures\n";
    return failures ? 1 : 0;
}
