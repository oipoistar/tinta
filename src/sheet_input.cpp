// Sheets: the pointer, the keys and the text box (sheet_ui.h), and the
// commands they share with the tool bar and the menu.

// Sheets favour small code over fast: they are drawn by Direct2D and
// laid out by the document layout, which keep their own optimisation
#pragma optimize("s", on)

#include "sheet_ui.h"
#include "d2d_init.h"
#include "document.h"
#include "editor.h"
#include "file_utils.h"
#include "i18n.h"
#include "network_path.h"
#include "signals.h"
#include "tabs.h"
#include "utils.h"

#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>

using namespace sheet;
using namespace sheetui;

namespace {

bool keyDown(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }

std::wstring clipboardText(HWND hwnd) {
    std::wstring text;
    if (!openClipboard(hwnd)) return text;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(data))) {
            text = p;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

UINT cardsFormat() {
    static UINT format = RegisterClipboardFormatW(L"Tinta.SheetCards");
    return format;
}

void invalidate(App& app) {
    if (app.hwnd) InvalidateRect(app.hwnd, nullptr, FALSE);
}

std::wstring stemOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of(L'.');
    return dot == std::wstring::npos || dot == 0 ? name : name.substr(0, dot);
}

float snap(float v, float step = kGrid) { return std::round(v / step) * step; }

void selectOnly(SheetDoc& doc, const std::string& id) {
    doc.selected.clear();
    if (!id.empty()) doc.selected.push_back(id);
    doc.selectedEdge.clear();
}

}  // namespace

// --- The text box ---------------------------------------------------------

namespace sheetui {

float editFontSize(const SheetDoc& doc) { return doc.editing == SheetDoc::Edit::Card ? 15.0f : 14.0f; }

bool editRect(App& app, SheetDoc& doc, D2D1_RECT_F& rect, float& textScale) {
    float s = scale(app, doc);
    switch (doc.editing) {
        case SheetDoc::Edit::Card: {
            int i = nodeIndex(doc.model, doc.editId);
            if (i < 0) return false;
            D2D1_RECT_F r = screenRect(app, doc, boxOf(doc.model.nodes[i]));
            float pad = cardPadding() * s;
            rect = D2D1::RectF(r.left + pad, r.top + pad, r.right - pad, r.bottom - pad * 0.6f);
            textScale = s;
            return rect.right > rect.left && rect.bottom > rect.top;
        }
        case SheetDoc::Edit::EdgeLabel: {
            int e = edgeIndex(doc.model, doc.editId);
            if (e < 0) return false;
            Curve c = edgeCurveFor(doc, doc.model.edges[e]);
            D2D1_POINT_2F mid = bezier(c, 0.5f);
            D2D1_POINT_2F q = toScreen(app, doc, mid.x, mid.y);
            float w = dpi(app, 220.0f), h = editFontSize(doc) * app.contentScale * 1.4f;
            rect = D2D1::RectF(q.x - w * 0.5f, q.y - h * 0.5f, q.x + w * 0.5f, q.y + h * 0.5f);
            textScale = app.contentScale;
            return true;
        }
        case SheetDoc::Edit::GroupLabel:
        case SheetDoc::Edit::LaneKey: {
            int g = nodeIndex(doc.model, doc.editId);
            if (g < 0) return false;
            Box lb = groupLabelBox(app, doc, doc.model.nodes[g]);
            D2D1_POINT_2F q = toScreen(app, doc, lb.left, lb.top);
            float w = dpi(app, 240.0f), h = editFontSize(doc) * app.contentScale * 1.4f;
            if (doc.editing == SheetDoc::Edit::LaneKey) q.y -= h + dpi(app, 18.0f);
            rect = D2D1::RectF(q.x + dpi(app, 8.0f), q.y + dpi(app, 4.0f), q.x + dpi(app, 8.0f) + w, q.y + dpi(app, 4.0f) + h);
            textScale = app.contentScale;
            return true;
        }
        case SheetDoc::Edit::Url: {
            int i = nodeIndex(doc.model, doc.editId);
            if (i < 0) return false;
            D2D1_RECT_F r = screenRect(app, doc, boxOf(doc.model.nodes[i]));
            float h = editFontSize(doc) * app.contentScale * 1.4f;
            float w = std::max(dpi(app, 260.0f), r.right - r.left - dpi(app, 24.0f));
            rect = D2D1::RectF(r.left + dpi(app, 12.0f), (r.top + r.bottom) * 0.5f - h * 0.5f, r.left + dpi(app, 12.0f) + w,
                               (r.top + r.bottom) * 0.5f + h * 0.5f);
            textScale = app.contentScale;
            return true;
        }
        default:
            return false;
    }
}

IDWriteTextLayout* editLayout(App& app, SheetDoc& doc, float width) {
    // One layout at a time, rebuilt when the text, the width or the kind change
    static Microsoft::WRL::ComPtr<IDWriteTextLayout> cached;
    static std::wstring cachedText;
    static float cachedWidth = -1;
    static int cachedKind = -1;
    static std::wstring cachedFace;
    static Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> markBrush;
    static unsigned brushGeneration = 0;
    bool card = doc.editing == SheetDoc::Edit::Card;
    const wchar_t* face = card && app.theme.fontFamily && *app.theme.fontFamily ? app.theme.fontFamily : L"Segoe UI";
    if (cached && cachedText == doc.edit.text && std::fabs(cachedWidth - width) < 0.01f && cachedKind == (int)doc.editing &&
        cachedFace == face && brushGeneration == app.renderTargetGeneration) {
        return cached.Get();
    }
    cached.Reset();
    IDWriteTextFormat* fmt = nullptr;
    app.dwriteFactory->CreateTextFormat(face, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                        DWRITE_FONT_STRETCH_NORMAL, editFontSize(doc), L"en-us", &fmt);
    if (!fmt) return nullptr;
    useUiFontFallback(app, fmt);
    if (!card) fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(doc.edit.text.c_str(), (UINT32)doc.edit.text.size(), fmt, std::max(1.0f, width),
                                        100000.0f, &layout);
    fmt->Release();
    if (!layout) return nullptr;
    cached.Attach(layout);
    cachedText = doc.edit.text;
    cachedWidth = width;
    cachedKind = (int)doc.editing;
    cachedFace = face;
    if (card) {
        // Markdown, lightly: headings bold and larger, **strong**, *emphasis*,
        // `code` in the code face, and the marks themselves muted
        if (!markBrush || brushGeneration != app.renderTargetGeneration) {
            markBrush.Reset();
            app.renderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &markBrush);
        }
        brushGeneration = app.renderTargetGeneration;
        if (markBrush) {
            D2D1_COLOR_F muted = app.theme.text;
            muted.a = 0.45f;
            markBrush->SetColor(muted);
        }
        const std::wstring& t = doc.edit.text;
        const wchar_t* codeFace = app.theme.codeFontFamily && *app.theme.codeFontFamily ? app.theme.codeFontFamily : L"Consolas";
        size_t start = 0;
        bool inFence = false;
        while (start <= t.size()) {
            size_t end = t.find(L'\n', start);
            if (end == std::wstring::npos) end = t.size();
            std::wstring_view lineText(t.data() + start, end - start);
            size_t lead = lineText.find_first_not_of(L' ');
            bool fence = lead != std::wstring::npos && lineText.substr(lead, 3) == L"```";
            if (fence || inFence) {
                layout->SetFontFamilyName(codeFace, {(UINT32)start, (UINT32)(end - start)});
                if (fence) inFence = !inFence;
            } else {
                size_t hashes = 0;
                while (hashes < lineText.size() && lineText[hashes] == L'#') ++hashes;
                if (hashes >= 1 && hashes <= 6 && hashes < lineText.size() && lineText[hashes] == L' ') {
                    float sizes[] = {1.6f, 1.35f, 1.18f, 1.08f, 1.0f, 1.0f};
                    DWRITE_TEXT_RANGE range{(UINT32)start, (UINT32)(end - start)};
                    layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
                    layout->SetFontSize(editFontSize(doc) * sizes[hashes - 1], range);
                    if (markBrush) layout->SetDrawingEffect(markBrush.Get(), {(UINT32)start, (UINT32)hashes});
                }
                // Inline spans
                for (size_t i = 0; i < lineText.size(); ++i) {
                    wchar_t c = lineText[i];
                    if (c == L'`') {
                        size_t close = lineText.find(L'`', i + 1);
                        if (close == std::wstring::npos) break;
                        layout->SetFontFamilyName(codeFace, {(UINT32)(start + i), (UINT32)(close - i + 1)});
                        if (markBrush) {
                            layout->SetDrawingEffect(markBrush.Get(), {(UINT32)(start + i), 1});
                            layout->SetDrawingEffect(markBrush.Get(), {(UINT32)(start + close), 1});
                        }
                        i = close;
                    } else if (c == L'*' && i + 1 < lineText.size() && lineText[i + 1] == L'*') {
                        size_t close = lineText.find(L"**", i + 2);
                        if (close == std::wstring::npos) continue;
                        layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, {(UINT32)(start + i), (UINT32)(close - i + 2)});
                        if (markBrush) {
                            layout->SetDrawingEffect(markBrush.Get(), {(UINT32)(start + i), 2});
                            layout->SetDrawingEffect(markBrush.Get(), {(UINT32)(start + close), 2});
                        }
                        i = close + 1;
                    } else if ((c == L'*' || c == L'_') && i + 1 < lineText.size() && lineText[i + 1] != L' ' &&
                               (i == 0 || lineText[i - 1] == L' ')) {
                        size_t close = lineText.find(c, i + 1);
                        if (close == std::wstring::npos || close == i + 1) continue;
                        layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, {(UINT32)(start + i), (UINT32)(close - i + 1)});
                        if (markBrush) {
                            layout->SetDrawingEffect(markBrush.Get(), {(UINT32)(start + i), 1});
                            layout->SetDrawingEffect(markBrush.Get(), {(UINT32)(start + close), 1});
                        }
                        i = close;
                    }
                }
                // List and quote marks
                if (lead != std::wstring::npos && markBrush) {
                    std::wstring_view rest = lineText.substr(lead);
                    size_t mark = 0;
                    if (rest.size() >= 2 && (rest[0] == L'-' || rest[0] == L'*' || rest[0] == L'+' || rest[0] == L'>') &&
                        rest[1] == L' ') {
                        mark = 1;
                        if (rest.size() >= 6 && rest[2] == L'[' && rest[4] == L']') mark = 5;
                    }
                    if (mark) layout->SetDrawingEffect(markBrush.Get(), {(UINT32)(start + lead), (UINT32)mark});
                }
            }
            if (end == t.size()) break;
            start = end + 1;
        }
    }
    return cached.Get();
}

}  // namespace sheetui

namespace {

void pushUndo(SheetTextEdit& e, bool coalesce) {
    unsigned long long now = nowMs();
    if (coalesce && !e.undo.empty() && now - e.lastTypeMs < 900) {
        e.lastTypeMs = now;
        return;
    }
    e.undo.push_back({e.text, e.caret, e.anchor});
    if (e.undo.size() > 300) e.undo.erase(e.undo.begin());
    e.redo.clear();
    e.lastTypeMs = now;
}

void replaceSelection(SheetDoc& doc, const std::wstring& with, bool coalesce = false) {
    SheetTextEdit& e = doc.edit;
    pushUndo(e, coalesce);
    size_t a = e.selStart(), b = e.selEnd();
    std::wstring insert = with;
    if (!e.multiline) {
        for (auto& c : insert) {
            if (c == L'\n' || c == L'\r' || c == L'\t') c = L' ';
        }
    }
    e.text.replace(a, b - a, insert);
    e.caret = e.anchor = a + insert.size();
    e.changed = true;
    e.preferredX = -1;
    e.lastTypeMs = nowMs();
}

bool isWordChar(wchar_t c) { return iswalnum(c) || c == L'_' || c > 0x2E7F; }

size_t wordLeft(const std::wstring& t, size_t at) {
    while (at > 0 && !isWordChar(t[at - 1])) --at;
    while (at > 0 && isWordChar(t[at - 1])) --at;
    return at;
}

size_t wordRight(const std::wstring& t, size_t at) {
    while (at < t.size() && !isWordChar(t[at])) ++at;
    while (at < t.size() && isWordChar(t[at])) ++at;
    return at;
}

size_t lineStart(const std::wstring& t, size_t at) {
    size_t nl = at == 0 ? std::wstring::npos : t.rfind(L'\n', at - 1);
    return nl == std::wstring::npos ? 0 : nl + 1;
}

size_t lineEnd(const std::wstring& t, size_t at) {
    size_t nl = t.find(L'\n', at);
    return nl == std::wstring::npos ? t.size() : nl;
}

// The caret's place in the box's layout units
bool caretPoint(App& app, SheetDoc& doc, float& x, float& y, float& height) {
    D2D1_RECT_F rect{};
    float scale = 1;
    if (!editRect(app, doc, rect, scale)) return false;
    IDWriteTextLayout* layout = editLayout(app, doc, (rect.right - rect.left) / scale);
    if (!layout) return false;
    DWRITE_HIT_TEST_METRICS m{};
    layout->HitTestTextPosition((UINT32)doc.edit.caret, FALSE, &x, &y, &m);
    height = m.height;
    return true;
}

size_t positionAt(App& app, SheetDoc& doc, float lx, float ly) {
    D2D1_RECT_F rect{};
    float scale = 1;
    if (!editRect(app, doc, rect, scale)) return doc.edit.caret;
    IDWriteTextLayout* layout = editLayout(app, doc, (rect.right - rect.left) / scale);
    if (!layout) return doc.edit.caret;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS m{};
    layout->HitTestPoint(lx, ly, &trailing, &inside, &m);
    size_t at = m.textPosition + (trailing ? m.length : 0);
    return std::min(at, doc.edit.text.size());
}

// The card grows to hold what is typed into it, and the box keeps the caret in view
void fitEditor(App& app, SheetDoc& doc) {
    if (doc.editing != SheetDoc::Edit::Card) return;
    int i = nodeIndex(doc.model, doc.editId);
    if (i < 0) return;
    D2D1_RECT_F rect{};
    float scale = 1;
    if (!editRect(app, doc, rect, scale)) return;
    IDWriteTextLayout* layout = editLayout(app, doc, (rect.right - rect.left) / scale);
    if (!layout) return;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    Node& n = doc.model.nodes[i];
    float want = m.height + cardPadding() * 1.6f + 24.0f;
    if (want > n.height && n.height < 1600.0f) n.height = std::min(1600.0f, snap(want + kGrid * 0.5f));
    float x = 0, y = 0, h = 0;
    if (caretPoint(app, doc, x, y, h)) {
        float visible = (rect.bottom - rect.top) / scale;
        if (y < doc.edit.scrollY) doc.edit.scrollY = y;
        if (y + h > doc.edit.scrollY + visible) doc.edit.scrollY = y + h - visible;
        doc.edit.scrollY = std::max(0.0f, doc.edit.scrollY);
    }
}

std::string noteTextForWrite(const SheetDoc& doc, const std::wstring& text) {
    std::string utf8 = toUtf8(text);
    bool crlf = doc.editOriginal.find("\r\n") != std::string::npos;
    if (crlf) {
        std::string out;
        out.reserve(utf8.size() + utf8.size() / 20);
        for (char c : utf8) {
            if (c == '\n') out += '\r';
            out += c;
        }
        utf8 = out;
    }
    if (startsWithUtf8Bom(doc.editOriginal)) utf8.insert(0, "\xEF\xBB\xBF");
    return utf8;
}

std::wstring textForEdit(std::string text) {
    if (startsWithUtf8Bom(text)) text.erase(0, 3);
    std::wstring wide = toWide(text);
    std::wstring out;
    out.reserve(wide.size());
    for (wchar_t c : wide) {
        if (c != L'\r') out += c;
    }
    return out;
}

}  // namespace

namespace sheetui {

void startEdit(App& app, SheetDoc& doc, SheetDoc::Edit what, const std::string& id) {
    if (doc.readOnly && what != SheetDoc::Edit::Find) {
        hint(app, tr(app, "sheet.read_only_short"));
        return;
    }
    if (doc.editing != SheetDoc::Edit::None) commitEdit(app, doc);
    doc.edit = SheetTextEdit{};
    doc.editOriginal.clear();
    doc.editId = id;
    switch (what) {
        case SheetDoc::Edit::Card: {
            int i = nodeIndex(doc.model, id);
            if (i < 0) return;
            const Node& n = doc.model.nodes[i];
            SheetCard& c = card(app, doc, n);
            if (n.type == NodeType::Text) {
                doc.editOriginal = n.text;
            } else if (n.type == NodeType::File && c.kind == SheetCard::Kind::Note) {
                if (!readFileUtf8(c.path, doc.editOriginal)) return;
            } else {
                return;
            }
            doc.edit.text = textForEdit(doc.editOriginal);
            doc.edit.caret = doc.edit.anchor = doc.edit.text.size();
            doc.edit.multiline = true;
            // Readable while typing: a card seen from far comes close
            float s = scale(app, doc);
            if (s < 0.8f * app.contentScale) {
                Box b = boxOf(n);
                fitBox(app, doc, b, true, 1.0f);
            }
            break;
        }
        case SheetDoc::Edit::EdgeLabel: {
            int e = edgeIndex(doc.model, id);
            if (e < 0) return;
            doc.edit.text = toWide(doc.model.edges[e].label);
            break;
        }
        case SheetDoc::Edit::GroupLabel: {
            int g = nodeIndex(doc.model, id);
            if (g < 0) return;
            doc.edit.text = toWide(doc.model.nodes[g].label);
            break;
        }
        case SheetDoc::Edit::LaneKey: {
            int g = nodeIndex(doc.model, id);
            if (g < 0) return;
            doc.edit.text = toWide(doc.model.nodes[g].laneKey.empty() ? std::string("status") : doc.model.nodes[g].laneKey);
            break;
        }
        case SheetDoc::Edit::Url: {
            int i = nodeIndex(doc.model, id);
            if (i < 0) return;
            doc.edit.text = toWide(doc.model.nodes[i].url);
            break;
        }
        case SheetDoc::Edit::Find:
            break;
        default:
            return;
    }
    if (what != SheetDoc::Edit::Card) {
        doc.edit.multiline = false;
        doc.edit.anchor = 0;
        doc.edit.caret = doc.edit.text.size();
    }
    if (what != SheetDoc::Edit::Find) beginStep(doc);
    doc.editing = what;
    app.cursorBlinkOn = true;
    ensureTimer(app);
    invalidate(app);
}

void commitEdit(App& app, SheetDoc& doc, bool stay) {
    SheetDoc::Edit what = doc.editing;
    if (what == SheetDoc::Edit::None) return;
    bool changedModel = false;
    switch (what) {
        case SheetDoc::Edit::Card: {
            int i = nodeIndex(doc.model, doc.editId);
            if (i < 0) break;
            Node& n = doc.model.nodes[i];
            SheetCard& c = card(app, doc, n);
            if (n.type == NodeType::Text) {
                std::string text = toUtf8(doc.edit.text);
                if (text != n.text) {
                    n.text = text;
                    changedModel = true;
                }
                refreshCard(app, doc, n, true);
            } else if (doc.edit.changed || !stay) {
                std::string text = noteTextForWrite(doc, doc.edit.text);
                std::string current;
                readFileUtf8(c.path, current);
                if (text != current) {
                    if (writeFileUtf8(c.path, text)) {
                        // One file change per edit session: from what it held to what it holds now
                        bool merged = false;
                        for (auto& change : doc.pendingFiles) {
                            if (change.kind == SheetDoc::FileChange::Edited && change.path == c.path) {
                                change.after = text;
                                merged = true;
                            }
                        }
                        if (!merged) {
                            SheetDoc::FileChange change;
                            change.kind = SheetDoc::FileChange::Edited;
                            change.path = c.path;
                            change.before = current;
                            change.after = text;
                            doc.pendingFiles.push_back(change);
                        }
                    } else {
                        signalPush(app, SIG_ERROR, SIGI_WARNING, tr(app, "sheet.note_failed"));
                    }
                }
                refreshCard(app, doc, n, true);
                // A new note names itself after its first line when it is done
                if (!stay && c.createdHere && c.autoNamed && !doc.edit.text.empty()) {
                    std::string title = noteTitle(toUtf8(doc.edit.text), "");
                    if (!title.empty()) {
                        std::wstring stem = safeFileStem(toWide(title));
                        std::wstring dir = c.path.substr(0, c.path.find_last_of(L"\\/"));
                        if (_wcsicmp(stem.c_str(), stemOf(c.path).c_str()) != 0) {
                            std::wstring target;
                            for (int k = 1; k < 1000; ++k) {
                                target = dir + L"\\" + stem + (k == 1 ? L"" : L" " + std::to_wstring(k)) + L".md";
                                if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) break;
                            }
                            if (MoveFileW(c.path.c_str(), target.c_str())) {
                                SheetDoc::FileChange change;
                                change.kind = SheetDoc::FileChange::Renamed;
                                change.path = target;
                                change.otherPath = c.path;
                                doc.pendingFiles.push_back(change);
                                n.file = relativePath(doc, target);
                                c.autoNamed = false;
                                changedModel = true;
                                refreshCard(app, doc, n, true);
                            }
                        } else {
                            c.autoNamed = false;
                        }
                    }
                }
            }
            doc.edit.changed = false;
            c.bmKey = 0;
            break;
        }
        case SheetDoc::Edit::EdgeLabel: {
            int e = edgeIndex(doc.model, doc.editId);
            if (e >= 0 && doc.model.edges[e].label != toUtf8(doc.edit.text)) {
                doc.model.edges[e].label = toUtf8(doc.edit.text);
                changedModel = true;
            }
            break;
        }
        case SheetDoc::Edit::GroupLabel: {
            int g = nodeIndex(doc.model, doc.editId);
            if (g >= 0 && doc.model.nodes[g].label != toUtf8(doc.edit.text)) {
                doc.model.nodes[g].label = toUtf8(doc.edit.text);
                changedModel = true;
                // A lane's notes follow its new name
                if (!doc.model.nodes[g].laneKey.empty()) {
                    std::vector<std::string> ids;
                    for (int i = 0; i < (int)doc.model.nodes.size(); ++i) {
                        if (groupOf(doc.model, i) == g) ids.push_back(doc.model.nodes[i].id);
                    }
                    applyLanes(app, doc, ids);
                }
            }
            break;
        }
        case SheetDoc::Edit::LaneKey: {
            int g = nodeIndex(doc.model, doc.editId);
            std::string key = toUtf8(doc.edit.text);
            while (!key.empty() && (key.back() == ' ' || key.back() == ':')) key.pop_back();
            while (!key.empty() && key.front() == ' ') key.erase(key.begin());
            if (g >= 0 && doc.model.nodes[g].laneKey != key) {
                doc.model.nodes[g].laneKey = key;
                changedModel = true;
                std::vector<std::string> ids;
                for (int i = 0; i < (int)doc.model.nodes.size(); ++i) {
                    if (groupOf(doc.model, i) == g) ids.push_back(doc.model.nodes[i].id);
                }
                applyLanes(app, doc, ids);
            }
            break;
        }
        case SheetDoc::Edit::Url: {
            int i = nodeIndex(doc.model, doc.editId);
            std::string url = toUtf8(doc.edit.text);
            if (i >= 0 && doc.model.nodes[i].url != url) {
                doc.model.nodes[i].url = url;
                changedModel = true;
                refreshCard(app, doc, doc.model.nodes[i], true);
            }
            break;
        }
        default:
            break;
    }
    if (stay) {
        if (changedModel) markChanged(app, doc);
        invalidate(app);
        return;
    }
    doc.editing = SheetDoc::Edit::None;
    if (what == SheetDoc::Edit::Find) {
        invalidate(app);
        return;
    }
    // An edit that changed nothing leaves no undo step behind
    bool anything = changedModel || !doc.pendingFiles.empty();
    if (!anything && !doc.undo.empty()) {
        std::string before = writeCanvas(doc.undo.back().model), now = writeCanvas(doc.model);
        anything = before != now;
    }
    if (anything) {
        commitStep(app, doc);
    } else if (!doc.undo.empty()) {
        doc.undo.pop_back();
    }
    invalidate(app);
}

void cancelEdit(App& app, SheetDoc& doc) {
    if (doc.editing == SheetDoc::Edit::None) return;
    if (doc.editing == SheetDoc::Edit::Card) {
        commitEdit(app, doc);  // what was typed into a note is kept
        return;
    }
    bool step = doc.editing != SheetDoc::Edit::Find;
    doc.editing = SheetDoc::Edit::None;
    if (step && !doc.undo.empty()) {
        doc.model = doc.undo.back().model;
        doc.undo.pop_back();
    }
    invalidate(app);
}

}  // namespace sheetui

namespace {

// List items continue on Enter; an empty item ends the list
void newlineWithList(SheetDoc& doc) {
    SheetTextEdit& e = doc.edit;
    size_t ls = lineStart(e.text, e.selStart());
    std::wstring line = e.text.substr(ls, e.selStart() - ls);
    size_t indent = line.find_first_not_of(L' ');
    if (indent == std::wstring::npos) indent = line.size();
    std::wstring rest = line.substr(indent);
    std::wstring marker;
    if (rest.size() >= 6 && (rest[0] == L'-' || rest[0] == L'*' || rest[0] == L'+') && rest[1] == L' ' && rest[2] == L'[' &&
        rest[4] == L']' && rest[5] == L' ') {
        marker = std::wstring(1, rest[0]) + L" [ ] ";
    } else if (rest.size() >= 2 && (rest[0] == L'-' || rest[0] == L'*' || rest[0] == L'+' || rest[0] == L'>') && rest[1] == L' ') {
        marker = rest.substr(0, 2);
    } else {
        size_t digits = 0;
        while (digits < rest.size() && iswdigit(rest[digits])) ++digits;
        if (digits > 0 && digits + 1 < rest.size() && (rest[digits] == L'.' || rest[digits] == L')') && rest[digits + 1] == L' ') {
            marker = std::to_wstring(std::stoi(rest.substr(0, digits)) + 1) + rest[digits] + L" ";
        }
    }
    if (!marker.empty()) {
        // The item is only its marker: end the list instead
        std::wstring bare = rest;
        while (!bare.empty() && bare.back() == L' ') bare.pop_back();
        std::wstring markTrim = marker;
        while (!markTrim.empty() && markTrim.back() == L' ') markTrim.pop_back();
        bool emptyItem = bare == markTrim || (markTrim.size() > 1 && bare.size() >= 1 && rest.size() <= marker.size() &&
                                              bare.find_first_not_of(L"-*+>[] ") == std::wstring::npos);
        if (emptyItem && e.selStart() == lineEnd(e.text, ls)) {
            e.anchor = ls;
            e.caret = e.selEnd();
            replaceSelection(doc, L"");
            return;
        }
    }
    replaceSelection(doc, L"\n" + std::wstring(indent, L' ') + marker);
}

void wrapSelection(SheetDoc& doc, const std::wstring& mark) {
    SheetTextEdit& e = doc.edit;
    size_t a = e.selStart(), b = e.selEnd();
    std::wstring inner = e.text.substr(a, b - a);
    replaceSelection(doc, mark + inner + mark);
    if (inner.empty()) e.caret = e.anchor = a + mark.size();
    else {
        e.anchor = a + mark.size();
        e.caret = e.anchor + inner.size();
    }
}

void indentLines(SheetDoc& doc, bool out) {
    SheetTextEdit& e = doc.edit;
    size_t a = lineStart(e.text, e.selStart());
    size_t b = lineEnd(e.text, e.selEnd());
    std::wstring block = e.text.substr(a, b - a);
    std::wstring result;
    size_t at = 0;
    int delta = 0;
    while (at <= block.size()) {
        size_t nl = block.find(L'\n', at);
        std::wstring line = block.substr(at, nl == std::wstring::npos ? std::wstring::npos : nl - at);
        if (out) {
            size_t spaces = 0;
            while (spaces < 2 && spaces < line.size() && line[spaces] == L' ') ++spaces;
            line.erase(0, spaces);
            delta -= (int)spaces;
        } else {
            line.insert(0, L"  ");
            delta += 2;
        }
        result += line;
        if (nl == std::wstring::npos) break;
        result += L'\n';
        at = nl + 1;
    }
    pushUndo(e, false);
    e.text.replace(a, b - a, result);
    e.anchor = a;
    e.caret = a + result.size();
    e.changed = true;
    (void)delta;
}

bool editKey(App& app, HWND hwnd, SheetDoc& doc, WPARAM key) {
    SheetTextEdit& e = doc.edit;
    bool ctrl = keyDown(VK_CONTROL), shift = keyDown(VK_SHIFT);
    bool card = doc.editing == SheetDoc::Edit::Card;
    auto move = [&](size_t to) {
        e.caret = std::min(to, e.text.size());
        if (!shift) e.anchor = e.caret;
    };
    switch (key) {
        case VK_ESCAPE:
            if (doc.editing == SheetDoc::Edit::Find) {
                doc.finding = false;
                doc.found.clear();
                cancelEdit(app, doc);
            } else if (card) {
                commitEdit(app, doc);
            } else {
                cancelEdit(app, doc);
            }
            return true;
        case VK_RETURN:
            if (doc.editing == SheetDoc::Edit::Find) {
                if (!doc.found.empty()) {
                    doc.foundIndex = (doc.foundIndex + (shift ? -1 : 1) + (int)doc.found.size()) % (int)doc.found.size();
                    int i = nodeIndex(doc.model, doc.found[doc.foundIndex]);
                    if (i >= 0) {
                        selectOnly(doc, doc.model.nodes[i].id);
                        fitBox(app, doc, boxOf(doc.model.nodes[i]), true, 1.0f);
                    }
                }
                return true;
            }
            if (!card || ctrl) {
                commitEdit(app, doc);
                return true;
            }
            newlineWithList(doc);
            break;
        case VK_TAB:
            if (!card) return true;
            if (e.hasSelection() || shift || e.text.substr(lineStart(e.text, e.caret), 2).find_first_of(L"-*+0123456789") == 0) {
                indentLines(doc, shift);
            } else {
                replaceSelection(doc, L"  ");
            }
            break;
        case VK_BACK:
            if (!e.hasSelection()) {
                if (e.caret == 0) return true;
                e.anchor = ctrl ? wordLeft(e.text, e.caret) : e.caret - 1;
                // A surrogate pair goes as one
                if (!ctrl && e.anchor > 0 && IS_LOW_SURROGATE(e.text[e.anchor]) && IS_HIGH_SURROGATE(e.text[e.anchor - 1])) --e.anchor;
            }
            replaceSelection(doc, L"", !ctrl);
            break;
        case VK_DELETE:
            if (!e.hasSelection()) {
                if (e.caret >= e.text.size()) return true;
                e.anchor = ctrl ? wordRight(e.text, e.caret) : e.caret + 1;
                if (!ctrl && e.anchor < e.text.size() && IS_LOW_SURROGATE(e.text[e.anchor])) ++e.anchor;
            }
            replaceSelection(doc, L"", !ctrl);
            break;
        case VK_LEFT:
            if (!shift && e.hasSelection()) move(e.selStart());
            else move(ctrl ? wordLeft(e.text, e.caret) : (e.caret > 0 ? e.caret - 1 : 0));
            e.preferredX = -1;
            break;
        case VK_RIGHT:
            if (!shift && e.hasSelection()) move(e.selEnd());
            else move(ctrl ? wordRight(e.text, e.caret) : e.caret + 1);
            e.preferredX = -1;
            break;
        case VK_HOME:
            move(ctrl ? 0 : lineStart(e.text, e.caret));
            break;
        case VK_END:
            move(ctrl ? e.text.size() : lineEnd(e.text, e.caret));
            break;
        case VK_UP:
        case VK_DOWN: {
            if (!card) return true;
            float x = 0, y = 0, h = 0;
            if (!caretPoint(app, doc, x, y, h)) break;
            if (e.preferredX < 0) e.preferredX = x;
            float target = key == VK_UP ? y - h * 0.5f : y + h * 1.5f;
            if (target < 0) {
                move(0);
            } else {
                move(positionAt(app, doc, e.preferredX, target));
            }
            break;
        }
        case 'A':
            if (!ctrl) return false;
            e.anchor = 0;
            e.caret = e.text.size();
            break;
        case 'C':
        case 'X':
            if (!ctrl) return false;
            if (e.hasSelection()) {
                copyToClipboard(hwnd, e.text.substr(e.selStart(), e.selEnd() - e.selStart()));
                if (key == 'X') replaceSelection(doc, L"");
            }
            break;
        case 'V':
            if (!ctrl) return false;
            {
                std::wstring paste = clipboardText(hwnd);
                std::wstring clean;
                for (wchar_t c : paste) {
                    if (c != L'\r') clean += c;
                }
                if (!clean.empty()) replaceSelection(doc, clean);
            }
            break;
        case 'Z':
        case 'Y': {
            if (!ctrl) return false;
            bool redo = key == 'Y' || shift;
            auto& from = redo ? e.redo : e.undo;
            auto& to = redo ? e.undo : e.redo;
            if (from.empty()) break;
            to.push_back({e.text, e.caret, e.anchor});
            e.text = from.back().text;
            e.caret = from.back().caret;
            e.anchor = from.back().anchor;
            from.pop_back();
            e.changed = true;
            e.lastTypeMs = 0;
            break;
        }
        case 'B':
            if (!ctrl || !card) return false;
            wrapSelection(doc, L"**");
            break;
        case 'I':
            if (!ctrl || !card) return false;
            wrapSelection(doc, L"*");
            break;
        default:
            // Typing keys come back as characters
            return false;
    }
    if (doc.editing == SheetDoc::Edit::Find) updateFind(app, doc);
    fitEditor(app, doc);
    app.cursorBlinkOn = true;
    invalidate(app);
    return true;
}

}  // namespace

// --- Commands --------------------------------------------------------------

namespace {

// Nodes that move with a selection: the selection and what its groups hold
std::vector<int> movingSet(const SheetDoc& doc) {
    std::set<int> out;
    for (int i : selectedIndexes(doc)) {
        out.insert(i);
        if (doc.model.nodes[i].type == NodeType::Group) {
            for (int j = 0; j < (int)doc.model.nodes.size(); ++j) {
                if (insideGroup(doc.model.nodes[i], doc.model.nodes[j])) out.insert(j);
            }
        }
    }
    return std::vector<int>(out.begin(), out.end());
}

D2D1_POINT_2F viewCentre(App& app, SheetDoc& doc) {
    D2D1_RECT_F v = viewRect(app);
    return toSheet(app, doc, (v.left + v.right) * 0.5f, (v.top + v.bottom) * 0.5f);
}

// A free spot for a new card near a point: the first place in a widening
// ring that no card covers
D2D1_POINT_2F freeSpot(const SheetDoc& doc, float x, float y, float w, float h) {
    auto clear = [&](float px, float py) {
        for (const Node& n : doc.model.nodes) {
            if (n.type == NodeType::Group) continue;
            if (px < n.x + n.width + 20 && n.x < px + w + 20 && py < n.y + n.height + 20 && n.y < py + h + 20) return false;
        }
        return true;
    };
    for (int ring = 0; ring < 12; ++ring) {
        for (int k = 0; k < std::max(1, ring * 8); ++k) {
            float angle = ring == 0 ? 0 : (float)k / (ring * 8) * 6.2831853f;
            float px = snap(x + std::cos(angle) * ring * (w * 0.6f + 20)), py = snap(y + std::sin(angle) * ring * (h * 0.6f + 20));
            if (clear(px, py)) return D2D1::Point2F(px, py);
        }
    }
    return D2D1::Point2F(snap(x), snap(y));
}

void addEdge(SheetDoc& doc, const std::string& from, const std::string& to, const std::string& label = {}) {
    if (from == to || from.empty() || to.empty()) return;
    for (const Edge& e : doc.model.edges) {
        if (e.fromNode == from && e.toNode == to) return;
    }
    Edge e;
    e.id = newId();
    e.fromNode = from;
    e.toNode = to;
    e.label = label;
    doc.model.edges.push_back(e);
}

void newCardAndEdit(App& app, SheetDoc& doc, float x, float y, const std::string& connectFrom = {}) {
    beginStep(doc);
    std::string id = newNoteCard(app, doc, x, y);
    if (id.empty()) {
        doc.undo.pop_back();
        return;
    }
    if (!connectFrom.empty()) addEdge(doc, connectFrom, id);
    commitStep(app, doc);
    selectOnly(doc, id);
    startEdit(app, doc, SheetDoc::Edit::Card, id);
}

// Mind-map growth from the selected card: a child to its right below its
// other children, or a sibling below it under the same parent
void growFromSelection(App& app, SheetDoc& doc, bool sibling) {
    if (doc.selected.size() != 1) {
        D2D1_POINT_2F c = viewCentre(app, doc);
        D2D1_POINT_2F spot = freeSpot(doc, c.x - 180, c.y - 120, 360, 240);
        newCardAndEdit(app, doc, spot.x, spot.y);
        return;
    }
    int i = nodeIndex(doc.model, doc.selected[0]);
    if (i < 0 || doc.model.nodes[i].type == NodeType::Group) return;
    const Node from = doc.model.nodes[i];
    std::string parent;
    if (sibling) {
        for (const Edge& e : doc.model.edges) {
            if (e.toNode == from.id) {
                parent = e.fromNode;
                break;
            }
        }
    }
    float w = 300, h = 160;
    float x, y;
    if (sibling) {
        x = from.x;
        y = from.y + from.height + 40;
    } else {
        x = from.x + from.width + 100;
        y = from.y;
        for (const Edge& e : doc.model.edges) {
            if (e.fromNode != from.id) continue;
            int c = nodeIndex(doc.model, e.toNode);
            if (c >= 0 && doc.model.nodes[c].x > from.x) y = std::max(y, doc.model.nodes[c].y + doc.model.nodes[c].height + 40);
        }
    }
    D2D1_POINT_2F spot = freeSpot(doc, x, y, w, h);
    beginStep(doc);
    std::string id = newNoteCard(app, doc, spot.x, spot.y, {}, w, h);
    if (id.empty()) {
        doc.undo.pop_back();
        return;
    }
    addEdge(doc, sibling ? parent : from.id, id);
    commitStep(app, doc);
    selectOnly(doc, id);
    // Keep the new card in view
    D2D1_RECT_F v = viewRect(app);
    D2D1_RECT_F r = screenRect(app, doc, {spot.x, spot.y, spot.x + w, spot.y + h});
    if (r.left < v.left || r.right > v.right || r.top < v.top || r.bottom > v.bottom) {
        flyTo(doc, spot.x + w * 0.5f, spot.y + h * 0.5f, doc.zoom, 300);
    }
    startEdit(app, doc, SheetDoc::Edit::Card, id);
}

void groupSelection(App& app, SheetDoc& doc) {
    std::vector<int> picked = selectedIndexes(doc);
    if (picked.empty()) return;
    Box b = bounds(doc.model, picked);
    beginStep(doc);
    Node g;
    g.id = newId();
    g.type = NodeType::Group;
    g.label = toUtf8(tr(app, "sheet.group_default"));
    g.x = snap(b.left - 40);
    g.y = snap(b.top - 80);
    g.width = snap(b.right - b.left + 80);
    g.height = snap(b.bottom - b.top + 120);
    // Behind everything it holds
    int at = (int)doc.model.nodes.size();
    for (int i : picked) at = std::min(at, i);
    doc.model.nodes.insert(doc.model.nodes.begin() + at, g);
    commitStep(app, doc);
    selectOnly(doc, g.id);
    startEdit(app, doc, SheetDoc::Edit::GroupLabel, g.id);
}

void removeSelection(App& app, SheetDoc& doc) {
    if (!doc.selectedEdge.empty()) {
        beginStep(doc);
        doc.model.edges.erase(std::remove_if(doc.model.edges.begin(), doc.model.edges.end(),
                                             [&](const Edge& e) { return e.id == doc.selectedEdge; }),
                              doc.model.edges.end());
        doc.selectedEdge.clear();
        commitStep(app, doc);
        return;
    }
    if (doc.selected.empty()) return;
    beginStep(doc);
    for (const std::string& id : doc.selected) {
        int i = nodeIndex(doc.model, id);
        if (i < 0) continue;
        // A note made here and never written to goes with its card
        auto it = doc.cards.find(id);
        if (it != doc.cards.end() && it->second.createdHere && it->second.kind == SheetCard::Kind::Note) {
            std::string text;
            if (readFileUtf8(it->second.path, text) && text.find_first_not_of(" \t\r\n") == std::string::npos &&
                DeleteFileW(it->second.path.c_str())) {
                SheetDoc::FileChange change;
                change.kind = SheetDoc::FileChange::Deleted;  // undoing the removal brings it back
                change.path = it->second.path;
                change.before = text;
                doc.pendingFiles.push_back(change);
            }
        }
        removeNode(doc.model, id);
    }
    doc.selected.clear();
    commitStep(app, doc);
    pollFolder(app, doc, true);
}

void duplicateSelection(App& app, SheetDoc& doc, float dx, float dy, bool select = true) {
    std::vector<int> picked = movingSet(doc);
    if (picked.empty()) return;
    std::map<std::string, std::string> idMap;
    std::vector<Node> made;
    for (int i : picked) {
        Node n = doc.model.nodes[i];
        std::string old = n.id;
        n.id = newId();
        n.x += dx;
        n.y += dy;
        if (n.type == NodeType::File) {
            auto it = doc.cards.find(old);
            if (it != doc.cards.end() && it->second.kind == SheetCard::Kind::Note) {
                std::string text;
                readFileUtf8(it->second.path, text);
                std::string rel = writeNoteFile(app, doc, stemOf(it->second.path), text);
                if (!rel.empty()) n.file = rel;
            }
        }
        idMap[old] = n.id;
        made.push_back(n);
    }
    std::vector<Edge> edges;
    for (const Edge& e : doc.model.edges) {
        auto f = idMap.find(e.fromNode), t = idMap.find(e.toNode);
        if (f == idMap.end() || t == idMap.end()) continue;
        Edge copy = e;
        copy.id = newId();
        copy.fromNode = f->second;
        copy.toNode = t->second;
        edges.push_back(copy);
    }
    // Groups go behind, cards on top
    for (const Node& n : made) {
        if (n.type == NodeType::Group) doc.model.nodes.insert(doc.model.nodes.begin(), n);
        else doc.model.nodes.push_back(n);
    }
    doc.model.edges.insert(doc.model.edges.end(), edges.begin(), edges.end());
    for (const Node& n : made) refreshCard(app, doc, n, true);
    if (select) {
        doc.selected.clear();
        for (const auto& entry : idMap) doc.selected.push_back(entry.second);
    }
}

std::string compileMarkdown(App& app, SheetDoc& doc, const std::vector<int>& subset, const std::wstring& outDir) {
    std::vector<int> order = readingOrder(doc.model, subset);
    std::string out = "# " + doc.info.title + "\n\n";
    int currentGroup = -2;
    auto topGroup = [&](int i) {
        int g = groupOf(doc.model, i), top = -1;
        while (g >= 0) {
            top = g;
            g = groupOf(doc.model, g);
        }
        return top;
    };
    for (int i : order) {
        const Node& n = doc.model.nodes[i];
        int g = topGroup(i);
        if (g != currentGroup) {
            if (g >= 0) out += "## " + (doc.model.nodes[g].label.empty() ? std::string("\xE2\x80\x94") : doc.model.nodes[g].label) + "\n\n";
            currentGroup = g;
        }
        int level = g >= 0 ? 3 : 2;
        SheetCard& c = card(app, doc, n);
        if (c.kind == SheetCard::Kind::Note) {
            std::string body(noteBody(c.source));
            // Links and images in the note keep pointing where they did
            std::wstring noteDir = n.type == NodeType::File ? c.path.substr(0, c.path.find_last_of(L"\\/")) : doc.dir;
            std::string prefix;
            if (_wcsicmp(noteDir.c_str(), outDir.c_str()) != 0) {
                std::filesystem::path rel = std::filesystem::path(noteDir).lexically_relative(outDir);
                std::wstring r = rel.wstring();
                for (auto& ch : r) {
                    if (ch == L'\\') ch = L'/';
                }
                std::string encoded;
                for (char ch : toUtf8(r)) encoded += ch == ' ' ? std::string("%20") : std::string(1, ch);
                if (!encoded.empty()) prefix = encoded + "/";
            }
            if (!prefix.empty()) {
                std::string fixed;
                for (size_t k = 0; k < body.size(); ++k) {
                    fixed += body[k];
                    if (body[k] == ']' && k + 1 < body.size() && body[k + 1] == '(') {
                        size_t start = k + 2;
                        std::string dest = body.substr(start, body.find(')', start) - start);
                        bool relative = !dest.empty() && dest[0] != '#' && dest[0] != '/' && dest.find("://") == std::string::npos &&
                                        dest.rfind("mailto:", 0) != 0 && !(dest.size() > 1 && dest[1] == ':');
                        if (relative) {
                            // Copy the '(' (and a '<') ourselves, then the prefix
                            fixed += '(';
                            ++k;
                            if (dest[0] == '<') {
                                fixed += '<';
                                ++k;
                            }
                            fixed += prefix;
                        }
                    }
                }
                body = fixed;
            }
            // Its headings sit under the section; a note without one gets its title
            int top = 7;
            bool firstIsHeading = false;
            {
                size_t at = 0;
                bool seenText = false;
                while (at < body.size()) {
                    size_t nl = body.find('\n', at);
                    std::string line = body.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
                    size_t h = 0;
                    while (h < line.size() && line[h] == '#') ++h;
                    if (h >= 1 && h <= 6 && h < line.size() && line[h] == ' ') {
                        top = std::min(top, (int)h);
                        if (!seenText) firstIsHeading = true;
                    }
                    if (line.find_first_not_of(" \t\r") != std::string::npos) seenText = true;
                    if (nl == std::string::npos) break;
                    at = nl + 1;
                }
            }
            if (firstIsHeading) {
                out += shiftHeadings(body, level - top);
            } else {
                out += std::string(level, '#') + " " + c.title + "\n\n" + shiftHeadings(body, top <= 6 ? level + 1 - top : 0);
            }
        } else if (c.kind == SheetCard::Kind::Image) {
            std::filesystem::path rel = std::filesystem::path(c.path).lexically_relative(outDir);
            std::string r = toUtf8(rel.wstring());
            for (auto& ch : r) {
                if (ch == '\\') ch = '/';
            }
            out += "![" + c.title + "](<" + r + ">)";
        } else if (n.type == NodeType::Link) {
            out += "<" + n.url + ">";
        } else if (n.type == NodeType::File) {
            out += "- " + c.title;
        }
        while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
        out += "\n\n";
    }
    return out;
}

void compileSheet(App& app, HWND hwnd, SheetDoc& doc) {
    std::vector<int> subset = selectedIndexes(doc);
    if (subset.size() < 2) subset.clear();
    std::wstring dir = doc.cardDir;
    std::string text = compileMarkdown(app, doc, subset, dir);
    std::wstring path = dir + L"\\" + safeFileStem(toWide(doc.info.title)) + L".compiled.md";
    CreateDirectoryW(dir.c_str(), nullptr);
    if (!writeFileUtf8(path, text)) {
        signalPush(app, SIG_ERROR, SIGI_WARNING, tr(app, "sheet.note_failed"));
        return;
    }
    sheetLeave(app);
    tabOpenPath(app, hwnd, toUtf8(path));
    signalPush(app, SIG_SUCCESS, SIGI_FILE, tr(app, "sheet.compiled"), path.substr(path.find_last_of(L"\\/") + 1));
}

void copyCards(App& app, HWND hwnd, SheetDoc& doc) {
    std::vector<int> picked = movingSet(doc);
    if (picked.empty()) return;
    Sheet part;
    std::set<std::string> ids;
    for (int i : picked) {
        Node n = doc.model.nodes[i];
        if (n.type == NodeType::File) n.file = toUtf8(absolutePath(doc, n.file));
        part.nodes.push_back(n);
        ids.insert(n.id);
    }
    for (const Edge& e : doc.model.edges) {
        if (ids.count(e.fromNode) && ids.count(e.toNode)) part.edges.push_back(e);
    }
    std::string json = writeCanvas(part);
    // Plain text for other programs: the cards as a list
    std::wstring text;
    for (int i : readingOrder(doc.model, picked)) {
        text += L"- " + toWide(cardTitle(app, doc, doc.model.nodes[i])) + L"\r\n";
    }
    if (!openClipboard(hwnd)) return;
    EmptyClipboard();
    auto put = [](UINT format, const void* data, size_t bytes) {
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!mem) return;
        if (void* p = GlobalLock(mem)) {
            memcpy(p, data, bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(format, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    };
    put(cardsFormat(), json.c_str(), json.size() + 1);
    put(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    CloseClipboard();
    signalHint(app, SIGI_COPY, tr(app, "toast.copied"));
}

void placeAt(App& app, SheetDoc& doc, Sheet part, float x, float y) {
    if (part.nodes.empty()) return;
    Box b = bounds(part);
    float dx = snap(x - (b.left + b.right) * 0.5f), dy = snap(y - (b.top + b.bottom) * 0.5f);
    std::map<std::string, std::string> idMap;
    doc.selected.clear();
    for (Node& n : part.nodes) {
        std::string old = n.id;
        n.id = newId();
        idMap[old] = n.id;
        n.x += dx;
        n.y += dy;
        if (n.type == NodeType::File) n.file = relativePath(doc, absolutePath(doc, n.file));
        doc.selected.push_back(n.id);
    }
    for (Edge& e : part.edges) {
        e.id = newId();
        e.fromNode = idMap[e.fromNode];
        e.toNode = idMap[e.toNode];
    }
    for (Node& n : part.nodes) {
        if (n.type == NodeType::Group) doc.model.nodes.insert(doc.model.nodes.begin(), n);
        else doc.model.nodes.push_back(n);
    }
    doc.model.edges.insert(doc.model.edges.end(), part.edges.begin(), part.edges.end());
    for (const Node& n : part.nodes) refreshCard(app, doc, n, true);
}

void addFileCards(App& app, SheetDoc& doc, const std::vector<std::wstring>& files, float x, float y) {
    std::vector<std::wstring> usable;
    for (const auto& f : files) {
        if (!qmd::mayOpenFromDocument(f, doc.dir)) continue;
        DWORD a = GetFileAttributesW(f.c_str());
        if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) continue;
        usable.push_back(f);
    }
    if (usable.empty()) return;
    beginStep(doc);
    auto places = gridPlaces((int)usable.size(), snap(x), snap(y), 360, 240, 40);
    doc.selected.clear();
    for (size_t k = 0; k < usable.size(); ++k) {
        Node n;
        n.id = newId();
        n.type = NodeType::File;
        n.file = relativePath(doc, usable[k]);
        n.x = places[k].first;
        n.y = places[k].second;
        n.width = 360;
        n.height = 240;
        // An image keeps its own proportions
        std::wstring lower = usable[k];
        for (auto& ch : lower) ch = (wchar_t)towlower(ch);
        doc.model.nodes.push_back(n);
        SheetCard& c = card(app, doc, doc.model.nodes.back());
        if (c.kind == SheetCard::Kind::Image) {
            IWICBitmapDecoder* decoder = nullptr;
            if (app.wicFactory && SUCCEEDED(app.wicFactory->CreateDecoderFromFilename(usable[k].c_str(), nullptr, GENERIC_READ,
                                                                                   WICDecodeMetadataCacheOnDemand, &decoder))) {
                IWICBitmapFrameDecode* frame = nullptr;
                UINT w = 0, h = 0;
                if (SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w && h) {
                    doc.model.nodes.back().height = snap(360.0f * h / w);
                }
                if (frame) frame->Release();
                decoder->Release();
            }
        }
        doc.selected.push_back(n.id);
    }
    commitStep(app, doc);
    pollFolder(app, doc, true);
}

bool pasteFromClipboard(App& app, HWND hwnd, SheetDoc& doc, float x, float y) {
    // Cards copied from a sheet
    if (IsClipboardFormatAvailable(cardsFormat()) && openClipboard(hwnd)) {
        std::string json;
        if (HANDLE data = GetClipboardData(cardsFormat())) {
            if (const char* p = static_cast<const char*>(GlobalLock(data))) {
                json = p;
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
        Sheet part;
        if (!json.empty() && parseCanvas(json, part)) {
            beginStep(doc);
            placeAt(app, doc, part, x, y);
            commitStep(app, doc);
            return true;
        }
    }
    // Files copied in Explorer
    if (IsClipboardFormatAvailable(CF_HDROP) && openClipboard(hwnd)) {
        std::vector<std::wstring> files;
        if (HDROP drop = (HDROP)GetClipboardData(CF_HDROP)) {
            UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT k = 0; k < count && k < 200; ++k) {
                wchar_t path[MAX_PATH * 4];
                if (DragQueryFileW(drop, k, path, (UINT)std::size(path))) files.push_back(path);
            }
        }
        CloseClipboard();
        if (!files.empty()) {
            addFileCards(app, doc, files, x, y);
            return true;
        }
    }
    std::wstring text = clipboardText(hwnd);
    std::wstring trimmed = text;
    while (!trimmed.empty() && iswspace(trimmed.back())) trimmed.pop_back();
    while (!trimmed.empty() && iswspace(trimmed.front())) trimmed.erase(trimmed.begin());
    if (trimmed.empty()) return false;
    std::string utf8 = toUtf8(trimmed);
    // A Mermaid flowchart becomes cards and arrows
    FlowImport flow;
    if (importFlowchart(utf8, flow, 240, 120)) {
        beginStep(doc);
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (auto& c : flow.cards) {
            minX = std::min(minX, c.x);
            minY = std::min(minY, c.y);
            maxX = std::max(maxX, c.x + c.width);
            maxY = std::max(maxY, c.y + c.height);
        }
        float ox = snap(x - (minX + maxX) * 0.5f), oy = snap(y - (minY + maxY) * 0.5f);
        std::vector<std::string> ids;
        doc.selected.clear();
        for (auto& c : flow.cards) {
            std::string id = newNoteCard(app, doc, c.x + ox, c.y + oy, "# " + c.label + "\n", c.width, c.height);
            ids.push_back(id);
            if (!id.empty()) doc.selected.push_back(id);
        }
        for (auto& l : flow.links) {
            if (ids[l.from].empty() || ids[l.to].empty()) continue;
            addEdge(doc, ids[l.from], ids[l.to], l.label);
            if (!l.arrow) doc.model.edges.back().toArrow = false;
        }
        for (auto& g : flow.groups) {
            std::vector<int> members;
            for (int k : g.cards) {
                int i = nodeIndex(doc.model, ids[k]);
                if (i >= 0) members.push_back(i);
            }
            if (members.empty()) continue;
            Box b = bounds(doc.model, members);
            Node group;
            group.id = newId();
            group.type = NodeType::Group;
            group.label = g.label;
            group.x = b.left - 30;
            group.y = b.top - 70;
            group.width = b.right - b.left + 60;
            group.height = b.bottom - b.top + 100;
            doc.model.nodes.insert(doc.model.nodes.begin(), group);
        }
        commitStep(app, doc);
        signalHint(app, SIGI_CHECK, tr(app, "sheet.mermaid_imported"));
        return true;
    }
    // An address becomes a link card
    bool url = trimmed.find(L'\n') == std::wstring::npos &&
               (trimmed.rfind(L"http://", 0) == 0 || trimmed.rfind(L"https://", 0) == 0);
    beginStep(doc);
    if (url) {
        Node n;
        n.id = newId();
        n.type = NodeType::Link;
        n.url = utf8;
        n.width = 400;
        n.height = 120;
        n.x = snap(x - 200);
        n.y = snap(y - 60);
        doc.model.nodes.push_back(n);
        selectOnly(doc, n.id);
    } else {
        // Text becomes a note of its own
        std::string id = newNoteCard(app, doc, x - 180, y - 120, utf8 + "\n");
        if (!id.empty()) selectOnly(doc, id);
    }
    commitStep(app, doc);
    return true;
}

void alignSelection(App& app, SheetDoc& doc, int action) {
    std::vector<int> picked = selectedIndexes(doc);
    if (picked.size() < 2) return;
    Box b = bounds(doc.model, picked);
    beginStep(doc);
    doc.glide.from.clear();
    for (const Node& n : doc.model.nodes) doc.glide.from[n.id] = boxOf(n);
    if (action == ACT_DISTRIBUTE_H || action == ACT_DISTRIBUTE_V) {
        bool h = action == ACT_DISTRIBUTE_H;
        std::sort(picked.begin(), picked.end(), [&](int a, int c) {
            return h ? doc.model.nodes[a].x < doc.model.nodes[c].x : doc.model.nodes[a].y < doc.model.nodes[c].y;
        });
        float total = 0;
        for (int i : picked) total += h ? doc.model.nodes[i].width : doc.model.nodes[i].height;
        float gap = ((h ? b.right - b.left : b.bottom - b.top) - total) / (picked.size() - 1);
        float at = h ? b.left : b.top;
        for (int i : picked) {
            Node& n = doc.model.nodes[i];
            (h ? n.x : n.y) = at;
            at += (h ? n.width : n.height) + gap;
        }
    } else {
        for (int i : picked) {
            Node& n = doc.model.nodes[i];
            switch (action) {
                case ACT_ALIGN_LEFT: n.x = b.left; break;
                case ACT_ALIGN_RIGHT: n.x = b.right - n.width; break;
                case ACT_ALIGN_CENTER: n.x = (b.left + b.right - n.width) * 0.5f; break;
                case ACT_ALIGN_TOP: n.y = b.top; break;
                case ACT_ALIGN_BOTTOM: n.y = b.bottom - n.height; break;
                case ACT_ALIGN_MIDDLE: n.y = (b.top + b.bottom - n.height) * 0.5f; break;
            }
        }
    }
    doc.glide.active = true;
    doc.glide.startMs = nowMs();
    commitStep(app, doc);
}

void sortIntoLanes(App& app, SheetDoc& doc) {
    beginStep(doc);
    doc.glide.from.clear();
    for (const Node& n : doc.model.nodes) doc.glide.from[n.id] = boxOf(n);
    int moved = 0;
    for (int i = 0; i < (int)doc.model.nodes.size(); ++i) {
        Node& n = doc.model.nodes[i];
        if (n.type != NodeType::File) continue;
        SheetCard& c = card(app, doc, n);
        if (c.kind != SheetCard::Kind::Note) continue;
        int here = groupOf(doc.model, i);
        for (int g = 0; g < (int)doc.model.nodes.size(); ++g) {
            const Node& lane = doc.model.nodes[g];
            if (lane.type != NodeType::Group || lane.laneKey.empty() || g == here) continue;
            std::string value = frontmatterValue(c.source, lane.laneKey);
            if (value.empty() || _stricmp(value.c_str(), lane.label.c_str()) != 0) continue;
            // Under the lane's lowest card
            float y = lane.y + 70;
            for (int k = 0; k < (int)doc.model.nodes.size(); ++k) {
                if (k == i || doc.model.nodes[k].type == NodeType::Group || groupOf(doc.model, k) != g) continue;
                y = std::max(y, doc.model.nodes[k].y + doc.model.nodes[k].height + 30);
            }
            n.x = snap(lane.x + std::max(20.0f, (lane.width - n.width) * 0.5f));
            n.y = snap(y);
            if (n.y + n.height > lane.y + lane.height - 20) {
                doc.model.nodes[g].height = snap(n.y + n.height + 40 - lane.y);
            }
            ++moved;
            break;
        }
    }
    if (moved) {
        doc.glide.active = true;
        doc.glide.startMs = nowMs();
        commitStep(app, doc);
    } else {
        doc.undo.pop_back();
    }
    signalHint(app, SIGI_CHECK, trCount(app.currentLanguageIndex, "sheet.sorted", moved));
}

}  // namespace

namespace sheetui {

void updateFind(App& app, SheetDoc& doc) {
    doc.found.clear();
    doc.foundIndex = -1;
    std::wstring needle = toLower(doc.edit.text);
    if (needle.empty()) return;
    for (int i : readingOrder(doc.model)) {
        const Node& n = doc.model.nodes[i];
        SheetCard& c = card(app, doc, n);
        std::wstring hay = toLower(toWide(c.title + "\n" + c.source + "\n" + n.url + "\n" + n.text));
        if (hay.find(needle) != std::wstring::npos) doc.found.push_back(n.id);
    }
    for (const Node& n : doc.model.nodes) {
        if (n.type == NodeType::Group && toLower(toWide(n.label)).find(needle) != std::wstring::npos) doc.found.push_back(n.id);
    }
    if (!doc.found.empty()) {
        doc.foundIndex = 0;
        int i = nodeIndex(doc.model, doc.found[0]);
        if (i >= 0) {
            D2D1_RECT_F r = screenRect(app, doc, boxOf(doc.model.nodes[i]));
            D2D1_RECT_F v = viewRect(app);
            if (r.left < v.left || r.right > v.right || r.top < v.top || r.bottom > v.bottom)
                fitBox(app, doc, boxOf(doc.model.nodes[i]), true, std::max(doc.zoom, 0.6f));
        }
    }
}

void startPresenting(App& app, SheetDoc& doc) {
    doc.slides.clear();
    // Along the arrows; a group shows whole before its first card
    std::set<int> shownGroups;
    for (int i : readingOrder(doc.model)) {
        int g = groupOf(doc.model, i);
        if (g >= 0 && !shownGroups.count(g)) {
            shownGroups.insert(g);
            doc.slides.push_back(doc.model.nodes[g].id);
        }
        doc.slides.push_back(doc.model.nodes[i].id);
    }
    if (doc.slides.empty()) {
        hint(app, tr(app, "sheet.nothing_to_present"));
        return;
    }
    if (doc.editing != SheetDoc::Edit::None) commitEdit(app, doc);
    doc.presentReturnX = doc.camX;
    doc.presentReturnY = doc.camY;
    doc.presentReturnZoom = doc.zoom;
    doc.presenting = true;
    doc.spotlight = false;
    doc.finding = false;
    doc.menuOpen = false;
    int start = 0;
    if (doc.selected.size() == 1) {
        auto it = std::find(doc.slides.begin(), doc.slides.end(), doc.selected[0]);
        if (it != doc.slides.end()) start = (int)(it - doc.slides.begin());
    }
    showSlide(app, doc, start);
}

void showSlide(App& app, SheetDoc& doc, int index) {
    if (doc.slides.empty()) return;
    doc.slide = std::clamp(index, 0, (int)doc.slides.size() - 1);
    int i = nodeIndex(doc.model, doc.slides[doc.slide]);
    if (i < 0) return;
    fitBox(app, doc, boxOf(doc.model.nodes[i]), true, 2.5f);
    doc.flight.durationMs = 650;
    invalidate(app);
}

void stopPresenting(App& app, SheetDoc& doc) {
    if (!doc.presenting) return;
    doc.presenting = false;
    flyTo(doc, doc.presentReturnX, doc.presentReturnY, doc.presentReturnZoom, 450);
    ensureTimer(app);
    invalidate(app);
}

void toggleSpotlight(App& app, SheetDoc& doc) {
    if (doc.spotlight) {
        doc.spotlight = false;
        invalidate(app);
        return;
    }
    if (doc.selected.size() != 1) {
        hint(app, tr(app, "sheet.spotlight_hint"));
        return;
    }
    doc.spotlight = true;
    doc.spotlightId = doc.selected[0];
    std::vector<int> around;
    int self = nodeIndex(doc.model, doc.spotlightId);
    if (self >= 0) around.push_back(self);
    for (const Edge& e : doc.model.edges) {
        if (e.fromNode == doc.spotlightId) around.push_back(nodeIndex(doc.model, e.toNode));
        if (e.toNode == doc.spotlightId) around.push_back(nodeIndex(doc.model, e.fromNode));
    }
    for (const auto& g : ghostLinks(app, doc)) {
        if (g.first == doc.spotlightId) around.push_back(nodeIndex(doc.model, g.second));
        if (g.second == doc.spotlightId) around.push_back(nodeIndex(doc.model, g.first));
    }
    fitBox(app, doc, bounds(doc.model, around), true, 1.2f);
    invalidate(app);
}

void closeMenu(SheetDoc& doc) {
    doc.menuOpen = false;
    doc.menu.clear();
    doc.menuHover = -1;
}

void openMenu(App& app, SheetDoc& doc, float x, float y) {
    using Item = SheetDoc::MenuItem;
    doc.menu.clear();
    auto add = [&](int action, const char* key, const wchar_t* keys = L"", bool enabled = true) {
        Item item;
        item.action = action;
        item.label = tr(app, key);
        item.keys = keys;
        item.enabled = enabled;
        doc.menu.push_back(item);
    };
    auto sep = [&] {
        if (!doc.menu.empty() && !doc.menu.back().separator) {
            Item item;
            item.separator = true;
            doc.menu.push_back(item);
        }
    };
    auto swatches = [&] {
        Item item;
        item.swatches = true;
        doc.menu.push_back(item);
    };
    D2D1_POINT_2F s = toSheet(app, doc, x, y);
    doc.menuSheetX = s.x;
    doc.menuSheetY = s.y;
    // What is under the pointer decides the menu
    float tol = 8.0f / scale(app, doc);
    std::string edgeHit;
    for (const Edge& e : doc.model.edges) {
        if (curveDistance(edgeCurveFor(doc, e), s.x, s.y) < tol) edgeHit = e.id;
    }
    int at = nodeAt(doc, s.x, s.y);
    if (!edgeHit.empty() && (at < 0 || doc.model.nodes[at].type == NodeType::Group)) {
        doc.selected.clear();
        doc.selectedEdge = edgeHit;
        add(ACT_EDGE_LABEL, "sheet.menu.label", L"Enter");
        swatches();
        sep();
        add(ACT_EDGE_FORWARD, "sheet.menu.forward");
        add(ACT_EDGE_BACKWARD, "sheet.menu.backward");
        add(ACT_EDGE_BOTH, "sheet.menu.both");
        add(ACT_EDGE_NONE, "sheet.menu.no_arrows");
        add(ACT_REANCHOR, "sheet.menu.reanchor");
        sep();
        add(ACT_REMOVE, "sheet.menu.remove", L"Del");
    } else if (at >= 0) {
        const Node& n = doc.model.nodes[at];
        if (!isSelected(doc, n.id)) selectOnly(doc, n.id);
        bool many = doc.selected.size() > 1;
        if (n.type == NodeType::Group && !many) {
            add(ACT_GROUP_RENAME, "sheet.menu.rename", L"Enter");
            swatches();
            sep();
            add(ACT_LANE, n.laneKey.empty() ? "sheet.menu.lane" : "sheet.menu.lane_off");
            add(ACT_SORT_LANES, "sheet.menu.sort_lanes");
            add(ACT_UNGROUP, "sheet.menu.ungroup", L"Ctrl+Shift+G");
            sep();
            add(ACT_TIDY, "sheet.menu.tidy", L"Ctrl+L");
            add(ACT_PRESENT, "sheet.menu.present_here", L"P");
            add(ACT_REMOVE, "sheet.menu.remove", L"Del");
        } else {
            SheetCard& c = card(app, doc, n);
            if (!many) {
                if (c.kind == SheetCard::Kind::Note) add(ACT_EDIT, "sheet.menu.edit", L"Enter");
                if (n.type == NodeType::Link) add(ACT_EDIT, "sheet.menu.edit_link", L"Enter");
                if (n.type == NodeType::File || n.type == NodeType::Link)
                    add(ACT_OPEN_TAB, n.type == NodeType::Link ? "sheet.menu.open_link" : "sheet.menu.open", L"Ctrl+Enter");
                if (n.type == NodeType::Text) add(ACT_TO_FILE, "sheet.menu.to_file");
                if (c.kind == SheetCard::Kind::Note && c.contentHeight > 0) add(ACT_FIT_HEIGHT, "sheet.menu.fit_height");
                add(ACT_SPOTLIGHT, "sheet.menu.spotlight", L"S");
                bool ghosted = false;
                for (const auto& g : ghostLinks(app, doc)) {
                    if (g.first == n.id || g.second == n.id) ghosted = true;
                }
                if (ghosted) add(ACT_LINKS_TO_ARROWS, "sheet.menu.links_to_arrows");
            }
            swatches();
            sep();
            add(ACT_GROUP_SELECTION, "sheet.menu.group", L"Ctrl+G");
            add(ACT_DUPLICATE, "sheet.menu.duplicate", L"Ctrl+D");
            if (many) {
                sep();
                add(ACT_ALIGN_LEFT, "sheet.menu.align_left");
                add(ACT_ALIGN_CENTER, "sheet.menu.align_center");
                add(ACT_ALIGN_TOP, "sheet.menu.align_top");
                add(ACT_DISTRIBUTE_H, "sheet.menu.distribute_h");
                add(ACT_DISTRIBUTE_V, "sheet.menu.distribute_v");
                add(ACT_TIDY, "sheet.menu.tidy", L"Ctrl+L");
                add(ACT_COMPILE, "sheet.menu.compile_selection");
            }
            add(ACT_COPY_MERMAID, "sheet.menu.copy_mermaid");
            sep();
            add(ACT_FRONT, "sheet.menu.front");
            add(ACT_BACK, "sheet.menu.back");
            add(ACT_REMOVE, "sheet.menu.remove", L"Del");
        }
    } else {
        doc.selected.clear();
        doc.selectedEdge.clear();
        add(ACT_NEW_NOTE, "sheet.menu.new_note", L"N");
        add(ACT_ADD_FILES, "sheet.menu.add_files");
        add(ACT_ADD_LINK, "sheet.menu.add_link");
        add(ACT_ADD_GROUP, "sheet.menu.add_group");
        add(ACT_PASTE, "sheet.menu.paste", L"Ctrl+V");
        sep();
        add(ACT_TIDY, "sheet.menu.tidy", L"Ctrl+L");
        add(ACT_TIDY_DOWN, "sheet.menu.tidy_down", L"Ctrl+Shift+L");
        add(ACT_SORT_LANES, "sheet.menu.sort_lanes");
        add(ACT_PRESENT, "sheet.menu.present", L"P");
        add(ACT_COMPILE, "sheet.menu.compile");
        add(ACT_COPY_MERMAID, "sheet.menu.copy_mermaid");
        if (!ghostLinks(app, doc).empty()) add(ACT_LINKS_TO_ARROWS, "sheet.menu.all_links_to_arrows");
        sep();
        add(ACT_SELECT_ALL, "sheet.menu.select_all", L"Ctrl+A");
        add(ACT_ZOOM_FIT, "sheet.menu.zoom_fit", L"Shift+1");
        add(ACT_SHOW_FOLDER, "sheet.menu.show_folder");
    }
    while (!doc.menu.empty() && doc.menu.back().separator) doc.menu.pop_back();
    doc.menuOpen = true;
    doc.menuX = x;
    doc.menuY = y;
    doc.menuHover = -1;
    invalidate(app);
}

void runAction(App& app, HWND hwnd, SheetDoc& doc, int action) {
    D2D1_POINT_2F centre = viewCentre(app, doc);
    float s = scale(app, doc);
    if (action >= ACT_COLOR_BASE && action <= ACT_COLOR_BASE + 6) {
        std::string color = action == ACT_COLOR_BASE ? std::string() : std::string(1, (char)('0' + action - ACT_COLOR_BASE));
        beginStep(doc);
        if (!doc.selectedEdge.empty()) {
            int e = edgeIndex(doc.model, doc.selectedEdge);
            if (e >= 0) doc.model.edges[e].color = color;
        }
        for (int i : selectedIndexes(doc)) doc.model.nodes[i].color = color;
        commitStep(app, doc);
        return;
    }
    if (action >= ACT_TRAY_ROW && action < ACT_TRAY_ROW + 100) {
        int row = action - ACT_TRAY_ROW;
        if (row < (int)doc.unplaced.size()) {
            D2D1_POINT_2F spot = freeSpot(doc, centre.x - 180, centre.y - 120, 360, 240);
            addFileCards(app, doc, {doc.unplaced[row]}, spot.x, spot.y);
        }
        return;
    }
    switch (action) {
        case ACT_NEW_NOTE: {
            D2D1_POINT_2F spot = doc.menuOpen || doc.menuSheetX != 0 ? D2D1::Point2F(doc.menuSheetX - 180, doc.menuSheetY - 120)
                                                                     : D2D1::Point2F(centre.x - 180, centre.y - 120);
            spot = freeSpot(doc, spot.x, spot.y, 360, 240);
            doc.menuSheetX = doc.menuSheetY = 0;
            newCardAndEdit(app, doc, spot.x, spot.y);
            break;
        }
        case ACT_ADD_FILES: {
            std::vector<wchar_t> buffer(32768, 0);
            std::wstring filter = std::wstring(tr(app, "sheet.filter_files")) + L" (*.*)";
            filter.push_back(L'\0');
            filter += L"*.*";
            filter.push_back(L'\0');
            filter.push_back(L'\0');
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = filter.c_str();
            ofn.lpstrFile = buffer.data();
            ofn.nMaxFile = (DWORD)buffer.size();
            ofn.lpstrInitialDir = doc.cardDir.c_str();
            ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
            if (!runOpenFileDialog(ofn)) break;
            std::vector<std::wstring> files;
            std::wstring first = buffer.data();
            const wchar_t* p = buffer.data() + first.size() + 1;
            if (!*p) {
                files.push_back(first);
            } else {
                while (*p) {
                    files.push_back(first + L"\\" + p);
                    p += wcslen(p) + 1;
                }
            }
            D2D1_POINT_2F spot = freeSpot(doc, centre.x - 180, centre.y - 120, 360, 240);
            addFileCards(app, doc, files, spot.x, spot.y);
            break;
        }
        case ACT_ADD_LINK: {
            beginStep(doc);
            Node n;
            n.id = newId();
            n.type = NodeType::Link;
            n.width = 400;
            n.height = 120;
            D2D1_POINT_2F spot = freeSpot(doc, centre.x - 200, centre.y - 60, 400, 120);
            n.x = spot.x;
            n.y = spot.y;
            doc.model.nodes.push_back(n);
            commitStep(app, doc);
            selectOnly(doc, n.id);
            startEdit(app, doc, SheetDoc::Edit::Url, n.id);
            break;
        }
        case ACT_ADD_GROUP: {
            if (!doc.selected.empty()) {
                groupSelection(app, doc);
                break;
            }
            beginStep(doc);
            Node g;
            g.id = newId();
            g.type = NodeType::Group;
            g.label = toUtf8(tr(app, "sheet.group_default"));
            g.width = 800;
            g.height = 520;
            g.x = snap(centre.x - 400);
            g.y = snap(centre.y - 260);
            doc.model.nodes.insert(doc.model.nodes.begin(), g);
            commitStep(app, doc);
            selectOnly(doc, g.id);
            startEdit(app, doc, SheetDoc::Edit::GroupLabel, g.id);
            break;
        }
        case ACT_TIDY:
        case ACT_TIDY_DOWN: {
            beginStep(doc);
            doc.glide.from.clear();
            for (const Node& n : doc.model.nodes) doc.glide.from[n.id] = boxOf(n);
            std::vector<int> subset = selectedIndexes(doc);
            if (subset.size() < 2 && !(subset.size() == 1 && doc.model.nodes[subset[0]].type == NodeType::Group)) subset.clear();
            tidy(doc.model, subset, action == ACT_TIDY);
            for (Node& n : doc.model.nodes) {
                n.x = snap(n.x);
                n.y = snap(n.y);
            }
            doc.glide.active = true;
            doc.glide.startMs = nowMs();
            commitStep(app, doc);
            std::vector<int> moved = subset.empty() ? std::vector<int>() : subset;
            fitBox(app, doc, bounds(doc.model, moved), true, std::max(doc.zoom, 0.25f));
            break;
        }
        case ACT_PRESENT: startPresenting(app, doc); break;
        case ACT_PRESENT_PREV: showSlide(app, doc, doc.slide - 1); break;
        case ACT_PRESENT_NEXT:
            if (doc.slide + 1 >= (int)doc.slides.size()) stopPresenting(app, doc);
            else showSlide(app, doc, doc.slide + 1);
            break;
        case ACT_PRESENT_EXIT: stopPresenting(app, doc); break;
        case ACT_COMPILE:
            compileSheet(app, hwnd, doc);
            return;
        case ACT_GHOSTS:
            doc.showGhosts = !doc.showGhosts;
            hint(app, tr(app, doc.showGhosts ? "sheet.ghosts_on" : "sheet.ghosts_off"));
            break;
        case ACT_MINIMAP: doc.showMinimap = !doc.showMinimap; break;
        case ACT_ZOOM_IN:
        case ACT_ZOOM_OUT: {
            float z = std::clamp(doc.zoom * (action == ACT_ZOOM_IN ? 1.25f : 0.8f), 0.05f, 4.0f);
            flyTo(doc, doc.camX, doc.camY, z, 160);
            doc.zoomChangedMs = nowMs();
            break;
        }
        case ACT_ZOOM_RESET: flyTo(doc, doc.camX, doc.camY, 1.0f, 220); break;
        case ACT_ZOOM_FIT:
            if (!doc.model.nodes.empty()) fitBox(app, doc, bounds(doc.model), true);
            break;
        case ACT_ZOOM_SELECTION:
            if (!doc.selected.empty()) fitBox(app, doc, bounds(doc.model, selectedIndexes(doc)), true, 2.0f);
            break;
        case ACT_EDIT: {
            if (doc.selected.size() != 1) break;
            int i = nodeIndex(doc.model, doc.selected[0]);
            if (i < 0) break;
            const Node& n = doc.model.nodes[i];
            if (n.type == NodeType::Group) startEdit(app, doc, SheetDoc::Edit::GroupLabel, n.id);
            else if (n.type == NodeType::Link) startEdit(app, doc, SheetDoc::Edit::Url, n.id);
            else if (card(app, doc, n).kind == SheetCard::Kind::Note) startEdit(app, doc, SheetDoc::Edit::Card, n.id);
            break;
        }
        case ACT_OPEN_TAB: {
            if (doc.selected.size() != 1) break;
            int i = nodeIndex(doc.model, doc.selected[0]);
            if (i < 0) break;
            const Node& n = doc.model.nodes[i];
            if (n.type == NodeType::Link) {
                openUrl(n.url);
            } else if (n.type == NodeType::File) {
                SheetCard& c = card(app, doc, n);
                if (c.kind == SheetCard::Kind::Note || isSupportedDocumentPath(std::wstring_view(c.path))) {
                    std::string path = toUtf8(c.path);
                    sheetLeave(app);
                    tabOpenPath(app, hwnd, path);
                    return;
                }
                if (c.kind != SheetCard::Kind::Missing && c.kind != SheetCard::Kind::Blocked)
                    ShellExecuteW(hwnd, L"open", c.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            break;
        }
        case ACT_GROUP_SELECTION: groupSelection(app, doc); break;
        case ACT_UNGROUP: {
            beginStep(doc);
            bool any = false;
            for (const std::string& id : std::vector<std::string>(doc.selected)) {
                int i = nodeIndex(doc.model, id);
                if (i >= 0 && doc.model.nodes[i].type == NodeType::Group) {
                    removeNode(doc.model, id);
                    any = true;
                }
            }
            if (any) commitStep(app, doc);
            else doc.undo.pop_back();
            doc.selected.clear();
            break;
        }
        case ACT_DUPLICATE:
            beginStep(doc);
            duplicateSelection(app, doc, 40, 40);
            commitStep(app, doc);
            break;
        case ACT_REMOVE: removeSelection(app, doc); break;
        case ACT_FRONT:
        case ACT_BACK: {
            beginStep(doc);
            std::vector<Node> picked, rest;
            for (const Node& n : doc.model.nodes) (isSelected(doc, n.id) ? picked : rest).push_back(n);
            doc.model.nodes = action == ACT_FRONT ? rest : picked;
            for (const Node& n : action == ACT_FRONT ? picked : rest) doc.model.nodes.push_back(n);
            commitStep(app, doc);
            break;
        }
        case ACT_COPY_MERMAID: {
            std::vector<int> subset = selectedIndexes(doc);
            if (subset.size() < 2) subset.clear();
            std::string text = exportFlowchart(doc.model, [&](const Node& n) { return cardTitle(app, doc, n); }, subset);
            std::wstring wide = toWide("```mermaid\n" + text + "```\n");
            std::wstring crlf;
            for (wchar_t c : wide) {
                if (c == L'\n') crlf += L'\r';
                crlf += c;
            }
            if (copyToClipboard(hwnd, crlf)) signalHint(app, SIGI_COPY, tr(app, "sheet.copied_mermaid"));
            break;
        }
        case ACT_PASTE: {
            D2D1_POINT_2F at = doc.menuSheetX != 0 || doc.menuSheetY != 0 ? D2D1::Point2F(doc.menuSheetX, doc.menuSheetY) : centre;
            pasteFromClipboard(app, hwnd, doc, at.x, at.y);
            break;
        }
        case ACT_SELECT_ALL:
            doc.selected.clear();
            for (const Node& n : doc.model.nodes) {
                if (n.type != NodeType::Group) doc.selected.push_back(n.id);
            }
            break;
        case ACT_EDGE_LABEL:
            if (!doc.selectedEdge.empty()) startEdit(app, doc, SheetDoc::Edit::EdgeLabel, doc.selectedEdge);
            break;
        case ACT_EDGE_FORWARD:
        case ACT_EDGE_BACKWARD:
        case ACT_EDGE_BOTH:
        case ACT_EDGE_NONE:
        case ACT_REANCHOR: {
            int e = edgeIndex(doc.model, doc.selectedEdge);
            if (e < 0) break;
            beginStep(doc);
            Edge& edge = doc.model.edges[e];
            if (action == ACT_REANCHOR) {
                edge.fromSide = edge.toSide = Side::None;
            } else {
                edge.toArrow = action == ACT_EDGE_FORWARD || action == ACT_EDGE_BOTH;
                edge.fromArrow = action == ACT_EDGE_BACKWARD || action == ACT_EDGE_BOTH;
            }
            commitStep(app, doc);
            break;
        }
        case ACT_GROUP_RENAME:
            if (doc.selected.size() == 1) startEdit(app, doc, SheetDoc::Edit::GroupLabel, doc.selected[0]);
            break;
        case ACT_LANE: {
            if (doc.selected.size() != 1) break;
            int g = nodeIndex(doc.model, doc.selected[0]);
            if (g < 0 || doc.model.nodes[g].type != NodeType::Group) break;
            if (!doc.model.nodes[g].laneKey.empty()) {
                beginStep(doc);
                doc.model.nodes[g].laneKey.clear();
                commitStep(app, doc);
            } else {
                startEdit(app, doc, SheetDoc::Edit::LaneKey, doc.model.nodes[g].id);
            }
            break;
        }
        case ACT_SORT_LANES: sortIntoLanes(app, doc); break;
        case ACT_LINKS_TO_ARROWS: {
            // Links written in the notes become arrows: the selection's, or all
            std::set<std::string> picked(doc.selected.begin(), doc.selected.end());
            beginStep(doc);
            int made = 0;
            for (const auto& g : ghostLinks(app, doc)) {
                if (!picked.empty() && !picked.count(g.first) && !picked.count(g.second)) continue;
                addEdge(doc, g.first, g.second);
                ++made;
            }
            if (made) {
                commitStep(app, doc);
                signalHint(app, SIGI_CHECK, tr(app, "sheet.ghost_made"));
            } else {
                doc.undo.pop_back();
            }
            break;
        }
        case ACT_TO_FILE: {
            if (doc.selected.size() != 1) break;
            int i = nodeIndex(doc.model, doc.selected[0]);
            if (i < 0 || doc.model.nodes[i].type != NodeType::Text) break;
            beginStep(doc);
            std::string text = doc.model.nodes[i].text;
            std::string title = noteTitle(text, toUtf8(tr(app, "title.untitled")));
            std::string rel = writeNoteFile(app, doc, toWide(title), text);
            if (rel.empty()) {
                doc.undo.pop_back();
                break;
            }
            doc.model.nodes[i].type = NodeType::File;
            doc.model.nodes[i].file = rel;
            doc.model.nodes[i].text.clear();
            refreshCard(app, doc, doc.model.nodes[i], true);
            commitStep(app, doc);
            break;
        }
        case ACT_FIT_HEIGHT: {
            if (doc.selected.size() != 1) break;
            int i = nodeIndex(doc.model, doc.selected[0]);
            if (i < 0) break;
            SheetCard& c = card(app, doc, doc.model.nodes[i]);
            if (c.contentHeight <= 0) break;
            beginStep(doc);
            doc.model.nodes[i].height = snap(c.contentHeight + cardPadding() * 2 + kGrid * 0.5f);
            commitStep(app, doc);
            break;
        }
        case ACT_ALIGN_LEFT:
        case ACT_ALIGN_CENTER:
        case ACT_ALIGN_RIGHT:
        case ACT_ALIGN_TOP:
        case ACT_ALIGN_MIDDLE:
        case ACT_ALIGN_BOTTOM:
        case ACT_DISTRIBUTE_H:
        case ACT_DISTRIBUTE_V:
            alignSelection(app, doc, action);
            break;
        case ACT_SPOTLIGHT: toggleSpotlight(app, doc); break;
        case ACT_FIND:
            doc.finding = true;
            startEdit(app, doc, SheetDoc::Edit::Find, {});
            break;
        case ACT_FIND_CLOSE:
            doc.finding = false;
            doc.found.clear();
            if (doc.editing == SheetDoc::Edit::Find) cancelEdit(app, doc);
            break;
        case ACT_TRAY: doc.trayOpen = !doc.trayOpen; break;
        case ACT_PLACE_ALL: {
            std::vector<std::wstring> files = doc.unplaced;
            Box all = bounds(doc.model);
            float x = doc.model.nodes.empty() ? centre.x : all.left, y = doc.model.nodes.empty() ? centre.y : all.bottom + 120;
            addFileCards(app, doc, files, x, y);
            doc.trayOpen = false;
            if (!files.empty()) fitBox(app, doc, bounds(doc.model, selectedIndexes(doc)), true, std::max(0.3f, doc.zoom));
            break;
        }
        case ACT_SHOW_FOLDER:
            ShellExecuteW(hwnd, L"open", doc.cardDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case ACT_UNDO: undoStep(app, doc, false); break;
        case ACT_REDO: undoStep(app, doc, true); break;
        default: break;
    }
    (void)s;
    ensureTimer(app);
    invalidate(app);
}

}  // namespace sheetui

// --- Keys ------------------------------------------------------------------

bool sheetKeyDown(App& app, HWND hwnd, WPARAM key, bool& handled) {
    handled = false;
    SheetDoc* docPtr = activeSheet(app);
    if (!docPtr) return false;
    SheetDoc& doc = *docPtr;
    bool ctrl = keyDown(VK_CONTROL), shift = keyDown(VK_SHIFT), alt = keyDown(VK_MENU);
    if (alt) return false;
    handled = true;
    // The menu takes the keys while it is up
    if (doc.menuOpen) {
        int count = (int)doc.menu.size();
        if (key == VK_ESCAPE) {
            closeMenu(doc);
        } else if (key == VK_DOWN || key == VK_UP) {
            int at = doc.menuHover;
            for (int step = 0; step < count; ++step) {
                at = (at + (key == VK_DOWN ? 1 : -1) + count) % count;
                if (at < 0) at = 0;
                if (!doc.menu[at].separator && !doc.menu[at].swatches && doc.menu[at].enabled) break;
            }
            doc.menuHover = at;
        } else if (key == VK_RETURN && doc.menuHover >= 0 && doc.menuHover < count) {
            int action = doc.menu[doc.menuHover].action;
            closeMenu(doc);
            runAction(app, hwnd, doc, action);
        }
        invalidate(app);
        return true;
    }
    if (doc.editing != SheetDoc::Edit::None) {
        bool took = editKey(app, hwnd, doc, key);
        return took;  // unconsumed typing keys come back as characters
    }
    if (doc.presenting) {
        switch (key) {
            case VK_RIGHT: case VK_DOWN: case VK_SPACE: case VK_NEXT: case VK_RETURN:
                runAction(app, hwnd, doc, ACT_PRESENT_NEXT);
                return true;
            case VK_LEFT: case VK_UP: case VK_PRIOR: case VK_BACK:
                runAction(app, hwnd, doc, ACT_PRESENT_PREV);
                return true;
            case VK_HOME: showSlide(app, doc, 0); return true;
            case VK_END: showSlide(app, doc, (int)doc.slides.size() - 1); return true;
            case VK_ESCAPE: case 'P': stopPresenting(app, doc); return true;
            default: return true;
        }
    }
    if (key == VK_SPACE) {
        doc.spaceHeld = true;
        return true;
    }
    if (ctrl) {
        switch (key) {
            case 'Z': runAction(app, hwnd, doc, shift ? ACT_REDO : ACT_UNDO); return true;
            case 'Y': runAction(app, hwnd, doc, ACT_REDO); return true;
            case 'A': runAction(app, hwnd, doc, ACT_SELECT_ALL); invalidate(app); return true;
            case 'C': copyCards(app, hwnd, doc); return true;
            case 'X':
                copyCards(app, hwnd, doc);
                removeSelection(app, doc);
                invalidate(app);
                return true;
            case 'V': {
                D2D1_POINT_2F at = toSheet(app, doc, (float)app.mouseX, (float)app.mouseY);
                D2D1_RECT_F v = viewRect(app);
                if (app.mouseX < v.left || app.mouseY < v.top) {
                    at = toSheet(app, doc, (v.left + v.right) * 0.5f, (v.top + v.bottom) * 0.5f);
                }
                pasteFromClipboard(app, hwnd, doc, at.x, at.y);
                invalidate(app);
                return true;
            }
            case 'D': runAction(app, hwnd, doc, ACT_DUPLICATE); return true;
            case 'G': runAction(app, hwnd, doc, shift ? ACT_UNGROUP : ACT_GROUP_SELECTION); return true;
            case 'L': runAction(app, hwnd, doc, shift ? ACT_TIDY_DOWN : ACT_TIDY); return true;
            case 'F': runAction(app, hwnd, doc, ACT_FIND); return true;
            case VK_RETURN: runAction(app, hwnd, doc, ACT_OPEN_TAB); return true;
            case '0': case VK_NUMPAD0: runAction(app, hwnd, doc, ACT_ZOOM_RESET); return true;
            case VK_OEM_PLUS: case VK_ADD: runAction(app, hwnd, doc, ACT_ZOOM_IN); return true;
            case VK_OEM_MINUS: case VK_SUBTRACT: runAction(app, hwnd, doc, ACT_ZOOM_OUT); return true;
            case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: {
                // Nudge: a grid step, or one unit with Shift
                float step = shift ? 1.0f : kGrid;
                float dx = key == VK_LEFT ? -step : key == VK_RIGHT ? step : 0;
                float dy = key == VK_UP ? -step : key == VK_DOWN ? step : 0;
                std::vector<int> moving = movingSet(doc);
                if (moving.empty()) return true;
                beginStep(doc);
                for (int i : moving) {
                    doc.model.nodes[i].x += dx;
                    doc.model.nodes[i].y += dy;
                }
                commitStep(app, doc);
                return true;
            }
            default:
                handled = false;  // Ctrl+T, Ctrl+W, Ctrl+O and the rest stay the window's
                return false;
        }
    }
    switch (key) {
        case VK_ESCAPE:
            if (doc.spotlight) doc.spotlight = false;
            else if (doc.finding) runAction(app, hwnd, doc, ACT_FIND_CLOSE);
            else if (doc.trayOpen) doc.trayOpen = false;
            else {
                doc.selected.clear();
                doc.selectedEdge.clear();
            }
            invalidate(app);
            return true;
        case VK_RETURN:
        case VK_F2:
            if (!doc.selectedEdge.empty()) runAction(app, hwnd, doc, ACT_EDGE_LABEL);
            else if (shift) growFromSelection(app, doc, true);
            else runAction(app, hwnd, doc, ACT_EDIT);
            return true;
        case 'E':
            if (doc.selected.empty() && doc.selectedEdge.empty()) {
                handled = false;  // E with nothing selected edits the sheet's own text
                return false;
            }
            runAction(app, hwnd, doc, ACT_EDIT);
            return true;
        case VK_TAB:
            if (doc.selected.empty()) {
                std::vector<int> order = readingOrder(doc.model);
                if (!order.empty()) selectOnly(doc, doc.model.nodes[order[0]].id);
                invalidate(app);
            } else {
                growFromSelection(app, doc, shift);
            }
            return true;
        case VK_DELETE:
        case VK_BACK:
            removeSelection(app, doc);
            invalidate(app);
            return true;
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN: {
            int dx = key == VK_LEFT ? -1 : key == VK_RIGHT ? 1 : 0;
            int dy = key == VK_UP ? -1 : key == VK_DOWN ? 1 : 0;
            int from = doc.selected.empty() ? -1 : nodeIndex(doc.model, doc.selected.back());
            int to = -1;
            if (from < 0) {
                std::vector<int> order = readingOrder(doc.model);
                if (!order.empty()) to = order[0];
            } else {
                to = neighbourInDirection(doc.model, from, dx, dy);
            }
            if (to >= 0) {
                if (shift) {
                    if (!isSelected(doc, doc.model.nodes[to].id)) doc.selected.push_back(doc.model.nodes[to].id);
                } else {
                    selectOnly(doc, doc.model.nodes[to].id);
                }
                // Follow it when it leaves the view
                D2D1_RECT_F r = screenRect(app, doc, boxOf(doc.model.nodes[to]));
                D2D1_RECT_F v = viewRect(app);
                if (r.left < v.left || r.right > v.right || r.top < v.top || r.bottom > v.bottom) {
                    const Node& n = doc.model.nodes[to];
                    flyTo(doc, n.x + n.width * 0.5f, n.y + n.height * 0.5f, doc.zoom, 260);
                    ensureTimer(app);
                }
            }
            invalidate(app);
            return true;
        }
        case 'N': runAction(app, hwnd, doc, ACT_NEW_NOTE); return true;
        case 'P': runAction(app, hwnd, doc, ACT_PRESENT); return true;
        case 'M': runAction(app, hwnd, doc, ACT_MINIMAP); return true;
        case 'L': runAction(app, hwnd, doc, ACT_GHOSTS); return true;
        case 'S': runAction(app, hwnd, doc, ACT_SPOTLIGHT); return true;
        case 'F': runAction(app, hwnd, doc, ACT_FIND); return true;
        case 'G': runAction(app, hwnd, doc, ACT_ADD_GROUP); return true;
        case '1':
            if (shift) {
                runAction(app, hwnd, doc, ACT_ZOOM_FIT);
                return true;
            }
            [[fallthrough]];
        case '2': case '3': case '4': case '5': case '6':
            if (shift && key == '2') {
                runAction(app, hwnd, doc, ACT_ZOOM_SELECTION);
                return true;
            }
            if (!shift && (!doc.selected.empty() || !doc.selectedEdge.empty())) {
                runAction(app, hwnd, doc, ACT_COLOR_BASE + (int)(key - '0'));
                return true;
            }
            return true;
        case '0':
            if (!doc.selected.empty() || !doc.selectedEdge.empty()) runAction(app, hwnd, doc, ACT_COLOR_BASE);
            return true;
        case VK_OEM_PLUS: case VK_ADD: runAction(app, hwnd, doc, ACT_ZOOM_IN); return true;
        case VK_OEM_MINUS: case VK_SUBTRACT: runAction(app, hwnd, doc, ACT_ZOOM_OUT); return true;
        default:
            handled = false;
            return false;
    }
}

bool sheetKeyUp(App& app, HWND, WPARAM key) {
    SheetDoc* doc = activeSheet(app);
    if (!doc) return false;
    if (key == VK_SPACE) {
        doc->spaceHeld = false;
        return true;
    }
    return false;
}

bool sheetCharInput(App& app, HWND, wchar_t ch) {
    SheetDoc* doc = activeSheet(app);
    if (!doc || doc->editing == SheetDoc::Edit::None) return doc != nullptr && doc->menuOpen;
    if (ch < 0x20 && ch != L'\t') return true;  // Enter, Backspace and the rest were keys
    if (ch == 0x7F) return true;
    replaceSelection(*doc, std::wstring(1, ch), true);
    if (doc->editing == SheetDoc::Edit::Find) updateFind(app, *doc);
    fitEditor(app, *doc);
    app.cursorBlinkOn = true;
    invalidate(app);
    return true;
}

// --- The pointer -------------------------------------------------------------

namespace {

int uiHitAt(const SheetDoc& doc, float x, float y) {
    for (int k = (int)doc.uiHits.size() - 1; k >= 0; --k) {
        const auto& r = doc.uiHits[k].first;
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return doc.uiHits[k].second;
    }
    return 0;
}

int menuHitAt(const SheetDoc& doc, float x, float y) {
    for (const auto& hit : doc.menuHits) {
        const auto& r = hit.first;
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return hit.second;
    }
    return -1;
}

bool inMenuBox(const SheetDoc& doc, float x, float y) {
    if (doc.menuHits.empty()) return false;
    float l = 1e9f, t = 1e9f, r = -1e9f, b = -1e9f;
    for (const auto& hit : doc.menuHits) {
        l = std::min(l, hit.first.left);
        t = std::min(t, hit.first.top);
        r = std::max(r, hit.first.right);
        b = std::max(b, hit.first.bottom);
    }
    return x >= l - 8 && x <= r + 8 && y >= t - 8 && y <= b + 8;
}

// The connection dot under the pointer: a side of a card the pointer is over or near
bool dotAt(App& app, SheetDoc& doc, float x, float y, std::string& node, Side& side) {
    float r = 9.0f * app.contentScale;
    std::vector<std::string> candidates = doc.selected;
    if (!doc.hoverNode.empty()) candidates.push_back(doc.hoverNode);
    for (const std::string& id : candidates) {
        int i = nodeIndex(doc.model, id);
        if (i < 0 || doc.model.nodes[i].type == NodeType::Group) continue;
        for (Side s : {Side::Top, Side::Right, Side::Bottom, Side::Left}) {
            D2D1_POINT_2F p = sidePoint(boxOf(doc.model.nodes[i]), s);
            D2D1_POINT_2F q = toScreen(app, doc, p.x, p.y);
            if (std::fabs(q.x - x) <= r && std::fabs(q.y - y) <= r) {
                node = id;
                side = s;
                return true;
            }
        }
    }
    return false;
}

// Resize edges of a selected card under the pointer: 1 left, 2 top, 4 right, 8 bottom
int resizeEdgesAt(App& app, SheetDoc& doc, float x, float y, std::string& node) {
    float tol = 6.0f * app.contentScale;
    for (const std::string& id : doc.selected) {
        int i = nodeIndex(doc.model, id);
        if (i < 0) continue;
        D2D1_RECT_F r = screenRect(app, doc, boxOf(doc.model.nodes[i]));
        if (x < r.left - tol || x > r.right + tol || y < r.top - tol || y > r.bottom + tol) continue;
        int edges = 0;
        if (std::fabs(x - r.left) <= tol) edges |= 1;
        if (std::fabs(x - r.right) <= tol) edges |= 4;
        if (std::fabs(y - r.top) <= tol) edges |= 2;
        if (std::fabs(y - r.bottom) <= tol) edges |= 8;
        // The corner knob is generous
        if (std::fabs(x - r.right) <= tol * 1.6f && std::fabs(y - r.bottom) <= tol * 1.6f) edges = 4 | 8;
        if (edges) {
            node = id;
            return edges;
        }
    }
    return 0;
}

void updateHover(App& app, SheetDoc& doc, float x, float y) {
    std::string node, edge, ghostFrom, ghostTo;
    int ui = uiHitAt(doc, x, y);
    std::wstring tip;
    if (ui > 0) {
        static const std::map<int, std::pair<const char*, const wchar_t*>> tips = {
            {ACT_NEW_NOTE, {"sheet.tool.note", L"N"}},         {ACT_ADD_FILES, {"sheet.tool.files", L""}},
            {ACT_ADD_LINK, {"sheet.tool.link", L""}},          {ACT_ADD_GROUP, {"sheet.tool.group", L"G"}},
            {ACT_TIDY, {"sheet.tool.tidy", L"Ctrl+L"}},        {ACT_PRESENT, {"sheet.tool.present", L"P"}},
            {ACT_COMPILE, {"sheet.tool.compile", L""}},        {ACT_GHOSTS, {"sheet.tool.ghosts", L"L"}},
            {ACT_MINIMAP, {"sheet.tool.minimap", L"M"}},
        };
        auto it = tips.find(ui);
        if (it != tips.end()) {
            tip = tr(app, it->second.first);
            if (*it->second.second) tip += std::wstring(L"  \u00B7  ") + it->second.second;
        }
    }
    if (ui == 0) {
        D2D1_POINT_2F s = toSheet(app, doc, x, y);
        int at = nodeAt(doc, s.x, s.y, false);
        if (at >= 0) node = doc.model.nodes[at].id;
        float tol = 7.0f / scale(app, doc);
        if (at < 0) {
            for (const Edge& e : doc.model.edges) {
                if (curveDistance(edgeCurveFor(doc, e), s.x, s.y) < tol) edge = e.id;
            }
            if (edge.empty() && doc.showGhosts) {
                std::vector<std::pair<std::string, std::string>> ghosts = ghostLinks(app, doc);
                for (const auto& g : ghosts) {
                    if (!ghostShown(doc, ghosts.size(), g)) continue;
                    int a = nodeIndex(doc.model, g.first), b = nodeIndex(doc.model, g.second);
                    if (a < 0 || b < 0) continue;
                    Side fs, ts;
                    edgeSides(doc.model.nodes[a], doc.model.nodes[b], Side::None, Side::None, fs, ts);
                    if (curveDistance(edgeCurve(boxOf(doc.model.nodes[a]), boxOf(doc.model.nodes[b]), fs, ts), s.x, s.y) < tol) {
                        ghostFrom = g.first;
                        ghostTo = g.second;
                    }
                }
            }
        }
    }
    // The pointer near a card's side keeps the dots up
    if (node.empty() && !doc.hoverNode.empty()) {
        std::string dotNode;
        Side side;
        if (dotAt(app, doc, x, y, dotNode, side)) node = dotNode;
    }
    if (node != doc.hoverNode || edge != doc.hoverEdge || ui != doc.hoverUi || ghostFrom != doc.hoverGhostFrom ||
        ghostTo != doc.hoverGhostTo || tip != doc.tip) {
        doc.hoverNode = node;
        doc.hoverEdge = edge;
        doc.hoverUi = ui;
        doc.hoverGhostFrom = ghostFrom;
        doc.hoverGhostTo = ghostTo;
        doc.tip = tip;
        invalidate(app);
    }
}

void snapMove(App& app, SheetDoc& doc, float& dx, float& dy, bool noSnap) {
    doc.guidesX.clear();
    doc.guidesY.clear();
    if (doc.moveStart.empty()) return;
    // The pressed card leads: its edges and centre meet other cards' edges
    // and centres within a few pixels, else its corner meets the grid
    const sheet::Box* lead = nullptr;
    for (const auto& entry : doc.moveStart) {
        if (entry.first == doc.pressNode) lead = &entry.second;
    }
    if (!lead) lead = &doc.moveStart[0].second;
    if (noSnap) return;
    float tol = 6.0f / scale(app, doc);
    std::set<std::string> moving;
    for (const auto& entry : doc.moveStart) moving.insert(entry.first);
    float bestX = tol, bestY = tol, snapX = 0, snapY = 0;
    bool hitX = false, hitY = false;
    float w = lead->right - lead->left, h = lead->bottom - lead->top;
    float xs[3] = {lead->left + dx, lead->left + dx + w * 0.5f, lead->right + dx};
    float ys[3] = {lead->top + dy, lead->top + dy + h * 0.5f, lead->bottom + dy};
    for (const Node& n : doc.model.nodes) {
        if (moving.count(n.id) || n.type == NodeType::Group) continue;
        float ox[3] = {n.x, n.x + n.width * 0.5f, n.x + n.width};
        float oy[3] = {n.y, n.y + n.height * 0.5f, n.y + n.height};
        for (float a : xs) {
            for (float b : ox) {
                if (std::fabs(a - b) < bestX) {
                    bestX = std::fabs(a - b);
                    snapX = b - a;
                    hitX = true;
                }
            }
        }
        for (float a : ys) {
            for (float b : oy) {
                if (std::fabs(a - b) < bestY) {
                    bestY = std::fabs(a - b);
                    snapY = b - a;
                    hitY = true;
                }
            }
        }
    }
    if (hitX) {
        dx += snapX;
        for (float a : {lead->left + dx, lead->left + dx + w * 0.5f, lead->right + dx}) {
            for (const Node& n : doc.model.nodes) {
                if (moving.count(n.id) || n.type == NodeType::Group) continue;
                for (float b : {n.x, n.x + n.width * 0.5f, n.x + n.width}) {
                    if (std::fabs(a - b) < 0.01f) doc.guidesX.push_back(b);
                }
            }
        }
    } else {
        dx = snap(lead->left + dx) - lead->left;
    }
    if (hitY) {
        dy += snapY;
        for (float a : {lead->top + dy, lead->top + dy + h * 0.5f, lead->bottom + dy}) {
            for (const Node& n : doc.model.nodes) {
                if (moving.count(n.id) || n.type == NodeType::Group) continue;
                for (float b : {n.y, n.y + n.height * 0.5f, n.y + n.height}) {
                    if (std::fabs(a - b) < 0.01f) doc.guidesY.push_back(b);
                }
            }
        }
    } else {
        dy = snap(lead->top + dy) - lead->top;
    }
}

void beginMove(App& app, SheetDoc& doc) {
    beginStep(doc);
    if (doc.pressAlt) duplicateSelection(app, doc, 0, 0);
    doc.moveStart.clear();
    for (int i : movingSet(doc)) doc.moveStart.push_back({doc.model.nodes[i].id, boxOf(doc.model.nodes[i])});
    doc.gesture = SheetDoc::Gesture::Move;
}

}  // namespace

void sheetSetCursor(App& app, int x, int y) {
    SheetDoc* doc = activeSheet(app);
    if (!doc) return;
    LPCWSTR cursor = IDC_ARROW;
    if (doc->gesture == SheetDoc::Gesture::Pan || (doc->spaceHeld && keyDown(VK_SPACE))) {
        cursor = IDC_SIZEALL;
    } else if (doc->gesture == SheetDoc::Gesture::Move) {
        cursor = IDC_SIZEALL;
    } else if (doc->gesture == SheetDoc::Gesture::Resize) {
        int e = doc->resizeEdges;
        cursor = (e == (4 | 8) || e == (1 | 2)) ? IDC_SIZENWSE : (e == (4 | 2) || e == (1 | 8)) ? IDC_SIZENESW
                 : (e & (1 | 4)) ? IDC_SIZEWE : IDC_SIZENS;
    } else if (doc->gesture == SheetDoc::Gesture::Connect) {
        cursor = IDC_CROSS;
    } else if (doc->menuOpen || doc->hoverUi > 0) {
        cursor = IDC_HAND;
    } else {
        D2D1_RECT_F rect{};
        float sc = 1;
        std::string node;
        Side side;
        if (doc->editing != SheetDoc::Edit::None && doc->editing != SheetDoc::Edit::Find && editRect(app, *doc, rect, sc) &&
            x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom) {
            cursor = IDC_IBEAM;
        } else if (dotAt(app, *doc, (float)x, (float)y, node, side)) {
            cursor = IDC_CROSS;
        } else if (int e = resizeEdgesAt(app, *doc, (float)x, (float)y, node)) {
            cursor = (e == (4 | 8) || e == (1 | 2)) ? IDC_SIZENWSE : (e == (4 | 2) || e == (1 | 8)) ? IDC_SIZENESW
                     : (e & (1 | 4)) ? IDC_SIZEWE : IDC_SIZENS;
        } else if (!doc->hoverEdge.empty() || !doc->hoverGhostFrom.empty()) {
            cursor = IDC_HAND;
        }
    }
    SetCursor(LoadCursorW(nullptr, cursor));
}

bool sheetMouseDown(App& app, HWND hwnd, int x, int y) {
    SheetDoc* docPtr = activeSheet(app);
    if (!docPtr) return false;
    SheetDoc& doc = *docPtr;
    D2D1_RECT_F v = viewRect(app);
    if (x < v.left || y < v.top) return false;
    float fx = (float)x, fy = (float)y;
    unsigned long long now = nowMs();
    bool dbl = now - doc.lastClickMs <= GetDoubleClickTime() && std::abs(x - doc.lastClickX) <= 4 &&
               std::abs(y - doc.lastClickY) <= 4;
    doc.lastClickMs = dbl ? 0 : now;
    doc.lastClickX = x;
    doc.lastClickY = y;
    doc.pressX = x;
    doc.pressY = y;
    doc.pressed = true;
    doc.pressShift = keyDown(VK_SHIFT);
    doc.pressCtrl = keyDown(VK_CONTROL);
    doc.pressAlt = keyDown(VK_MENU);
    // An Alt+drag duplicates; releasing Alt after it is no menu request
    if (doc.pressAlt) app.altClickGuard = true;
    doc.gestureMoved = false;
    doc.hoverUi = 0;
    D2D1_POINT_2F s = toSheet(app, doc, fx, fy);
    doc.pressSX = doc.lastSX = s.x;
    doc.pressSY = doc.lastSY = s.y;
    SetCapture(hwnd);

    if (doc.menuOpen) {
        int hit = menuHitAt(doc, fx, fy);
        std::vector<SheetDoc::MenuItem> items = doc.menu;
        bool inside = inMenuBox(doc, fx, fy);
        closeMenu(doc);
        doc.gesture = SheetDoc::Gesture::None;
        if (hit >= 1000) runAction(app, hwnd, doc, ACT_COLOR_BASE + (hit - 1000));
        else if (hit >= 0 && hit < (int)items.size() && items[hit].enabled) runAction(app, hwnd, doc, items[hit].action);
        (void)inside;
        doc.menuSheetX = doc.menuSheetY = 0;
        invalidate(app);
        return true;
    }
    // Chrome first: tool bar, zoom, tray, find, presentation controls
    int ui = uiHitAt(doc, fx, fy);
    if (ui != 0) {
        if (ui >= ACT_TRAY_ROW && ui < ACT_TRAY_ROW + 100) {
            int row = ui - ACT_TRAY_ROW;
            if (row < (int)doc.trayRows.size()) {
                doc.trayDragFile = doc.trayRows[row].second;
                doc.gesture = SheetDoc::Gesture::TrayDrag;
                return true;
            }
        }
        doc.gesture = SheetDoc::Gesture::None;
        doc.hoverUi = ui;
        return true;  // buttons act on release
    }
    if (doc.minimapRect.right > doc.minimapRect.left && fx >= doc.minimapRect.left && fx <= doc.minimapRect.right &&
        fy >= doc.minimapRect.top && fy <= doc.minimapRect.bottom) {
        doc.gesture = SheetDoc::Gesture::Minimap;
        doc.camX = (fx - doc.minimapOriginX) / doc.minimapScale;
        doc.camY = (fy - doc.minimapOriginY) / doc.minimapScale;
        doc.flight.active = false;
        invalidate(app);
        return true;
    }
    if (doc.presenting) {
        doc.gesture = SheetDoc::Gesture::None;
        runAction(app, hwnd, doc, fx < (v.left + v.right) * 0.33f ? ACT_PRESENT_PREV : ACT_PRESENT_NEXT);
        return true;
    }
    if (doc.trayOpen) doc.trayOpen = false;
    // The text box takes clicks inside it
    if (doc.editing != SheetDoc::Edit::None && doc.editing != SheetDoc::Edit::Find) {
        D2D1_RECT_F rect{};
        float sc = 1;
        if (editRect(app, doc, rect, sc) && fx >= rect.left && fx <= rect.right && fy >= rect.top && fy <= rect.bottom) {
            size_t at = positionAt(app, doc, (fx - rect.left) / sc, (fy - rect.top) / sc + doc.edit.scrollY);
            unsigned long long t = nowMs();
            if (t - doc.edit.lastClickMs < GetDoubleClickTime()) doc.edit.clickCount++;
            else doc.edit.clickCount = 1;
            doc.edit.lastClickMs = t;
            if (doc.edit.clickCount == 2) {
                doc.edit.anchor = wordLeft(doc.edit.text, std::min(at + 1, doc.edit.text.size()));
                doc.edit.caret = wordRight(doc.edit.text, at);
            } else if (doc.edit.clickCount >= 3) {
                doc.edit.anchor = lineStart(doc.edit.text, at);
                doc.edit.caret = lineEnd(doc.edit.text, at);
            } else {
                doc.edit.caret = at;
                if (!doc.pressShift) doc.edit.anchor = at;
            }
            doc.edit.preferredX = -1;
            doc.gesture = SheetDoc::Gesture::EditSelect;
            app.cursorBlinkOn = true;
            invalidate(app);
            return true;
        }
        commitEdit(app, doc);
    } else if (doc.editing == SheetDoc::Edit::Find) {
        // The canvas behind the find bar still works; the query stays
        doc.editing = SheetDoc::Edit::None;
    }
    // Space held down pans (the flag is checked against the key: focus can
    // leave while it is down and the release go elsewhere)
    if (doc.spaceHeld && keyDown(VK_SPACE)) {
        doc.gesture = SheetDoc::Gesture::Pan;
        doc.panCamX = doc.camX;
        doc.panCamY = doc.camY;
        doc.flight.active = false;
        return true;
    }
    // Connect from a dot, resize from an edge
    std::string node;
    Side side;
    if (dotAt(app, doc, fx, fy, node, side)) {
        doc.gesture = SheetDoc::Gesture::Connect;
        doc.pressNode = node;
        doc.connectSide = side;
        doc.connectTarget.clear();
        return true;
    }
    if (int edges = resizeEdgesAt(app, doc, fx, fy, node)) {
        int i = nodeIndex(doc.model, node);
        // A double-click on the bottom edge fits the card to its note
        if (dbl && (edges & 8) && i >= 0) {
            SheetCard& c = card(app, doc, doc.model.nodes[i]);
            if (c.contentHeight > 0) {
                selectOnly(doc, node);
                runAction(app, hwnd, doc, ACT_FIT_HEIGHT);
                doc.gesture = SheetDoc::Gesture::None;
                return true;
            }
        }
        beginStep(doc);
        doc.gesture = SheetDoc::Gesture::Resize;
        doc.pressNode = node;
        doc.resizeEdges = edges;
        doc.resizeStart = boxOf(doc.model.nodes[i]);
        return true;
    }
    float tol = 7.0f / scale(app, doc);
    // A ghost link: a click makes it a real arrow
    if (!doc.hoverGhostFrom.empty()) {
        beginStep(doc);
        addEdge(doc, doc.hoverGhostFrom, doc.hoverGhostTo);
        commitStep(app, doc);
        doc.selectedEdge = doc.model.edges.back().id;
        doc.selected.clear();
        doc.hoverGhostFrom.clear();
        doc.hoverGhostTo.clear();
        doc.gesture = SheetDoc::Gesture::None;
        signalHint(app, SIGI_CHECK, tr(app, "sheet.ghost_made"));
        invalidate(app);
        return true;
    }
    int at = nodeAt(doc, s.x, s.y, false);
    if (at < 0) {
        std::string edgeHit;
        for (const Edge& e : doc.model.edges) {
            if (curveDistance(edgeCurveFor(doc, e), s.x, s.y) < tol) edgeHit = e.id;
        }
        if (!edgeHit.empty()) {
            doc.selected.clear();
            doc.selectedEdge = edgeHit;
            doc.gesture = SheetDoc::Gesture::None;
            if (dbl) startEdit(app, doc, SheetDoc::Edit::EdgeLabel, edgeHit);
            invalidate(app);
            return true;
        }
    }
    doc.selectedEdge.clear();
    if (at >= 0) {
        const Node& n = doc.model.nodes[at];
        doc.pressNode = n.id;
        if (dbl) {
            doc.gesture = SheetDoc::Gesture::None;
            selectOnly(doc, n.id);
            SheetCard& c = card(app, doc, n);
            if (n.type == NodeType::Link) startEdit(app, doc, SheetDoc::Edit::Url, n.id);
            else if (c.kind == SheetCard::Kind::Note) startEdit(app, doc, SheetDoc::Edit::Card, n.id);
            else runAction(app, hwnd, doc, ACT_OPEN_TAB);
            invalidate(app);
            return true;
        }
        // Ctrl+click opens a link or a note, as in Obsidian
        if (doc.pressCtrl && !doc.pressShift && (n.type == NodeType::Link)) {
            openUrl(n.url);
            doc.gesture = SheetDoc::Gesture::None;
            return true;
        }
        doc.pressWasSelected = isSelected(doc, n.id);
        if (doc.pressShift || doc.pressCtrl) {
            if (doc.pressWasSelected) {
                doc.selected.erase(std::remove(doc.selected.begin(), doc.selected.end(), n.id), doc.selected.end());
            } else {
                doc.selected.push_back(n.id);
            }
        } else if (!doc.pressWasSelected) {
            selectOnly(doc, n.id);
        }
        doc.gesture = SheetDoc::Gesture::Pending;
        invalidate(app);
        return true;
    }
    // A group: its label moves it, its body is open ground
    int g = nodeAt(doc, s.x, s.y, true);
    if (g >= 0 && doc.model.nodes[g].type == NodeType::Group) {
        Box label = groupLabelBox(app, doc, doc.model.nodes[g]);
        bool onLabel = s.x >= label.left && s.x <= label.right && s.y >= label.top && s.y <= label.bottom;
        if (onLabel) {
            doc.pressNode = doc.model.nodes[g].id;
            if (dbl) {
                selectOnly(doc, doc.pressNode);
                startEdit(app, doc, SheetDoc::Edit::GroupLabel, doc.pressNode);
                doc.gesture = SheetDoc::Gesture::None;
                return true;
            }
            doc.pressWasSelected = isSelected(doc, doc.pressNode);
            if (doc.pressShift) {
                if (!doc.pressWasSelected) doc.selected.push_back(doc.pressNode);
            } else if (!doc.pressWasSelected) {
                selectOnly(doc, doc.pressNode);
            }
            doc.gesture = SheetDoc::Gesture::Pending;
            invalidate(app);
            return true;
        }
    }
    // Open ground: a double-click writes a note there, a drag selects
    if (dbl) {
        doc.gesture = SheetDoc::Gesture::None;
        newCardAndEdit(app, doc, s.x - 180, s.y - 40);
        return true;
    }
    doc.pressNode = g >= 0 ? doc.model.nodes[g].id : std::string();
    if (!doc.pressShift) {
        doc.selected.clear();
        doc.selectedEdge.clear();
    }
    doc.gesture = SheetDoc::Gesture::Marquee;
    invalidate(app);
    return true;
}

bool sheetMouseMove(App& app, HWND hwnd, int x, int y) {
    SheetDoc* docPtr = activeSheet(app);
    if (!docPtr) return false;
    SheetDoc& doc = *docPtr;
    float fx = (float)x, fy = (float)y;
    D2D1_RECT_F v = viewRect(app);
    bool inView = fx >= v.left && fy >= v.top;
    D2D1_POINT_2F s = toSheet(app, doc, fx, fy);
    float sc = scale(app, doc);
    bool moved = std::abs(x - doc.pressX) > 3 || std::abs(y - doc.pressY) > 3;
    switch (doc.gesture) {
        case SheetDoc::Gesture::None:
            if (doc.menuOpen) {
                int hit = menuHitAt(doc, fx, fy);
                if (hit != doc.menuHover) {
                    doc.menuHover = hit;
                    invalidate(app);
                }
            } else if (inView) {
                updateHover(app, doc, fx, fy);
            } else if (!doc.hoverNode.empty() || doc.hoverUi) {
                doc.hoverNode.clear();
                doc.hoverUi = 0;
                doc.tip.clear();
                invalidate(app);
            }
            sheetSetCursor(app, x, y);
            return inView || doc.menuOpen;
        case SheetDoc::Gesture::Pending:
            if (!moved) return true;
            beginMove(app, doc);
            [[fallthrough]];
        case SheetDoc::Gesture::Move: {
            float dx = s.x - doc.pressSX, dy = s.y - doc.pressSY;
            if (keyDown(VK_SHIFT) && !doc.pressShift) {
                if (std::fabs(dx) > std::fabs(dy)) dy = 0;
                else dx = 0;
            }
            snapMove(app, doc, dx, dy, keyDown(VK_CONTROL));
            for (const auto& entry : doc.moveStart) {
                int i = nodeIndex(doc.model, entry.first);
                if (i < 0) continue;
                doc.model.nodes[i].x = entry.second.left + dx;
                doc.model.nodes[i].y = entry.second.top + dy;
            }
            doc.gestureMoved = true;
            break;
        }
        case SheetDoc::Gesture::Pan:
            doc.camX = doc.panCamX - (fx - doc.pressX) / sc;
            doc.camY = doc.panCamY - (fy - doc.pressY) / sc;
            break;
        case SheetDoc::Gesture::Marquee:
            doc.lastSX = s.x;
            doc.lastSY = s.y;
            doc.gestureMoved = doc.gestureMoved || moved;
            break;
        case SheetDoc::Gesture::Resize: {
            int i = nodeIndex(doc.model, doc.pressNode);
            if (i < 0) break;
            Box b = doc.resizeStart;
            float dx = s.x - doc.pressSX, dy = s.y - doc.pressSY;
            bool free = keyDown(VK_CONTROL);
            auto fit = [&](float v2) { return free ? v2 : snap(v2); };
            if (doc.resizeEdges & 1) b.left = std::min(fit(b.left + dx), b.right - 80);
            if (doc.resizeEdges & 4) b.right = std::max(fit(b.right + dx), b.left + 80);
            if (doc.resizeEdges & 2) b.top = std::min(fit(b.top + dy), b.bottom - 40);
            if (doc.resizeEdges & 8) b.bottom = std::max(fit(b.bottom + dy), b.top + 40);
            Node& n = doc.model.nodes[i];
            n.x = b.left;
            n.y = b.top;
            n.width = b.right - b.left;
            n.height = b.bottom - b.top;
            doc.gestureMoved = true;
            break;
        }
        case SheetDoc::Gesture::Connect: {
            doc.lastSX = s.x;
            doc.lastSY = s.y;
            int at = nodeAt(doc, s.x, s.y, false);
            doc.connectTarget = at >= 0 && doc.model.nodes[at].id != doc.pressNode ? doc.model.nodes[at].id : std::string();
            doc.gestureMoved = true;
            break;
        }
        case SheetDoc::Gesture::Minimap:
            if (doc.minimapScale > 0) {
                doc.camX = (fx - doc.minimapOriginX) / doc.minimapScale;
                doc.camY = (fy - doc.minimapOriginY) / doc.minimapScale;
            }
            break;
        case SheetDoc::Gesture::EditSelect: {
            D2D1_RECT_F rect{};
            float ts = 1;
            if (editRect(app, doc, rect, ts)) {
                doc.edit.caret = positionAt(app, doc, (fx - rect.left) / ts, (fy - rect.top) / ts + doc.edit.scrollY);
            }
            break;
        }
        case SheetDoc::Gesture::TrayDrag:
            doc.gestureMoved = doc.gestureMoved || moved;
            break;
    }
    sheetSetCursor(app, x, y);
    invalidate(app);
    (void)hwnd;
    return true;
}

bool sheetPressed(App& app) {
    SheetDoc* doc = activeSheet(app);
    return doc && doc->pressed;
}

bool sheetMouseUp(App& app, HWND hwnd, int x, int y) {
    SheetDoc* docPtr = activeSheet(app);
    if (!docPtr || !docPtr->pressed) return false;
    SheetDoc& doc = *docPtr;
    doc.pressed = false;
    if (GetCapture() == hwnd) ReleaseCapture();
    float fx = (float)x, fy = (float)y;
    SheetDoc::Gesture gesture = doc.gesture;
    doc.gesture = SheetDoc::Gesture::None;
    doc.guidesX.clear();
    doc.guidesY.clear();
    switch (gesture) {
        case SheetDoc::Gesture::None: {
            int ui = uiHitAt(doc, fx, fy);
            if (ui > 0 && ui == doc.hoverUi) runAction(app, hwnd, doc, ui);
            else if (ui > 0) doc.hoverUi = ui;
            break;
        }
        case SheetDoc::Gesture::Pending:
            // A plain click on a card that was already part of a selection picks it alone
            if (doc.pressWasSelected && !doc.pressShift && !doc.pressCtrl) selectOnly(doc, doc.pressNode);
            break;
        case SheetDoc::Gesture::Move: {
            std::vector<std::string> ids;
            for (const auto& entry : doc.moveStart) ids.push_back(entry.first);
            applyLanes(app, doc, ids);
            doc.moveStart.clear();
            commitStep(app, doc);
            break;
        }
        case SheetDoc::Gesture::Resize:
            if (doc.gestureMoved) {
                auto it = doc.cards.find(doc.pressNode);
                if (it != doc.cards.end()) it->second.bmKey = 0;
                commitStep(app, doc);
            } else if (!doc.undo.empty()) {
                doc.undo.pop_back();
            }
            break;
        case SheetDoc::Gesture::Connect: {
            if (!doc.connectTarget.empty()) {
                beginStep(doc);
                addEdge(doc, doc.pressNode, doc.connectTarget);
                commitStep(app, doc);
            } else if (doc.gestureMoved) {
                // Dropped on open ground: a new note there, joined to the first
                float w = 300, h = 160;
                float cx = doc.lastSX, cy = doc.lastSY;
                float nx = cx - w * 0.5f, ny = cy - h * 0.5f;
                int from = nodeIndex(doc.model, doc.pressNode);
                if (from >= 0) {
                    const Node& f = doc.model.nodes[from];
                    if (cx > f.x + f.width) nx = cx;
                    else if (cx < f.x) nx = cx - w;
                    if (cy > f.y + f.height) ny = cy;
                    else if (cy < f.y) ny = cy - h;
                }
                beginStep(doc);
                std::string id = newNoteCard(app, doc, nx, ny, {}, w, h);
                if (id.empty()) {
                    doc.undo.pop_back();
                } else {
                    addEdge(doc, doc.pressNode, id);
                    commitStep(app, doc);
                    selectOnly(doc, id);
                    startEdit(app, doc, SheetDoc::Edit::Card, id);
                }
            }
            doc.connectTarget.clear();
            break;
        }
        case SheetDoc::Gesture::Marquee: {
            if (!doc.gestureMoved) {
                // A click in a group's open ground picks the group
                if (!doc.pressNode.empty() && !doc.pressShift) selectOnly(doc, doc.pressNode);
                break;
            }
            Box m{std::min(doc.pressSX, doc.lastSX), std::min(doc.pressSY, doc.lastSY), std::max(doc.pressSX, doc.lastSX),
                  std::max(doc.pressSY, doc.lastSY)};
            for (const Node& n : doc.model.nodes) {
                Box b = boxOf(n);
                bool inside = n.type == NodeType::Group
                                  ? b.left >= m.left && b.right <= m.right && b.top >= m.top && b.bottom <= m.bottom
                                  : b.left < m.right && b.right > m.left && b.top < m.bottom && b.bottom > m.top;
                if (inside && !isSelected(doc, n.id)) doc.selected.push_back(n.id);
            }
            break;
        }
        case SheetDoc::Gesture::Minimap:
            sheetSaveView(doc);
            break;
        case SheetDoc::Gesture::TrayDrag: {
            D2D1_RECT_F v = viewRect(app);
            bool onCanvas = fx >= v.left && fy >= v.top &&
                            !(fx >= doc.trayRect.left && fx <= doc.trayRect.right && fy >= doc.trayRect.top &&
                              fy <= doc.trayRect.bottom);
            if (doc.gestureMoved && onCanvas) {
                D2D1_POINT_2F s = toSheet(app, doc, fx, fy);
                addFileCards(app, doc, {doc.trayDragFile}, s.x - 180, s.y - 120);
            } else if (!doc.gestureMoved) {
                D2D1_POINT_2F c = viewCentre(app, doc);
                D2D1_POINT_2F spot = freeSpot(doc, c.x - 180, c.y - 120, 360, 240);
                addFileCards(app, doc, {doc.trayDragFile}, spot.x, spot.y);
            }
            doc.trayDragFile.clear();
            break;
        }
        default:
            break;
    }
    updateHover(app, doc, fx, fy);
    invalidate(app);
    return true;
}

bool sheetMiddleDown(App& app, HWND hwnd, int x, int y) {
    SheetDoc* doc = activeSheet(app);
    if (!doc) return false;
    D2D1_RECT_F v = viewRect(app);
    if (x < v.left || y < v.top) return false;
    doc->gesture = SheetDoc::Gesture::Pan;
    doc->middlePan = true;
    doc->pressed = true;
    doc->pressX = x;
    doc->pressY = y;
    doc->panCamX = doc->camX;
    doc->panCamY = doc->camY;
    doc->flight.active = false;
    SetCapture(hwnd);
    return true;
}

bool sheetMiddleUp(App& app, HWND hwnd, int, int) {
    SheetDoc* doc = activeSheet(app);
    if (!doc || doc->gesture != SheetDoc::Gesture::Pan || !doc->middlePan) return false;
    doc->gesture = SheetDoc::Gesture::None;
    doc->middlePan = false;
    doc->pressed = false;
    if (GetCapture() == hwnd) ReleaseCapture();
    sheetSaveView(*doc);
    return true;
}

bool sheetMouseWheel(App& app, HWND hwnd, float delta, bool horizontal, int x, int y) {
    SheetDoc* docPtr = activeSheet(app);
    if (!docPtr) return false;
    SheetDoc& doc = *docPtr;
    D2D1_RECT_F v = viewRect(app);
    if (x < v.left || y < v.top) return false;
    if (doc.menuOpen) {
        closeMenu(doc);
        invalidate(app);
        return true;
    }
    doc.flight.active = false;
    float s = scale(app, doc);
    bool ctrl = keyDown(VK_CONTROL);
    if (ctrl && !horizontal) {
        // Zoom about the pointer
        D2D1_POINT_2F before = toSheet(app, doc, (float)x, (float)y);
        doc.zoom = std::clamp(doc.zoom * std::pow(1.0018f, delta), 0.05f, 4.0f);
        D2D1_POINT_2F after = toSheet(app, doc, (float)x, (float)y);
        doc.camX += before.x - after.x;
        doc.camY += before.y - after.y;
        doc.zoomChangedMs = nowMs();
        doc.placed = true;
        ensureTimer(app);
        invalidate(app);
        return true;
    }
    // A selected note scrolls inside itself while it has more to show
    if (!horizontal && !keyDown(VK_SHIFT)) {
        D2D1_POINT_2F p = toSheet(app, doc, (float)x, (float)y);
        int at = nodeAt(doc, p.x, p.y, false);
        if (at >= 0 && isSelected(doc, doc.model.nodes[at].id)) {
            SheetCard& c = card(app, doc, doc.model.nodes[at]);
            float innerH = doc.model.nodes[at].height - cardPadding() * 2;
            if (c.kind == SheetCard::Kind::Note && c.contentHeight > innerH + 1) {
                float before = c.scroll;
                c.scroll = std::clamp(c.scroll - delta * 0.6f, 0.0f, c.contentHeight - innerH);
                if (c.scroll != before) {
                    invalidate(app);
                    return true;
                }
            }
        }
    }
    float step = delta * 0.9f / s;
    if (horizontal || keyDown(VK_SHIFT)) doc.camX -= horizontal ? -step : step;
    else doc.camY -= step;
    doc.placed = true;
    invalidate(app);
    (void)hwnd;
    return true;
}

bool sheetContextMenu(App& app, HWND hwnd, int x, int y) {
    SheetDoc* doc = activeSheet(app);
    if (!doc) return false;
    D2D1_RECT_F v = viewRect(app);
    if (x < v.left || y < v.top) return false;
    if (doc->editing != SheetDoc::Edit::None && doc->editing != SheetDoc::Edit::Find) commitEdit(app, *doc);
    if (doc->presenting) return true;
    openMenu(app, *doc, (float)x, (float)y);
    (void)hwnd;
    return true;
}

bool sheetDropFiles(App& app, HWND hwnd, const std::vector<std::wstring>& files, int x, int y) {
    SheetDoc* doc = activeSheet(app);
    if (!doc) return false;
    D2D1_POINT_2F s = toSheet(app, *doc, (float)x, (float)y);
    addFileCards(app, *doc, files, s.x - 180, s.y - 120);
    (void)hwnd;
    invalidate(app);
    return true;
}

void sheetCaptureLost(App& app, HWND hwnd) {
    SheetDoc* doc = activeSheet(app);
    if (!doc) return;
    if (doc->gesture == SheetDoc::Gesture::None) {
        doc->pressed = false;
        return;
    }
    if (doc->gesture == SheetDoc::Gesture::Move || doc->gesture == SheetDoc::Gesture::Resize) {
        // Put things back as they were before the press
        if (!doc->undo.empty()) {
            doc->model = doc->undo.back().model;
            doc->undo.pop_back();
        }
    }
    doc->gesture = SheetDoc::Gesture::None;
    doc->pressed = false;
    doc->middlePan = false;
    doc->guidesX.clear();
    doc->guidesY.clear();
    InvalidateRect(hwnd, nullptr, FALSE);
}
