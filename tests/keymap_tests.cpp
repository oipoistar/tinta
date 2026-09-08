#include "keymap.h"

#include <iostream>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main() {
    KeyBinding keys[KEY_ACTION_COUNT];
    resolveKeymap(keys, "windows", {});
    check(translateActionKey(keys, 'E', false) == ':', "VK_E enters edit before any character arrives");
    for (unsigned character : {0x65, 0x45, 0x0443, 0x0423, 0x03B5, 0xFF45}) {
        check(translateActionKey(keys, character, true) == 0,
              "Latin, Cyrillic, Greek and full-width text cannot trigger or duplicate E");
    }
    check(translateActionKey(keys, VK_F1, false) == '?', "F1 opens help without a WM_CHAR");
    check(translateActionKey(keys, 'p', true) == 0 &&
          translateActionKey(keys, 'P', true) == 0, "P does not alias F1");
    check(translateActionKey(keys, VK_F11, false) == VK_F11, "Windows zen key survives");
    check(translateActionKey(keys, VK_DOWN, false) == VK_DOWN, "unbound arrow navigation survives");
    for (unsigned character : {0x3A, 0xFF1A})
        check(translateActionKey(keys, character, true) == ':', "ASCII/full-width edit fallback");
    for (unsigned character : {0x3F, 0xFF1F})
        check(translateActionKey(keys, character, true) == '?', "ASCII/full-width help fallback");

    resolveKeymap(keys, "vim", {});
    check(translateActionKey(keys, '/', true) == 'F', "vim slash opens search via WM_CHAR");
    check(translateActionKey(keys, VK_OEM_2, false) != 'F', "layout-dependent slash VK is not a character");
    check(translateActionKey(keys, 'F', false) == 0 &&
          translateActionKey(keys, 'F', true) == 0, "rebound default cannot reopen search via WM_CHAR");
    check(translateActionKey(keys, 'E', false) != ':' &&
          translateActionKey(keys, 'e', true) == 0, "Windows E stays inactive in vim");

    for (unsigned n = 1; n <= 12; ++n) {
        std::string name = "F" + std::to_string(n);
        resolveKeymap(keys, "custom", {{"help", name}, {"zen", "Z"}});
        check(translateActionKey(keys, VK_F1 + n - 1, false) == '?', "function-key help remap works");
        check(translateActionKey(keys, VK_F1 + n - 1, true) != '?', "function key is not a Unicode alias");
        check(keyIniName(keys[KA_HELP]) == name, "function binding round trips");
    }
    resolveKeymap(keys, "custom", {{"edit", "G"}, {"help", "H"}});
    check(translateActionKey(keys, 'G', false) == ':' &&
          translateActionKey(keys, 'H', false) == '?', "letter remaps work on keydown");
    check(translateActionKey(keys, 'g', true) == 0 &&
          translateActionKey(keys, 'h', true) == 0, "letter remaps cannot fire twice");
    resolveKeymap(keys, "custom", {{"edit", "F2"}, {"help", "{"}});
    check(translateActionKey(keys, VK_F2, false) == ':', "function-key edit remap works");
    check(translateActionKey(keys, 'q', true) == 0, "function-key edit cannot alias Q");
    check(translateActionKey(keys, '{', true) == '?' &&
          translateActionKey(keys, VK_F12, false) != '?', "brace and F12 stay distinct");
    check(keyIniName(keys[KA_HELP]) == "{", "brace remains punctuation in settings and hints");
    resolveKeymap(keys, "custom", {{"edit", "Tab"}, {"toc", "I"},
                                  {"help", "Space"}});
    check(translateActionKey(keys, VK_TAB, false) == ':' &&
          translateActionKey(keys, VK_SPACE, false) == '?', "named nonprinting bindings use keydown");
    resolveKeymap(keys, "custom", {{"help", "F1garbage"}});
    check(translateActionKey(keys, '?', true) == '?', "invalid binding retains default");
    check(!parseKeyName("F13").key && !parseKeyName("F1garbage").key,
          "malformed function-key settings are rejected");
    std::cout << "Keymap: " << failures << " failures\n";
    return failures ? 1 : 0;
}
