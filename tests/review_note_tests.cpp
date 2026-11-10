// Review notes (annotations.h) written after the text changed. A note is a
// comment in the Markdown, and its place in the text was kept from the last
// time the notes were read. Saving from the editor replaced the text without
// reading them again, so deleting or changing a note afterwards wrote to
// where its comment had been: other words went, and pieces of the comment
// stayed. Checked here through the editor's keys and the note box's functions.
#include "annotations.h"
#include "d2d_init.h"
#include "editor.h"
#include "input.h"
#include "keystroke.h"
#include "render.h"
#include "settings.h"
#include "tabs.h"
#include "utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

namespace {
namespace fs = std::filesystem;

int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) {
        std::cerr << "FAIL review notes: " << message << '\n';
        ++failures;
    }
}

std::string bytesOf(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// A text without one of its lines, the one that holds `words`
std::string without(const std::string& text, const std::string& words) {
    const size_t at = text.find(words);
    if (at == std::string::npos) return text;
    size_t start = text.rfind('\n', at), end = text.find('\n', at);
    start = start == std::string::npos ? 0 : start + 1;
    end = end == std::string::npos ? text.size() : end + 1;
    return text.substr(0, start) + text.substr(end);
}

// Every note's range holds a review comment, whole
bool notesInPlace(const App& app) {
    for (const auto& note : app.annotations) {
        if (note.commentEnd > app.sourceText.size() || note.commentStart >= note.commentEnd) return false;
        const std::string comment = app.sourceText.substr(note.commentStart, note.commentEnd - note.commentStart);
        if (comment.rfind("<!-- review \"", 0) != 0 || comment.size() < 3 || comment.compare(comment.size() - 3, 3, "-->") != 0) return false;
    }
    return true;
}

bool select(App& app, const std::wstring& words) {
    ensureLayoutComplete(app);
    const size_t at = app.docText.find(words);
    if (at == std::wstring::npos) return false;
    app.selAnchor = at;
    app.selFocus = at + words.size();
    app.hasSelection = true;
    return true;
}

void everything(App& app, const char* fixture) {
    const fs::path dir = fs::current_path() / "review-notes";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path doc = dir / "Notes.md";
    fs::copy_file(fixture, doc, ec);
    check(!ec, "the fixture copies");

    tabsInit(app);
    check(openDocumentInViewer(app, doc.wstring()), "the fixture opens");
    check(app.annotations.size() == 2 && notesInPlace(app), "the fixture's two notes are read");

    // A line typed above the notes, saved, the editor left
    enterEditMode(app);
    app.editorCursorPos = app.editorSelStart = app.editorSelEnd = 0;
    app.editorHasSelection = false;
    for (unsigned key : {(unsigned)'Z', (unsigned)'Z', (unsigned)'Z', (unsigned)VK_RETURN}) keystroke(app, key);
    KeyMods ctrl;
    ctrl.ctrl = true;
    keystroke(app, 'S', ctrl);
    const std::string saved = bytesOf(doc);
    check(!app.editorDirty && saved.rfind("zzz", 0) == 0, "a line typed at the top is saved");
    exitEditMode(app);
    check(!app.editMode && app.annotations.size() == 2, "the reading view has both notes after the edit");
    check(notesInPlace(app), "each note's range holds its comment in the text as saved");

    // The first note deleted: its line, and nothing else
    annotationOpenEditor(app, 0);
    annotationEditorDelete(app, app.hwnd);
    const std::string deleted = without(saved, "say which fox");
    check(deleted.size() < saved.size() && bytesOf(doc) == deleted, "deleting a note after the edit takes its comment's line and nothing else");
    check(app.annotations.size() == 1 && notesInPlace(app), "one note is left, in place");

    // The other changed, its range out of date, as a text taken in without
    // the notes read again leaves it
    app.sourceText.insert(0, "A line taken in from elsewhere.\n");
    check(!notesInPlace(app), "(the note's range is out of date for this check)");
    annotationOpenEditor(app, 0);
    app.annotEditorText = L"merge them";
    app.annotEditorCaret = app.annotEditorText.size();
    annotationEditorConfirm(app, app.hwnd);
    std::string changed = "A line taken in from elsewhere.\n" + deleted;
    const size_t words = changed.find("merge with the first");
    if (words != std::string::npos) changed.replace(words, 20, "merge them");
    check(words != std::string::npos && bytesOf(doc) == changed, "changing a note whose range is out of date changes its words and nothing else");
    check(app.annotations.size() == 1 && notesInPlace(app) && app.annotations[0].note == "merge them", "and the note reads as changed");

    // A note that is gone from the text by the time it is deleted
    annotationOpenEditor(app, 0);
    app.sourceText = without(app.sourceText, "merge them");
    annotationEditorDelete(app, app.hwnd);
    check(!app.annotEditorOpen && bytesOf(doc) == changed, "a note no longer in the text is not deleted from it: the file is as it was");
    check(app.annotations.empty(), "and the notes are read again from the text");

    // A new note, the text changed under its box: not written, the box stays
    check(openDocumentInViewer(app, doc.wstring()) && app.annotations.size() == 1, "the file opens again with its note");
    check(select(app, L"quick brown fox") && annotationBeginCreate(app), "a note is begun on selected words");
    app.annotEditorText = L"which one";
    app.sourceText.insert(0, "Another line from elsewhere.\n");
    annotationEditorConfirm(app, app.hwnd);
    check(app.annotEditorOpen && bytesOf(doc) == changed, "a new note whose place was found in another text is not written, and its box stays");
    annotationEditorCancel(app);

    // And with the text as it was: written after its paragraph
    check(openDocumentInViewer(app, doc.wstring()), "the file opens once more");
    check(select(app, L"quick brown fox") && annotationBeginCreate(app), "a note is begun again");
    app.annotEditorText = L"which one";
    annotationEditorConfirm(app, app.hwnd);
    const std::string noted = bytesOf(doc);
    check(noted.find("<!-- review \"quick brown fox\": which one -->") != std::string::npos && without(noted, "which one") == changed,
          "a new note is written as a line of its own, and nothing else changes");
    check(app.annotations.size() == 2 && notesInPlace(app), "both notes are read, in place");
}

}  // namespace

int runReviewNoteTests(const char* fixture) {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Review note tests", WS_POPUP, 0, 0, 1050, 800, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 800;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);

    everything(app, fixture);

    app.editorDirty = false;
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    std::cout << "Review notes: " << failures << " failures\n";
    return failures ? 1 : 0;
}
