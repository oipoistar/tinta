#ifndef TINTA_EDITOR_H
#define TINTA_EDITOR_H

#include "app.h"
#include "live_text.h"
#include <windows.h>

// Mode transitions
void enterEditMode(App& app);
// Ctrl+N quick note: edit mode on an untitled empty buffer with no backing
// file; the first save routes through the classic Save As dialog
void enterQuickNoteMode(App& app);
void enterRecoveredDraft(App& app, HWND hwnd, const std::string& content,
                         const std::string& origPath);
void exitEditMode(App& app);
void setEditorReadingPreview(App& app, bool reading);
// The Read pill's words, with the shortcut that reads where it is shown
std::wstring editorReadingButtonLabel(const App& app);
// The Read button (#236) fades in for a hint session's intro and while the
// pointer rests on it, and is hidden and click-through otherwise (#245)
D2D1_RECT_F editorReadingButtonRect(const App& app);
void renderEditorReadingButton(App& app);
bool editorReadingButtonShown(const App& app);
bool editorReadingButtonNeedsTicks(const App& app);
void editorReadingButtonHover(App& app, float x, float y);
// Word and character count beside the Read button (#240)
void renderEditorWordCount(App& app);
// Alt+Up/Down move lines, Ctrl+Alt+Up/Down add carets (#251); false when
// the key is not one of them or the source editor is not in front
bool editorAltArrowKey(App& app, HWND hwnd, WPARAM wParam);
void editorClearExtraCarets(App& app);
// Multi-caret clipboard text (#251): every selection, joined by line
// breaks; pasting a text with one line per caret hands each its own line
std::wstring editorCaretSelectionsText(const App& app);
void editorPasteAtCarets(App& app, HWND hwnd, const std::wstring& paste);

// Editor input handlers
void handleEditorKeyDown(App& app, HWND hwnd, WPARAM wParam);
void handleEditorCharInput(App& app, HWND hwnd, WPARAM wParam);
void editorPositionImeWindow(App& app, HWND hwnd);
void handleEditorMouseDown(App& app, HWND hwnd, int x, int y);
void handleEditorMouseUp(App& app, HWND hwnd, int x, int y);
void handleEditorMouseMove(App& app, HWND hwnd, int x, int y);
// A held finger extends the original word, without arming a text drag.
void editorTouchSelection(App& app, int x, int y, bool begin);
void handleEditorMouseWheel(App& app, HWND hwnd, float delta);
// A dragged selection (#289): TIMER_EDITOR_DRAG_SCROLL scrolls the editor
// while the pointer is past its top or bottom edge; Esc, a lost capture or
// a cancelled mode put the drag away and leave the text as it was
void editorTextDragScroll(App& app, HWND hwnd);
void editorTextDragCancel(App& app, HWND hwnd);

// Editor rendering
void renderEditor(App& app, float editorWidth);
// Screen-space caret origin shared by rendering and IME anchoring.
bool editorCaretPoint(App& app, D2D1_POINT_2F& point);

// Re-enter edit mode from a parked tab buffer (tabs.cpp), restoring the
// dirty flag, scroll, and caret the switch-away captured; bom when the
// buffer's file starts with a byte order mark
void restoreEditBuffer(App& app, const std::wstring& text, bool dirty,
                       float scrollY, size_t cursor, bool bom = false);

// Quick-note empty state (design 4a): an untitled buffer with no text yet
// shows a centered "Open a file" button in the preview pane until typing
// starts; the button (or Ctrl+O) opens the classic file picker
bool quickNoteEmptyStateActive(const App& app);
// Open in a tab, preserving existing/unsaved documents in either mode.
void openFileDialog(App& app, HWND hwnd);
void renderQuickNoteEmptyState(App& app);

// File save; Save As (Ctrl+Shift+S) re-prompts for the path and moves the
// tab's identity to the chosen file
void saveEditorFile(App& app, HWND hwnd);
void saveEditorFileAs(App& app, HWND hwnd);
// Ctrl+Shift+S anywhere: editor writes the buffer to a new path, viewer
// copies the viewed file; the tab follows the new name either way
void saveFileAs(App& app, HWND hwnd);
// Unsaved-changes dialog outcome: 1 save+exit, 2 discard, 3 keep editing
void confirmExitAction(App& app, HWND hwnd, int action);

// Editor reparse (called from timer)
// Exports need the current buffer even when the preview pane is hidden.
void editorReparse(App& app, bool force = false);
// External edits: undo-capable range replace plus dirty + immediate
// reparse, used by the preview table cell editor (#148)
void editorReplaceRangeExternal(App& app, size_t start, size_t end,
                                const std::wstring& repl);
void editorMarkDirtyAndReparse(App& app);
// The Live editor's edits, in order, as one undo step (a character typed
// runs on like typing in the source), then its caret and selection
void editorApplyLiveEdits(App& app, const std::vector<LiveText::Change>& changes, size_t caret, size_t anchor);
// Ctrl+Z and Ctrl+Y, for the Live editor
void editorUndoRedo(App& app, bool redo);
// Puts the source's caret in view (back from Live)
void editorRevealCaret(App& app);
// Ctrl+V at the source's caret: a screenshot saves as a PNG beside the
// document, a spreadsheet's range becomes a table unless `raw` (Shift)
void editorPasteClipboard(App& app, HWND hwnd, bool raw);
// The fenced templates the rail's flyout and Live's slash menu insert:
// 0 flowchart, 1 sequence, 2 class, 3 state, 4 gantt, 5 pie, 6 an empty
// Mermaid block, 7-9 PlantUML sequence, class and activity, 10 mind map
const wchar_t* diagramTemplate(int kind);

// Editor search
void performEditorSearch(App& app);
void scrollEditorToMatch(App& app);
// Find & replace (#121): replace the current match / every match with
// App::replaceText; each hunk is one undo entry
void editorReplaceCurrent(App& app, HWND hwnd);
void editorReplaceAll(App& app, HWND hwnd);

// Utility
// Navigate without changing the source buffer, including when preview is hidden.
void editorScrollToSourceOffset(App& app, size_t byteOffset);
void rebuildLineStarts(App& app);
size_t editorTopVisibleLine(App& app);
// Scrolls the edit-mode preview to follow the editor's top line
void syncPreviewToEditor(App& app);
// The other way: the editor scrolls to the source line at the preview's
// top, for a drag on the preview's scrollbar (#280)
void editorFollowPreview(App& app);

// Ctrl+G, go to line (#276): a prompt over the editor or the reading view.
// While open it takes every key; Enter jumps, Esc closes. The key-down
// consumes only those and Backspace, so the message loop turns the other
// keys into the characters gotoLineChar types (#282).
void gotoLineOpenPrompt(App& app);
bool gotoLineKeyDown(App& app, HWND hwnd, WPARAM key);
void gotoLineChar(App& app, wchar_t ch);
// The prompt's box, and whether a point in client pixels falls on it
D2D1_RECT_F gotoLinePromptBox(const App& app);
bool gotoLinePromptHit(const App& app, float x, float y);
// Lines in the document being edited or read
size_t gotoLineCount(const App& app);
// 1-based, clamped to the document: the editor's caret moves to the line's
// start, centred; the reading view scrolls to where the line is drawn
void gotoLineJump(App& app, size_t line);
void renderGotoLinePrompt(App& app);
std::string toUtf8(const std::wstring& wstr);

#endif // TINTA_EDITOR_H
