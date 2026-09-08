#ifndef TINTA_KEYMAP_H
#define TINTA_KEYMAP_H

#include <windows.h>
#include <string>
#include <utility>
#include <vector>

// A virtual key and a typed character are different namespaces. In
// particular, VK_F1 is also the Unicode value of 'p', and VK_F12 of '{'.
struct KeyBinding {
    unsigned key = 0;
    bool isChar = false;
};

struct KeyActionDef {
    const char* name;
    unsigned defaultKey;
    bool isChar;
};
inline constexpr KeyActionDef KEY_ACTIONS[] = {
    {"search", 'F', false}, {"browse", 'B', false},
    {"toc", VK_TAB, false}, {"theme", 'T', false},
    {"stats", 'S', false}, {"quit", 'Q', false},
    {"newfile", 'N', false}, {"zen", VK_F11, false},
    {"scrollup", 'K', false}, {"scrolldown", 'J', false},
    {"edit", ':', true}, {"help", '?', true},
    {"annotate", 'A', false},
};
inline constexpr int KEY_ACTION_COUNT = 13;
enum KeyActionIndex {
    KA_SEARCH = 0, KA_BROWSE, KA_TOC, KA_THEME, KA_STATS, KA_QUIT,
    KA_NEWFILE, KA_ZEN, KA_SCROLLUP, KA_SCROLLDOWN, KA_EDIT, KA_HELP,
    KA_ANNOTATE
};

struct KeyProfileDef {
    const char* id;
    KeyBinding keys[KEY_ACTION_COUNT];
};
inline constexpr KeyProfileDef KEY_PROFILES[] = {
    {"windows", {{'F'}, {'B'}, {VK_TAB}, {'T'}, {'S'}, {'Q'}, {'N'},
                 {VK_F11}, {'K'}, {'J'}, {'E'}, {VK_F1}, {'A'}}},
    {"vim", {{'/', true}, {'B'}, {VK_TAB}, {'T'}, {'S'}, {'Q'}, {'N'},
             {VK_F11}, {'K'}, {'J'}, {':', true}, {'?', true}, {'A'}}},
};
inline constexpr int KEY_PROFILE_COUNT = 2;

int keyProfileIndexById(const std::string& id);
KeyBinding parseKeyName(const std::string& value);
std::string keyIniName(KeyBinding binding);
std::wstring keyLabel(KeyBinding binding);
void resolveKeymap(KeyBinding (&keys)[KEY_ACTION_COUNT], const std::string& profile,
                   const std::vector<std::pair<std::string, std::string>>& overrides);
bool keyBindingMatches(KeyBinding binding, unsigned key, bool isChar);
// Convert to the legacy viewer command codes. Unbound characters produce
// no command; navigation virtual keys pass through unless explicitly moved.
unsigned translateActionKey(const KeyBinding* keys, unsigned key, bool isChar);

#endif
