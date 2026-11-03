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
std::string themeToCss(const D2DTheme& theme);

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
CssTheme compileCssTheme(const std::string& text, const std::string& file, bool dark,
                         const css::ImportSource& imports, CustomTheme& out);
