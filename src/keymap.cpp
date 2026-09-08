#include "keymap.h"

#include <cctype>

KeyBinding parseKeyName(const std::string& value) {
    if (value.empty()) return {};
    if (value.size() == 1) {
        unsigned char ch = (unsigned char)value[0];
        if (ch < 32 || ch >= 127) return {};
        return {(unsigned)std::toupper(ch), ch != ' ' && !std::isalnum(ch)};
    }
    std::string lower;
    for (unsigned char ch : value) lower += (char)std::tolower(ch);
    if (lower == "tab") return {VK_TAB};
    if (lower == "space") return {VK_SPACE};
    for (unsigned n = 1; n <= 12; ++n) {
        if (lower == "f" + std::to_string(n)) return {VK_F1 + n - 1};
    }
    return {};
}

std::string keyIniName(KeyBinding binding) {
    if (!binding.isChar) {
        if (binding.key == VK_TAB) return "Tab";
        if (binding.key == VK_SPACE) return "Space";
        if (binding.key >= VK_F1 && binding.key <= VK_F12)
            return "F" + std::to_string(binding.key - VK_F1 + 1);
    }
    return std::string(1, (char)binding.key);
}

std::wstring keyLabel(KeyBinding binding) {
    std::string value = keyIniName(binding);
    return std::wstring(value.begin(), value.end());
}

int keyProfileIndexById(const std::string& id) {
    for (int i = 0; i < KEY_PROFILE_COUNT; ++i)
        if (id == KEY_PROFILES[i].id) return i;
    return -1;
}

void resolveKeymap(KeyBinding (&keys)[KEY_ACTION_COUNT], const std::string& profile,
                   const std::vector<std::pair<std::string, std::string>>& overrides) {
    int index = keyProfileIndexById(profile);
    for (int i = 0; i < KEY_ACTION_COUNT; ++i) {
        keys[i] = index >= 0 ? KEY_PROFILES[index].keys[i]
                            : KeyBinding{KEY_ACTIONS[i].defaultKey, KEY_ACTIONS[i].isChar};
        if (index >= 0) continue;
        for (const auto& override : overrides) {
            if (override.first != KEY_ACTIONS[i].name) continue;
            KeyBinding binding = parseKeyName(override.second);
            if (binding.key) keys[i] = binding;
        }
    }
}

bool keyBindingMatches(KeyBinding binding, unsigned key, bool isChar) {
    // Only punctuation bindings use WM_CHAR. Never case-fold a virtual
    // key as Unicode, or compare it to a layout-dependent character.
    if (isChar && key == 0xFF1A) key = ':';
    if (isChar && key == 0xFF1F) key = '?';
    return binding.isChar == isChar && binding.key == key;
}

unsigned translateActionKey(const KeyBinding* keys, unsigned key, bool isChar) {
    for (int i = 0; i < KEY_ACTION_COUNT; ++i) {
        if (keyBindingMatches(keys[i], key, isChar)) return KEY_ACTIONS[i].defaultKey;
    }
    if (isChar) {
        // Preserve the documented ':'/'?' fallbacks in every profile.
        if (key == ':' || key == 0xFF1A) return ':';
        if (key == '?' || key == 0xFF1F) return '?';
        return 0;
    }
    for (const auto& action : KEY_ACTIONS) {
        if (!action.isChar && action.defaultKey == key) return 0;
    }
    return key;
}
