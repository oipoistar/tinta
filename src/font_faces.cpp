// Fonts a CSS theme brings with @font-face (font_faces.h).

#pragma optimize("s", on)

#include "font_faces.h"

#include <windows.h>
#include <dwrite_3.h>

#include <cwctype>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>

namespace fontfaces {

namespace {

struct Face {
    std::wstring name;                            // as its collection knows it
    IDWriteFontCollection* collection = nullptr;  // null: a face this PC has
    std::vector<std::wstring> files;
};

std::mutex lock;
std::map<std::wstring, Face> faces;  // by family, lower case
IDWriteFactory5* factory = nullptr;
IDWriteInMemoryFontFileLoader* memory = nullptr;

std::wstring lower(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)std::towlower(c);
    return s;
}

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)(len > 0 ? len : 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
    return out;
}

IDWriteFactory5* dwrite() {
    if (!factory) {
        IDWriteFactory* base = nullptr;
        if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                          reinterpret_cast<IUnknown**>(&base))) && base) {
            base->QueryInterface(__uuidof(IDWriteFactory5), reinterpret_cast<void**>(&factory));
            base->Release();
        }
    }
    return factory;
}

bool readFile(const std::wstring& path, std::string& data) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !data.empty();
}

// A TTF, OTF or TTC as it is on disk; a WOFF or WOFF2 unpacked into memory
IDWriteFontFile* fontFile(IDWriteFactory5* f, const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    const std::wstring ext = dot == std::wstring::npos ? std::wstring() : lower(path.substr(dot + 1));
    IDWriteFontFile* file = nullptr;
    if (ext != L"woff" && ext != L"woff2") {
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
        f->CreateFontFileReference(path.c_str(), nullptr, &file);
        return file;
    }
    std::string data;
    if (!readFile(path, data)) return nullptr;
    IDWriteFontFileStream* stream = nullptr;
    if (FAILED(f->UnpackFontFile(ext == L"woff" ? DWRITE_CONTAINER_TYPE_WOFF : DWRITE_CONTAINER_TYPE_WOFF2, data.data(),
                                 (UINT32)data.size(), &stream)) || !stream) {
        return nullptr;
    }
    UINT64 size = 0;
    const void* bytes = nullptr;
    void* context = nullptr;
    if (SUCCEEDED(stream->GetFileSize(&size)) && SUCCEEDED(stream->ReadFileFragment(&bytes, 0, size, &context))) {
        if (!memory && SUCCEEDED(f->CreateInMemoryFontFileLoader(&memory)) && memory) f->RegisterFontFileLoader(memory);
        // No owner: the loader keeps a copy of the bytes
        if (memory) memory->CreateInMemoryFontFileReference(f, bytes, (UINT32)size, nullptr, &file);
        stream->ReleaseFileFragment(context);
    }
    stream->Release();
    return file;
}

std::wstring firstFamily(IDWriteFontCollection* collection) {
    std::wstring out;
    IDWriteFontFamily* family = nullptr;
    IDWriteLocalizedStrings* names = nullptr;
    if (collection && collection->GetFontFamilyCount() > 0 && SUCCEEDED(collection->GetFontFamily(0, &family)) &&
        SUCCEEDED(family->GetFamilyNames(&names))) {
        UINT32 index = 0, length = 0;
        BOOL exists = FALSE;
        names->FindLocaleName(L"en-us", &index, &exists);
        if (!exists) index = 0;
        if (SUCCEEDED(names->GetStringLength(index, &length))) {
            out.resize(length + 1);
            if (SUCCEEDED(names->GetString(index, out.data(), length + 1))) out.resize(length);
            else out.clear();
        }
    }
    if (names) names->Release();
    if (family) family->Release();
    return out;
}

bool systemHas(IDWriteFactory5* f, const std::wstring& family) {
    IDWriteFontCollection* fonts = nullptr;
    if (FAILED(f->GetSystemFontCollection(&fonts)) || !fonts) return false;
    UINT32 index = 0;
    BOOL exists = FALSE;
    const bool found = SUCCEEDED(fonts->FindFamilyName(family.c_str(), &index, &exists)) && exists;
    fonts->Release();
    return found;
}

}  // namespace

void load(const std::vector<css::FontFace>& list, std::vector<css::Diagnostic>& problems) {
    std::lock_guard<std::mutex> guard(lock);
    if (list.empty()) return;
    IDWriteFactory5* f = dwrite();
    if (!f) {
        problems.push_back({css::Diagnostic::Warning, list[0].file, list[0].line, 0,
                            "`@font-face` needs Windows 10: the faces are skipped"});
        return;
    }
    // One collection per family: its regular, bold and italic files together
    std::map<std::wstring, std::vector<const css::FontFace*>> families;
    for (const auto& face : list) families[lower(wide(face.family))].push_back(&face);
    for (const auto& [key, group] : families) {
        IDWriteFontSetBuilder1* builder = nullptr;
        if (FAILED(f->CreateFontSetBuilder(&builder)) || !builder) continue;
        std::vector<std::wstring> files;
        std::wstring local;
        for (const css::FontFace* face : group) {
            // The first source that loads, as a browser takes it
            for (const auto& source : face->sources) {
                if (source.rfind("local:", 0) == 0) {
                    const std::wstring name = wide(source.substr(6));
                    if (systemHas(f, name)) {
                        if (local.empty()) local = name;
                        break;
                    }
                    continue;
                }
                const std::wstring path = wide(source);
                IDWriteFontFile* file = fontFile(f, path);
                const HRESULT added = file ? builder->AddFontFile(file) : E_FAIL;
                if (file) file->Release();
                if (SUCCEEDED(added)) {
                    files.push_back(path);
                    break;
                }
                const size_t slash = source.find_last_of("/\\");
                problems.push_back({css::Diagnostic::Warning, face->file, face->line, 0,
                                    "`" + (slash == std::string::npos ? source : source.substr(slash + 1)) +
                                        (file ? "` is not a font Tinta can read" : "` cannot be read")});
            }
        }
        Face loaded;
        if (!files.empty()) {
            IDWriteFontSet* set = nullptr;
            IDWriteFontCollection1* collection = nullptr;
            if (SUCCEEDED(builder->CreateFontSet(&set)) && set &&
                SUCCEEDED(f->CreateFontCollectionFromFontSet(set, &collection)) && collection) {
                loaded.collection = collection;
                loaded.name = firstFamily(collection);
                loaded.files = files;
            }
            if (set) set->Release();
        }
        builder->Release();
        if (!loaded.collection) {
            if (local.empty()) continue;
            loaded.name = local;
        } else if (loaded.name.empty()) {
            loaded.collection->Release();
            continue;
        }
        auto old = faces.find(key);
        if (old != faces.end() && old->second.collection) old->second.collection->Release();
        faces[key] = std::move(loaded);
    }
}

bool has(const std::wstring& family) {
    std::lock_guard<std::mutex> guard(lock);
    return faces.count(lower(family)) != 0;
}

const wchar_t* name(const wchar_t* family) {
    if (!family) return family;
    std::lock_guard<std::mutex> guard(lock);
    if (faces.empty()) return family;
    auto found = faces.find(lower(family));
    return found == faces.end() ? family : found->second.name.c_str();
}

IDWriteFontCollection* collection(const wchar_t* family) {
    if (!family) return nullptr;
    std::lock_guard<std::mutex> guard(lock);
    if (faces.empty()) return nullptr;
    auto found = faces.find(lower(family));
    return found == faces.end() ? nullptr : found->second.collection;
}

std::vector<std::wstring> files(const std::wstring& family) {
    std::lock_guard<std::mutex> guard(lock);
    auto found = faces.find(lower(family));
    return found == faces.end() ? std::vector<std::wstring>() : found->second.files;
}

}  // namespace fontfaces
