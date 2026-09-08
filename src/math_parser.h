#pragma once

#include <memory>
#include <string>
#include <vector>

// Internal, platform-independent math syntax tree.
namespace tinta_math {

struct MNode;
using MNodePtr = std::shared_ptr<MNode>;

struct MNode {
    enum Kind {
        Row,     // kids in sequence
        Sym,     // text (variable, number, operator, mapped symbol)
        Frac,    // kids = {num, den}
        Script,  // kids = {base, sub-or-null, sup-or-null}
        Delim,   // \left..\right, kids = {content}
        Deco,    // kids = {content}; decoKind below
        Space,   // explicit spacing; scale in "space"
        Grid,    // cells by row; each cell is a complete expression
    } kind = Row;

    std::wstring text;
    std::vector<MNodePtr> kids;
    wchar_t open = 0, close = 0;   // Delim
    int decoKind = 0;              // 1 overline/bar 2 overrightarrow/vec 3 sqrt 4 hat
    bool roman = false;            // upright (functions, \mathrm, \text)
    bool bold = false;             // \mathbf
    float space = 0.0f;            // Space: width in em
    std::vector<std::vector<MNodePtr>> cells;
    std::wstring alignment;        // Grid: l/c/r per column (empty = centered)
    bool aligned = false;          // Grid: alternating right/left equation columns
    bool compact = false;            // Grid: smallmatrix / substack
};


MNodePtr parseLatex(const std::wstring& source);
float symbolSpacing(const std::wstring& text, bool roman);

} // namespace tinta_math
