#pragma once
// The clipboard's older keys, which Windows programs know beside Ctrl+C,
// Ctrl+X and Ctrl+V (#320): Ctrl+Insert copies, Shift+Insert pastes and
// Shift+Delete cuts. Every place that takes the letters takes these too:
// its key handler reads a key-down through takeClipboardKey, which turns
// such a key into Ctrl with its letter, and goes on as it did.

// The letter an older clipboard key stands for: 'C', 'V' or 'X'. 0 for
// any other key, and for Insert or Delete with other modifiers
inline unsigned clipboardKeyLetter(unsigned key, bool ctrl, bool shift) {
    const unsigned insertKey = 0x2D, deleteKey = 0x2E;  // VK_INSERT, VK_DELETE
    if (key == insertKey) return ctrl && !shift ? 'C' : (shift && !ctrl ? 'V' : 0);
    if (key == deleteKey) return shift && !ctrl ? 'X' : 0;
    return 0;
}

// Turns an older clipboard key into Ctrl with its letter, in the handler's
// own key and modifiers. True when it did
template <typename Key>
inline bool takeClipboardKey(Key& key, bool& ctrl, bool& shift) {
    const unsigned letter = clipboardKeyLetter(static_cast<unsigned>(key), ctrl, shift);
    if (!letter) return false;
    key = static_cast<Key>(letter);
    ctrl = true;
    shift = false;
    return true;
}
