#ifndef TINTA_INPUT_H
#define TINTA_INPUT_H

#include "app.h"
#include <windows.h>
#include <functional>
#include <string>

void handleMouseWheel(App& app, HWND hwnd, WPARAM wParam, LPARAM lParam);
void handleMouseHWheel(App& app, HWND hwnd, WPARAM wParam, LPARAM lParam);
void handleMouseMove(App& app, HWND hwnd, LPARAM lParam);
void handleMouseDown(App& app, HWND hwnd, WPARAM wParam, LPARAM lParam);
void handleMouseUp(App& app, HWND hwnd, WPARAM wParam, LPARAM lParam);
void cancelDocumentScrollbarDrag(App& app, HWND hwnd);
// The document's scrollbars below the tab strip, for a page that takes the
// pointer itself (Live): under the pointer they light up and show the
// arrow, and a press takes them as in the reader. False off them (#307)
bool documentScrollbarHover(App& app, HWND hwnd, int x, int y);
bool documentScrollbarPress(App& app, HWND hwnd, int x, int y);
void cancelAppMenuPress(App& app, HWND hwnd);
// True consumes this keystroke's character translation as well (#195).
bool handleKeyDown(App& app, HWND hwnd, WPARAM wParam);
void handleContextMenu(App& app, HWND hwnd, LPARAM lParam);
void handleCharInput(App& app, HWND hwnd, WPARAM wParam);
// Opens what a link points at as a click on it in the page would: a note
// (wiki: or a file reference) as a tab, a heading, an address
void followDocumentLink(App& app, HWND hwnd, const std::string& url);
void handleDropFiles(App& app, HWND hwnd, WPARAM wParam);
// Load a document into the viewer, saving the outgoing reading position
// and restoring the incoming one (shared with tabs.cpp)
bool openDocumentInViewer(App& app, const std::wstring& fullPath);
// Same document setup for a tab's cached source when its file disappeared.
bool openDocumentSourceInViewer(App& app, const std::wstring& fullPath,
                                const std::string& source);
// Reading position memory (#77): the place the document is read at, which
// is its saved place while the layout has not reached that yet
float readingPlace(const App& app);
// Saves the current document's place as the view leaves it, for another
// file or for a note, the start page or an untitled tab (#279)
void persistLeavingPosition(App& app);
// Mouse XBUTTON / Alt+arrow navigation across link jumps (viewer)
void navigateBack(App& app, HWND hwnd);
void navigateForward(App& app, HWND hwnd);
// First clipboard line as text (shared by the search and path inputs)
std::wstring clipboardLine(HWND hwnd);
void handleFileWatchTimer(App& app, HWND hwnd);

// What opening an address written in a document means. A web address opens.
// A file opens with its own application, a program, a script or a file on a
// server the document chose only after the user is asked, and a name alone
// is a file beside the document. Any other program's address is asked about.
enum class LinkKind { None, Web, Path, Address };
struct LinkOpening {
    LinkKind kind = LinkKind::None;
    std::wstring target;   // what the shell is given: the address, or the file's full path
    bool ask = false;      // opened only after the user says so
    bool remote = false;   // a path on a server the document may not pick: not looked for
};
LinkOpening linkOpening(const std::string& written, const std::wstring& documentFolder);
LinkOpening pathOpening(const std::wstring& fullPath, const std::wstring& documentFolder);
// Opens it, or asks first in the dialog a missing file's link asks in
void openFromDocument(App& app, HWND hwnd, const std::string& address, const std::wstring& documentFolder);
void openFileFromDocument(App& app, HWND hwnd, const std::wstring& fullPath, const std::wstring& documentFolder);
// Tests take what would go to the shell
std::function<void(const std::wstring&)>& shellOpenHook();
// themes.ini edited while Tinta runs (#310): the themes load again, and the
// one in use, found by its name, shows the edit at once
void reloadThemesIniIfChanged(App& app, HWND hwnd);
void handleSelectScrollTimer(App& app, HWND hwnd);
// Dwell timer fired over a local .md link: load the peek preview
void handleLinkPeekTimer(App& app, HWND hwnd);

#endif // TINTA_INPUT_H
