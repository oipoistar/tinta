#pragma once
// Fonts a CSS theme brings with @font-face (docs/specs/theming.md, phase 2):
// TTF, OTF, TTC, WOFF and WOFF2 files beside the sheet, loaded into
// DirectWrite collections of their own, and local() names of faces this PC
// has. Nothing is installed. A theme's family name then draws from its own
// files wherever Tinta makes a text format.
#include "css.h"

#include <dwrite.h>

#include <string>
#include <vector>

namespace fontfaces {

// Loads a sheet's faces, replacing any loaded before under the same family
// names; what cannot be read goes to `problems` ("fonts/X.ttf cannot be read")
void load(const std::vector<css::FontFace>& faces, std::vector<css::Diagnostic>& problems);
// Whether a family is one a theme brought
bool has(const std::wstring& family);
// The name and collection to make a format of `family` with: a theme's own
// face from its collection, else the name as given and the system's (null)
const wchar_t* name(const wchar_t* family);
IDWriteFontCollection* collection(const wchar_t* family);
// The files a family was loaded from, for an export to carry beside it
std::vector<std::wstring> files(const std::wstring& family);

}  // namespace fontfaces
