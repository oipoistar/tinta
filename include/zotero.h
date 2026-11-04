#pragma once
// Zotero's citation picker, through Better BibTeX (docs/citations.md). Its
// Cite As You Write endpoint answers a probe with "ready" while Zotero runs,
// and opens the picker, answering with the citation in pandoc's syntax.
#include <windows.h>

#include <string>

namespace zotero {

// Where Better BibTeX listens: http://127.0.0.1:23119, unless a test points
// it at a server of its own
void setBaseUrl(const std::wstring& url);
std::wstring baseUrl();

// The picker is there to ask: the probe answered "ready" within `timeoutMs`
bool ready(int timeoutMs = 300);
// Opens the picker and waits for the writer: the citation picked, as
// "[@doe2024; @smith2020]", or empty when the picker was cancelled. False
// when Zotero did not answer. It blocks: call it off the window's thread
bool pick(std::string& citation);

// What came of a Cite click, posted to the window as WM_APP_ZOTERO with the
// request's number (WPARAM) and an Answer* (LPARAM) the handler deletes
struct Answer {
    enum Kind { Absent, Picked, Failed } kind = Absent;
    std::string citation;  // Picked: UTF-8, empty when cancelled
};
// Probes and, with Zotero there, picks, on a thread of its own
void ask(HWND hwnd, unsigned request);

}  // namespace zotero
