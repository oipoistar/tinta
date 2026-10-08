#ifndef TINTA_FILE_UTILS_H
#define TINTA_FILE_UTILS_H

#include "app.h"
#include <commdlg.h>
#include <string>

#define TIMER_FILE_WATCH 1

void updateFileWriteTime(App& app);
bool isRootPath(const std::wstring& path);
std::wstring getParentPath(const std::wstring& path);
std::wstring getDirectoryFromFile(const std::string& filePath);
void populateFolderItems(App& app);

// The folder Tinta was started in, captured before the process first
// leaves it. Relative arguments, the file browser's fallback and an
// untitled note's relative images resolve against it (#253).
const std::wstring& launchDirectory();
// Moves the working directory into the system folder. Explorer starts
// Tinta inside the document's folder and file dialogs can move it into the
// picked one; Windows will not delete or rename a folder that a running
// process works in (#253).
void parkWorkingDirectory();
// GetOpenFileNameW / GetSaveFileNameW that park the working directory
// again afterwards, whatever the dialog did with it (#253)
bool runOpenFileDialog(OPENFILENAMEW& ofn);
bool runSaveFileDialog(OPENFILENAMEW& ofn);

#endif // TINTA_FILE_UTILS_H
