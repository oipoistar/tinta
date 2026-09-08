#pragma once
#include <string>

namespace tinta_math {
// Expand equation-local macros. No state is shared between equations/documents.
bool expandLatexMacros(const std::wstring& source, std::wstring& expanded);
bool isBuiltinMathCommand(const std::wstring& command);
}
