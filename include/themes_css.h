#pragma once
// Themes written in CSS (docs/specs/theming.md, phase 1).
#include "app.h"
#include "css.h"

#include <memory>
#include <string>
#include <vector>

// A theme as a CSS sheet: how a built-in or themes.ini theme is @imported,
// and what a new CSS theme starts from. themes.ini's keys map one to one:
// background and text to body, h1-h6, link to a, code and codebackground
// to pre, inlinecode to code, and so on
//
// `design` (D2DTheme::design, for a section that has design=1) writes the
// theme as the built-in themes are written: it starts from one of them,
// Paper or Midnight when dark, with @import, and so takes their type
// sizes and rules; its colours are that sheet's variables, the lines and
// the quieter ink worked out from them as the built-in themes' were, and
// rules follow only for what no variable carries
std::string themeToCss(const D2DTheme& theme, bool design = false);
// The same as the text of a file of its own (themes\<name>.css, written
// for a themes.ini section): each face followed by the ones Tinta falls
// back to, so the HTML export's page finds a face on another system too
std::string themeToCssFile(const D2DTheme& theme, bool design = false);
// The hairline's colour in that design, worked out from the page and the
// ink: what --line is for a theme that names no colour for rules (hr=)
D2D1_COLOR_F themeDesignLine(const D2DTheme& theme);

// A CSS theme read into the theme Tinta draws with: it starts from Paper,
// or Midnight when dark, and takes what the sheet declares for the page,
// headings, links, code, quotes, marks, emphasis, the selection, rules and
// code tokens, with the type sizes. `out`'s strings are filled; the caller
// binds them. lightDark says the sheet follows Windows (color-scheme: light
// dark), so it is read once for each
struct CssTheme {
    std::shared_ptr<const css::Sheet> sheet;
    std::vector<css::Diagnostic> diagnostics;
    bool lightDark = false;
};
// `base` is what the sheet starts from in place of Paper or Midnight: the
// built-in themes are read from nothing
CssTheme compileCssTheme(const std::string& text, const std::string& file, bool dark,
                         const css::ImportSource& imports, CustomTheme& out, const D2DTheme* base = nullptr);
