#pragma once
// The citation styles and locales Tinta ships (resources/csl,
// docs/specs/scholar.md): apa, chicago-author-date, ieee, nature,
// china-national-standard-gb-t-7714-2015-numeric, locales-en-US and
// locales-zh-CN.
#include <string>
#include <string_view>
#include <vector>

// The file's XML, unpacked from the executable; empty when it has none by
// that name (or the running program carries no resources, as tests may not)
std::string bundledCsl(std::string_view name);
std::vector<std::string> bundledCslNames();
