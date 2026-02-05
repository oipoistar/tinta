#ifndef TINTA_FILE_UTILS_H
#define TINTA_FILE_UTILS_H

#include "app.h"
#include <string>

#define TIMER_FILE_WATCH 1

void updateFileWriteTime(App& app);
bool isRootPath(const std::wstring& path);
std::wstring getParentPath(const std::wstring& path);
std::wstring getDirectoryFromFile(const std::string& filePath);
void populateFolderItems(App& app);

#endif // TINTA_FILE_UTILS_H
