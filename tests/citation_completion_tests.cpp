// Citation completion (docs/citations.md): [@ and an @ that starts a word
// list the bibliography's keys in Live and in Source, and @fig:, @tbl:,
// @eq: and @sec: the document's own ids, typed through the real key and
// character handlers. The rail's Cite button asks Zotero's picker through
// Better BibTeX, here a small HTTP server on this machine, and types [@
// without it. Frames of the open lists are saved beside the test.
#include <winsock2.h>  // before windows.h, which app.h brings

#include "d2d_init.h"
#include "editor.h"
#include "editrail.h"
#include "frame_preview.h"
#include "i18n.h"
#include "input.h"
#include "keystroke.h"
#include "live_editor.h"
#include "live_text.h"
#include "render.h"
#include "scholar.h"
#include "settings.h"
#include "tabs.h"
#include "utils.h"
#include "zotero.h"

#include <atomic>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

namespace {
namespace fs = std::filesystem;

int failures = 0, checks = 0;
void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) {
        std::cerr << "FAIL citation completion: " << message << '\n';
        ++failures;
    }
}

std::string narrow(const std::wstring& s) { return toUtf8(s); }

// The rows a list offers, as "key key key"
std::string keys(const std::vector<LiveText::Choice>& choices) {
    std::string out;
    for (const auto& c : choices) out += (out.empty() ? "" : " ") + narrow(c.label);
    return out;
}
std::string keys(const std::vector<scholar::Suggestion>& rows) {
    std::string out;
    for (const auto& r : rows) out += (out.empty() ? "" : " ") + narrow(r.key);
    return out;
}

const LiveText& sourceList(const App& app) { return editorCompletion(app); }
const LiveText& liveList(const App& app) { return liveText(app); }
bool listOpen(const LiveText& list) { return list.popup == LiveText::Popup::Cite && !list.choices.empty(); }

// --- Better BibTeX, played by a server on this machine ----------------------

// "ready" to the probe, and to the picker what the test sets (empty: the
// writer cancelled). With `hold` the picker waits, as a writer choosing does
struct FakeZotero {
    SOCKET listener = INVALID_SOCKET;
    int port = 0;
    std::thread thread;
    std::mutex lock;
    std::string picked;
    std::atomic<bool> hold{false};
    std::atomic<int> probes{0}, picks{0};
    std::atomic<int> pickStatus{200};  // anything else: the picker failed

    bool start() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data)) return false;
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;  // any free port
        int length = sizeof addr;
        if (listener == INVALID_SOCKET || bind(listener, (sockaddr*)&addr, sizeof addr) || listen(listener, 4) ||
            getsockname(listener, (sockaddr*)&addr, &length)) {
            return false;
        }
        port = ntohs(addr.sin_port);
        thread = std::thread([this] { serve(); });
        return true;
    }
    void answer(const std::string& citation) {
        std::lock_guard<std::mutex> guard(lock);
        picked = citation;
    }
    void serve() {
        for (;;) {
            SOCKET client = accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) return;
            std::string request;
            char buffer[1024];
            while (request.find("\r\n\r\n") == std::string::npos) {
                const int got = recv(client, buffer, sizeof buffer, 0);
                if (got <= 0) break;
                request.append(buffer, (size_t)got);
            }
            std::string body;
            int status = 404;
            if (request.rfind("GET /better-bibtex/cayw?probe=true ", 0) == 0) {
                ++probes;
                status = 200;
                body = "ready";
            } else if (request.rfind("GET /better-bibtex/cayw?format=pandoc&brackets=1 ", 0) == 0) {
                ++picks;
                while (hold) Sleep(5);
                std::lock_guard<std::mutex> guard(lock);
                status = pickStatus;
                if (status == 200) body = picked;
            }
            const std::string response = "HTTP/1.1 " + std::to_string(status) + (status == 200 ? " OK" : " Error") +
                                         "\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " +
                                         std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            send(client, response.data(), (int)response.size(), 0);
            shutdown(client, SD_SEND);
            closesocket(client);
        }
    }
    void stop() {
        hold = false;
        if (listener != INVALID_SOCKET) closesocket(listener);
        listener = INVALID_SOCKET;
        if (thread.joinable()) thread.join();
        WSACleanup();
    }
};

// A port nothing listens on: Zotero not running
int deadPort() {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int length = sizeof addr;
    bind(s, (sockaddr*)&addr, sizeof addr);
    getsockname(s, (sockaddr*)&addr, &length);
    closesocket(s);
    return ntohs(addr.sin_port);
}

// The worker's answer to a Cite click, handled as the window procedure
// handles WM_APP_ZOTERO
bool citeAnswered(App& app, DWORD waitMs = 5000) {
    MSG msg{};
    for (const ULONGLONG start = GetTickCount64(); GetTickCount64() - start < waitMs;) {
        if (PeekMessageW(&msg, app.hwnd, WM_APP_ZOTERO, WM_APP_ZOTERO, PM_REMOVE)) {
            editorCiteAnswer(app, app.hwnd, msg.wParam, msg.lParam);
            return true;
        }
        Sleep(5);
    }
    return false;
}

// --- The window --------------------------------------------------------------

// What the window draws in Live: the page, the strip, the rail
void drawLive(App& app) {
    app.renderTarget->Clear(app.theme.background);
    D2D1_MATRIX_3X2_F saved;
    app.renderTarget->GetTransform(&saved);
    app.renderTarget->SetTransform(D2D1::Matrix3x2F::Translation(documentViewportX(app), 0) * saved);
    renderLivePage(app);
    app.renderTarget->SetTransform(saved);
    renderTabStrip(app);
    app.editRailAnim = 1.0f;
    app.editRailHover = 0;
    renderEditRail(app);
}

// What it draws over Source: the rail and the citation list
void drawSourceExtras(App& app) {
    app.editRailAnim = 1.0f;
    app.editRailHover = 0;
    renderEditRail(app);
    renderEditorCompletion(app);
}

// A frame as the window paints it before the next press: rows and buttons
// where the pointer finds them
void paint(App& app) {
    if (app.layoutDirty) ensureLayoutComplete(app);
    app.renderTarget->BeginDraw();
    if (editorLiveOn(app)) {
        drawLive(app);
    } else {
        app.renderTarget->Clear(editDeskColor(app));
        renderEditor(app, editorPaneWidth(app));
        drawSourceExtras(app);
    }
    app.renderTarget->EndDraw();
}

void frame(App& app, const fs::path& path) {
    if (app.layoutDirty) ensureLayoutComplete(app);
    const bool saved = editorLiveOn(app) ? saveFramePreview(app, path, [&] { drawLive(app); })
                                         : saveFramePreview(app, path, [&] { drawSourceExtras(app); });
    check(saved, "frame saved: " + path.filename().string());
}

LPARAM at(float x, float y) { return MAKELPARAM((int)std::lround(x), (int)std::lround(y)); }

void click(App& app, float x, float y) {
    paint(app);
    handleMouseMove(app, app.hwnd, at(x, y));
    handleMouseDown(app, app.hwnd, MK_LBUTTON, at(x, y));
    handleMouseUp(app, app.hwnd, 0, at(x, y));
}

// A row of the open list, clicked where it is drawn
bool clickRow(App& app, int index) {
    paint(app);
    const LiveText& list = editorLiveOn(app) ? liveList(app) : sourceList(app);
    // Live's rows are in the page's coordinates, beside the rail
    const float dx = editorLiveOn(app) ? documentViewportX(app) : 0.0f;
    for (const auto& hit : list.popupHits) {
        if (hit.second != index) continue;
        click(app, dx + (hit.first.left + hit.first.right) * 0.5f, (hit.first.top + hit.first.bottom) * 0.5f);
        return true;
    }
    return false;
}

// The rail's Cite button, clicked
bool clickCite(App& app) {
    paint(app);
    for (const auto& hit : app.editRailHits) {
        if (hit.second != 24) continue;
        click(app, (hit.first.left + hit.first.right) * 0.5f, (hit.first.top + hit.first.bottom) * 0.5f);
        return true;
    }
    return false;
}

void placeCaret(App& app, size_t pos) {
    app.editorCursorPos = app.editorSelStart = app.editorSelEnd = pos;
    app.editorHasSelection = false;
    paint(app);
}

// The end of the line holding `what`
size_t endOf(const App& app, const std::wstring& what) {
    size_t at = app.editorText.find(what);
    if (at == std::wstring::npos) return app.editorText.size();
    size_t end = app.editorText.find(L'\n', at);
    return end == std::wstring::npos ? app.editorText.size() : end;
}

// The line the caret is on, for messages
std::string caretLine(const App& app) {
    size_t a = app.editorText.rfind(L'\n', app.editorCursorPos ? app.editorCursorPos - 1 : 0);
    a = a == std::wstring::npos ? 0 : a + 1;
    size_t b = app.editorText.find(L'\n', app.editorCursorPos);
    std::wstring line = app.editorText.substr(a, (b == std::wstring::npos ? app.editorText.size() : b) - a);
    line.insert(std::min(app.editorCursorPos - a, line.size()), L"|");
    return narrow(line);
}

bool before(const App& app, const std::wstring& text) {
    return app.editorCursorPos >= text.size() && app.editorText.compare(app.editorCursorPos - text.size(), text.size(), text) == 0;
}

void openInEditor(App& app, const fs::path& path, bool live) {
    app.editorDirty = false;
    exitEditMode(app);
    check(openDocumentInViewer(app, path.wstring()), "opens: " + path.filename().string());
    enterEditMode(app);
    setEditorLive(app, app.hwnd, live);
    check(app.editMode && editorLiveOn(app) == live, path.filename().string() + (live ? " in Live" : " in Source"));
}

// --- The lists themselves -------------------------------------------------------

void suggestions(App& app, const fs::path& fixtures) {
    openInEditor(app, fixtures / "completion.md", false);
    const std::wstring folder = fixtures.wstring();
    const std::wstring& text = app.editorText;
    // What is typed after an @ at the end of the document
    auto rows = [&](const std::wstring& typed) {
        std::wstring t = text + L"\nSee [@" + typed;
        return scholar::suggest(t, t.size() - typed.size(), t.size(), folder);
    };
    const auto all = rows(L"");
    check(all.size() == 30, "[@ lists the library's thirty keys: " + std::to_string(all.size()));
    check(!all.empty() && narrow(all[0].key) == "brooks1995", "in the order of their keys: " + keys(all));
    const auto knuth = rows(L"knu");
    check(keys(knuth) == "knuth1984", "a key's start finds it: " + keys(knuth));
    check(!knuth.empty() && narrow(knuth[0].detail) == "Knuth 1984, Literate Programming",
          "its row says author, year and title: " + (knuth.empty() ? std::string() : narrow(knuth[0].detail)));
    const auto okafor = rows(L"okafor");
    check(keys(okafor) == "okafor2021 okafor2025 garcia2022 lindqvist2023",
          "keys first, then the works Okafor wrote with others: " + keys(okafor));
    const auto muller = rows(L"Muller");
    check(keys(muller) == "consortium2022 garcia2022", "authors match without case or accents: " + keys(muller));
    for (const auto& row : muller) {
        if (narrow(row.key) == "garcia2022")
            check(narrow(row.detail) == "Garc\xC3\xAD" "a et al. 2022, Ink Pigments in Sepia officinalis",
                  "three authors read et al., markup dropped: " + narrow(row.detail));
        if (narrow(row.key) == "consortium2022")
            check(narrow(row.detail) == "Abe et al. 2022, The Ink Atlas: Viscosity of 1,200\xE2\x80\xA6",
                  "a long title is shortened at a word: " + narrow(row.detail));
    }
    const auto lopez = rows(L"lopez");
    check(keys(lopez) == "consortium2022", "L\xC3\xB3pez is found as lopez: " + keys(lopez));
    const auto year = rows(L"2024");
    check(keys(year) == "doe2024 doe2024b inkconf2024 macfarlane2024 rcore2024", "a year finds its works: " + keys(year));
    const auto both = rows(L"knuth1984");
    check(keys(both) == "knuth1984", "the whole key finds it alone: " + keys(both));
    const auto written = rows(L"doe2");
    check(keys(written) == "doe2024 doe2024b", "author and year typed together: " + keys(written));
    const auto title = rows(L"mythical");
    check(keys(title) == "brooks1995", "a title's word finds its work: " + keys(title));
    for (const auto& row : rows(L"vanros")) {
        check(narrow(row.detail) == "van Rossum & Drake 2009, Python 3 Reference Manual",
              "two authors are named both, a particle kept: " + narrow(row.detail));
    }
    const auto rossum = rows(L"rossum");
    check(keys(rossum) == "vanrossum2009", "a family name after its particle finds it too: " + keys(rossum));
    const auto chinese = rows(L"\x5F20");
    check(keys(chinese) == "zhang2020", "a Chinese author's name finds the work: " + keys(chinese));
    check(rows(L"zzz").empty(), "nothing matches nothing");

    // The document's own ids after fig:, tbl:, eq: and sec:
    const auto figures = rows(L"fig:");
    check(keys(figures) == "fig:nib fig:bars", "fig: lists the figures in order: " + keys(figures));
    check(figures.size() == 2 && narrow(figures[0].detail) == "The nib, split along its slit",
          "a figure's row has its caption");
    check(keys(rows(L"fig:ba")) == "fig:bars", "and narrows as the id is typed");
    check(keys(rows(L"fig:b")) == "fig:bars fig:nib", "an id starting with what was typed comes before one holding it");
    check(keys(rows(L"Fig:")) == "Fig:nib Fig:bars", "@Fig: keeps its capital");
    const auto tables = rows(L"tbl:");
    check(keys(tables) == "tbl:inks" && narrow(tables[0].detail) == "Inks measured, by density",
          "tbl: lists the tables with their captions: " + keys(tables));
    check(keys(rows(L"eq:")) == "eq:flow eq:rise", "eq: lists {#eq:} and \\label{} equations: " + keys(rows(L"eq:")));
    const auto sections = rows(L"sec:");
    check(keys(sections) == "sec:intro sec:figures sec:equations" && narrow(sections[0].detail) == "Citing as you type",
          "sec: lists the sections by their headings: " + keys(sections));

    // A document without a bibliography lists only its own ids
    openInEditor(app, fixtures / "completion-plain.md", false);
    const std::wstring plain = app.editorText + L"\n@";
    check(scholar::suggest(plain, plain.size(), plain.size(), folder).empty(), "no bibliography: @ lists nothing");
    const std::wstring figure = app.editorText + L"\n@fig:";
    check(keys(scholar::suggest(figure, figure.size() - 4, figure.size(), folder)) == "fig:alone",
          "but @fig: still lists the figures");
}

// Where an @ starts a key, and what a pick writes
void triggers() {
    size_t at = 0;
    auto opens = [&](const std::wstring& text) { return live::citeAt(text, text.size(), at); };
    check(opens(L"[@") && at == 1, "[@ starts a key");
    check(opens(L"See [@do") && at == 5, "and so does [@ after words");
    check(opens(L"@do"), "@ at a line's start");
    check(opens(L"As @do"), "@ after a space");
    check(opens(L"[@a; @b"), "@ after a semicolon");
    check(opens(L"[-@do"), "[-@ leaves the author out and still completes");
    check(opens(L"> @do"), "@ in a quote");
    check(!opens(L"ana@example"), "not an e-mail address");
    check(!opens(L"x@"), "not an @ inside a word");
    check(!opens(L"a.@b"), "not after punctuation");
    check(!opens(L"`code [@"), "not in inline code");
    check(!opens(L"```\ncode @"), "not in a fenced block");
    check(!opens(L"[@ "), "a space ends the key");
    check(!opens(L"[@-x"), "a key starts with a letter, a digit or _");

    size_t after = 0;
    std::wstring text = L"See [@do";
    live::MarkEdit edit = live::citeEdit(text, text.size(), 6, L"doe2024", after);
    text.replace(edit.at, edit.removed, edit.with);
    check(text == L"See [@doe2024]" && after == text.size(), "a pick in brackets closes them");
    text = L"See [@do, p. 4]";
    edit = live::citeEdit(text, 8, 6, L"doe2024", after);
    text.replace(edit.at, edit.removed, edit.with);
    check(text == L"See [@doe2024, p. 4]" && after == 13, "and adds no ] when one follows");
    text = L"As @do says";
    edit = live::citeEdit(text, 6, 4, L"doe2024", after);
    text.replace(edit.at, edit.removed, edit.with);
    check(text == L"As @doe2024 says" && after == 11, "in running text the key goes in alone");
    text = L"[@kn|1984].";
    text.erase(4, 1);
    edit = live::citeEdit(text, 4, 2, L"knuth1984", after);
    text.replace(edit.at, edit.removed, edit.with);
    check(text == L"[@knuth1984]." && after == 11, "the rest of a key after the caret is replaced too");
    text = L"[@a; @b";
    edit = live::citeEdit(text, text.size(), 6, L"brooks1995", after);
    text.replace(edit.at, edit.removed, edit.with);
    check(text == L"[@a; @brooks1995]", "a second key in brackets closes them too");
}

// --- Typing in Live and Source ----------------------------------------------------

void typing(App& app, const fs::path& fixtures, const fs::path& out, bool live) {
    const std::string where = live ? " (Live)" : " (Source)";
    const std::string prefix = live ? "live-" : "source-";
    openInEditor(app, fixtures / "completion.md", live);
    auto list = [&]() -> const LiveText& { return live ? liveList(app) : sourceList(app); };
    placeCaret(app, endOf(app, L"The last paragraph."));
    const size_t lines = app.editorLineStarts.size();

    // [@ opens the list of every key
    typeKeys(app, L" [@");
    check(listOpen(list()) && list().choices.size() == 30, "[@ opens the library's keys" + where);
    paint(app);
    frame(app, out / (prefix + "cite-open.png"));
    // Typing narrows it; Enter puts the key in and closes the bracket
    typeKeys(app, L"knu");
    check(keys(list().choices) == "knuth1984", "typing narrows it to Knuth" + where + ": " + keys(list().choices));
    keystroke(app, VK_RETURN);
    check(before(app, L"The last paragraph. [@knuth1984]"), "Enter puts the key in with its ]" + where + ": " + caretLine(app));
    check(!listOpen(list()) && app.editorLineStarts.size() == lines, "and no new line" + where);

    // @ after a space, filtered by author; Esc closes it and the typing stays
    typeKeys(app, L" @oka");
    check(listOpen(list()) && keys(list().choices) == "okafor2021 okafor2025 garcia2022 lindqvist2023",
          "@ after a space lists keys and authors" + where + ": " + keys(list().choices));
    paint(app);
    frame(app, out / (prefix + "cite-author.png"));
    keystroke(app, VK_ESCAPE);
    check(!listOpen(list()) && before(app, L" @oka") && app.editMode && !app.escPressedOnce,
          "Esc closes the list and nothing else" + where);
    typeKeys(app, L"f");
    check(!listOpen(list()), "typing on after Esc keeps it closed" + where);

    // Not in an address or a word
    typeKeys(app, L" ana@ex");
    check(!listOpen(list()), "an e-mail address lists nothing" + where);
    typeKeys(app, L" x@");
    check(!listOpen(list()), "nor an @ inside a word" + where);

    // A year; Tab picks as Enter does
    typeKeys(app, L" [@2017");
    check(listOpen(list()) && keys(list().choices) == "vaswani2017", "a year finds its work" + where + ": " + keys(list().choices));
    keystroke(app, VK_TAB);
    check(before(app, L" [@vaswani2017]"), "Tab puts it in" + where + ": " + caretLine(app));

    // The arrows choose; @fig: lists the figures
    typeKeys(app, L" @fig:");
    check(listOpen(list()) && keys(list().choices) == "fig:nib fig:bars", "@fig: lists the figures" + where);
    paint(app);
    frame(app, out / (prefix + "cite-figures.png"));
    keystroke(app, VK_DOWN);
    keystroke(app, VK_RETURN);
    check(before(app, L" @fig:bars"), "Down and Enter take the second" + where + ": " + caretLine(app));

    // The pointer picks a row
    typeKeys(app, L" [@smi");
    check(listOpen(list()) && keys(list().choices) == "smith2019 smith2020 inkconf2024",
          "Smith's works, and the proceedings she edited" + where + ": " + keys(list().choices));
    check(clickRow(app, 1) && before(app, L" [@smith2020]"), "a click on a row puts its key in" + where + ": " + caretLine(app));
    check(!listOpen(list()), "and closes the list" + where);

    // Moving the caret away closes it; moving back does not open it
    typeKeys(app, L" @doe");
    check(listOpen(list()), "the list is open again" + where);
    keystroke(app, VK_HOME);
    check(!listOpen(list()), "Home leaves the key and closes the list" + where);
    keystroke(app, VK_END);
    check(!listOpen(list()), "End comes back without opening it" + where);

    // Not in code
    placeCaret(app, endOf(app, L"def cite(key):"));
    typeKeys(app, L" [@");
    check(!listOpen(list()), "nothing in a code block" + where);

    // At the narrow width and in a dark theme
    placeCaret(app, endOf(app, L"The last paragraph."));
    typeKeys(app, L" [@g");
    app.width = 650;
    app.layoutDirty = true;
    app.editorRowMetricsWidth = -1.0f;
    if (!live) editorRevealCaret(app);  // as a resize leaves it, the caret in view
    paint(app);
    check(listOpen(list()), "the list stays open at 650 px" + where);
    frame(app, out / (prefix + "cite-650.png"));
    applyTheme(app, 5);
    app.layoutDirty = true;
    paint(app);
    frame(app, out / (prefix + "cite-dark.png"));
    D2D1_RECT_F box{1e9f, 1e9f, -1e9f, -1e9f};
    for (const auto& hit : list().popupHits) {
        box.left = std::min(box.left, hit.first.left);
        box.right = std::max(box.right, hit.first.right);
    }
    const float dx = live ? documentViewportX(app) : 0.0f;
    check(box.right + dx <= (float)app.width && box.left + dx >= editRailWidth(app),
          "the list stays in the window at 650 px" + where);
    applyTheme(app, 0);
    app.width = 1050;
    app.layoutDirty = true;
    keystroke(app, VK_ESCAPE);
    app.editorDirty = false;
}

// A document without a bibliography: only its own ids
void withoutBibliography(App& app, const fs::path& fixtures, bool live) {
    const std::string where = live ? " (Live)" : " (Source)";
    openInEditor(app, fixtures / "completion-plain.md", live);
    auto list = [&]() -> const LiveText& { return live ? liveList(app) : sourceList(app); };
    placeCaret(app, endOf(app, L"The last paragraph."));
    typeKeys(app, L" @oct");
    check(!listOpen(list()), "no bibliography: @ lists nothing" + where);
    typeKeys(app, L" [@");
    check(!listOpen(list()), "nor does [@" + where);
    keystroke(app, VK_BACK);
    keystroke(app, VK_BACK);
    typeKeys(app, L"@tbl:");
    check(listOpen(list()) && keys(list().choices) == "tbl:alone", "@tbl: lists the note's own table" + where);
    keystroke(app, VK_RETURN);
    check(before(app, L" @tbl:alone"), "and puts it in" + where + ": " + caretLine(app));
    app.editorDirty = false;
}

// --- The Cite button and Zotero ------------------------------------------------------

void zoteroPicker(App& app, const fs::path& fixtures, const fs::path& out) {
    FakeZotero server;
    check(server.start(), "the stand-in for Better BibTeX listens");
    zotero::setBaseUrl(L"http://127.0.0.1:" + std::to_wstring(server.port));
    check(zotero::ready(), "its probe answers ready");
    check(server.probes == 1, "one probe reached it");

    for (bool live : {false, true}) {
        const std::string where = live ? " (Live)" : " (Source)";
        openInEditor(app, fixtures / "completion.md", live);
        placeCaret(app, endOf(app, L"The last paragraph."));
        typeKeys(app, L" ");
        // A pick goes in at the caret
        server.answer("[@doe2024; @smith2020]");
        const int picks = server.picks;
        check(clickCite(app), "the rail has a Cite button" + where);
        check(citeAnswered(app) && server.picks == picks + 1, "the picker was asked" + where);
        check(before(app, L"The last paragraph. [@doe2024; @smith2020]"),
              "what Zotero picked goes in at the caret" + where + ": " + caretLine(app));
        // Cancelled, nothing does
        const std::wstring was = app.editorText;
        server.answer("");
        check(clickCite(app) && citeAnswered(app), "the picker is asked again" + where);
        check(app.editorText == was, "a cancelled pick changes nothing" + where);
        app.editorDirty = false;
    }

    // The answer goes to the document that asked, though another is in front
    openInEditor(app, fixtures / "completion.md", false);
    placeCaret(app, endOf(app, L"The last paragraph."));
    const int asking = app.tabs.empty() ? -1 : app.tabs[app.activeTab].id;
    const size_t caret = app.editorCursorPos;
    server.answer("[@hart2023]");
    server.hold = true;
    check(clickCite(app), "Cite while the writer picks");
    tabOpenQuickNote(app, app.hwnd);
    check(app.tabs.size() >= 2 && app.tabs[app.activeTab].id != asking, "another tab comes to the front");
    server.hold = false;
    check(citeAnswered(app), "the pick arrives");
    bool parked = false;
    for (const auto& tab : app.tabs) {
        if (tab.id != asking) continue;
        parked = tab.editorText.compare(caret, 11, L"[@hart2023]") == 0 && tab.editorCursor == caret + 11 && tab.editorDirty;
    }
    check(parked, "it goes in at the asking document's caret, in its tab");
    check(app.editorText.empty(), "and not into the note in front");
    for (int i = 0; i < (int)app.tabs.size(); ++i) {
        if (app.tabs[i].id == asking) tabActivate(app, app.hwnd, i);
    }
    check(app.editorText.find(L"The last paragraph.[@hart2023]") != std::wstring::npos,
          "the tab brings it back to the editor");
    // A picker that fails says so, and puts nothing in
    const std::wstring kept = app.editorText;
    app.signalChips.clear();
    server.pickStatus = 500;
    check(clickCite(app) && citeAnswered(app), "the picker fails");
    bool warned = false;
    for (const auto& chip : app.signalChips) warned = warned || chip.text == tr(app, "toast.cite_failed");
    check(warned && app.editorText == kept && !app.citeWaiting, "a notice says Zotero did not answer, and the text stays");
    server.pickStatus = 200;
    app.editorDirty = false;
    server.stop();

    // Without Zotero the button types [@ and opens the list
    zotero::setBaseUrl(L"http://127.0.0.1:" + std::to_wstring(deadPort()));
    // Windows tries a refused connection for two seconds; the probe does not wait for it
    const ULONGLONG probing = GetTickCount64();
    check(!zotero::ready(), "no answer where nothing listens");
    const ULONGLONG probed = GetTickCount64() - probing;
    check(probed < 1000, "the probe gives up in about 300 ms: " + std::to_string(probed) + " ms");
    for (bool live : {false, true}) {
        const std::string where = live ? " (Live)" : " (Source)";
        openInEditor(app, fixtures / "completion.md", live);
        placeCaret(app, endOf(app, L"The last paragraph."));
        typeKeys(app, L" ");
        const ULONGLONG start = GetTickCount64();
        check(clickCite(app) && citeAnswered(app), "Cite without Zotero" + where);
        check(GetTickCount64() - start < 1000, "[@ comes within a second" + where);
        const LiveText& list = live ? liveList(app) : sourceList(app);
        check(before(app, L"The last paragraph. [@") && listOpen(list) && list.choices.size() == 30,
              "[@ is typed and the list opens" + where + ": " + caretLine(app));
        paint(app);
        frame(app, out / (std::string(live ? "live" : "source") + "-cite-button.png"));
        keystroke(app, VK_ESCAPE);
        app.editorDirty = false;
    }
    // Nor a bibliography: [@ alone, and a word on what to do
    openInEditor(app, fixtures / "completion-plain.md", false);
    placeCaret(app, endOf(app, L"The last paragraph."));
    app.signalChips.clear();
    check(clickCite(app) && citeAnswered(app), "Cite without Zotero or a bibliography");
    bool told = false;
    for (const auto& chip : app.signalChips) told = told || chip.text == tr(app, "toast.cite_none");
    check(before(app, L"The last paragraph.[@") && !listOpen(sourceList(app)) && told,
          "types [@, opens no list, and says what would: " + caretLine(app));
    app.editorDirty = false;
}

}  // namespace

int runCitationCompletionTests(const char* fixtureDir) {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Citation completion tests", WS_POPUP, 0, 0, 1050, 800, nullptr, nullptr,
                               nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 800;
    applyTheme(app, 0);
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);
    tabsInit(app);

    const fs::path fixtures = fixtureDir;
    const fs::path out = fs::current_path() / "frames";
    std::error_code ec;
    fs::create_directories(out, ec);

    triggers();
    suggestions(app, fixtures);
    for (bool live : {true, false}) typing(app, fixtures, out, live);
    for (bool live : {false, true}) withoutBibliography(app, fixtures, live);
    zoteroPicker(app, fixtures, out);

    app.editorDirty = false;
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    std::cout << "Citation completion: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
