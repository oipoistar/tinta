#include "settings.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    failures++;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    // Unique test files only: never read/write the user's real Tinta config.
    fs::path dir = fs::temp_directory_path() /
        (L"tinta config \u65E5\u672C-" + std::to_wstring(GetCurrentProcessId()) +
         L"-" + std::to_wstring(GetTickCount64()));
    std::error_code error;
    if (!fs::create_directory(dir, error)) {
        std::cerr << "Cannot create config test directory: " << error.message() << '\n';
        return 1;
    }

    check(configFilePathForShell(L"").empty(), "empty config path stays empty");
    std::wstring missing = (dir / L"missing.ini").wstring();
    check(configFilePathForShell(missing) == missing,
          "missing file retains the original path for shell error handling");
    check(!fs::exists(missing), "resolving a missing file does not create it");

    for (const wchar_t* name : {L"settings.ini", L"themes.ini", L"languages.ini"}) {
        fs::path path = dir / name;
        {
            std::ofstream file(path, std::ios::binary);
            file << "; config path test\n";
            check(file.good(), "test INI is written");
        }
        std::wstring resolved = configFilePathForShell(path.wstring());
        check(resolved.rfind(L"\\\\?\\", 0) != 0,
              "shell path has no extended DOS prefix");
        check(fs::equivalent(path, fs::path(resolved), error) && !error,
              "shell path identifies the same file, including spaces and Unicode");

        // The helper must not depend on how a caller spells an existing path.
        std::wstring ordinary = path.wstring();
        std::wstring extended = ordinary.rfind(L"\\\\", 0) == 0
            ? L"\\\\?\\UNC\\" + ordinary.substr(2) : L"\\\\?\\" + ordinary;
        check(configFilePathForShell(extended) == resolved,
              "extended DOS input resolves to the ordinary shell path");

        HANDLE shared = CreateFileW(path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(shared != INVALID_HANDLE_VALUE, "test reader opens the INI");
        if (shared != INVALID_HANDLE_VALUE) {
            check(configFilePathForShell(path.wstring()) == resolved,
                  "an already open INI can still be resolved");
            CloseHandle(shared);
        }

        std::ifstream file(fs::path(resolved), std::ios::binary);
        std::string line;
        std::getline(file, line);
        check(line == "; config path test", "resolution preserves INI contents");
        file.close();
        check(fs::remove(path, error) && !error, "resolution releases its file handle");
    }
    check(fs::remove(dir, error) && !error, "temporary config directory is removed");

    if (failures == 0) {
        std::cout << "Config path tests passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
