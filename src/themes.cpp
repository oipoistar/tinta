#include "app.h"

// 10 Distinctive Themes - 5 Light, 5 Dark
const D2DTheme THEMES[] = {
    // ═══════════════════════════════════════════════════════════
    // LIGHT THEMES
    // ═══════════════════════════════════════════════════════════

    // 1. Paper - Warm sepia, literary manuscript feel
    {
        L"Paper", L"Segoe UI", L"Consolas", false,
        hexColor(0xF5F1E8),    // background - warm cream
        hexColor(0x3D3329),    // text - deep brown
        hexColor(0x2A1F16),    // heading - dark brown
        hexColor(0xB85A3C),    // link - terracotta
        hexColor(0x6B5344),    // code - brown
        hexColor(0xEDE6DA),    // codeBackground - parchment
        hexColor(0xC4B8A8),    // blockquoteBorder
        hexColor(0xB85A3C),    // accent
        // Syntax colors
        hexColor(0x8B4513),    // syntaxKeyword - saddle brown
        hexColor(0x2E8B57),    // syntaxString - sea green
        hexColor(0x808080),    // syntaxComment - gray
        hexColor(0xB8860B),    // syntaxNumber - dark goldenrod
        hexColor(0x4169E1),    // syntaxFunction - royal blue
        hexColor(0x9932CC)     // syntaxType - dark orchid
    },

    // 2. Sakura - Japanese cherry blossom, soft pink elegance
    {
        L"Sakura", L"Segoe UI", L"Consolas", false,
        hexColor(0xFDF8F8),    // background - soft blush white
        hexColor(0x404040),    // text - soft charcoal
        hexColor(0xC44569),    // heading - deep rose
        hexColor(0xE8749A),    // link - coral pink
        hexColor(0x8B5570),    // code - plum
        hexColor(0xF8ECF0),    // codeBackground - blush
        hexColor(0xE8B4C0),    // blockquoteBorder
        hexColor(0xC44569),    // accent
        // Syntax colors
        hexColor(0xC44569),    // syntaxKeyword - rose
        hexColor(0x2E8B57),    // syntaxString - sea green
        hexColor(0x999999),    // syntaxComment - gray
        hexColor(0xE8749A),    // syntaxNumber - coral pink
        hexColor(0x6A5ACD),    // syntaxFunction - slate blue
        hexColor(0x8B5570)     // syntaxType - plum
    },

    // 3. Arctic - Nordic ice blues, crisp and clean
    {
        L"Arctic", L"Segoe UI", L"Cascadia Code", false,
        hexColor(0xF7FAFC),    // background - ice white
        hexColor(0x2D3748),    // text - deep slate
        hexColor(0x1A365D),    // heading - navy
        hexColor(0x3182CE),    // link - bright blue
        hexColor(0x285E61),    // code - teal
        hexColor(0xEBF8FF),    // codeBackground - light blue
        hexColor(0xA0C4E8),    // blockquoteBorder
        hexColor(0x3182CE),    // accent
        // Syntax colors
        hexColor(0x0066CC),    // syntaxKeyword - blue
        hexColor(0x2E8B57),    // syntaxString - sea green
        hexColor(0x708090),    // syntaxComment - slate gray
        hexColor(0xD2691E),    // syntaxNumber - chocolate
        hexColor(0x1A365D),    // syntaxFunction - navy
        hexColor(0x6B5B95)     // syntaxType - purple
    },

    // 4. Meadow - Fresh organic greens, nature-inspired
    {
        L"Meadow", L"Segoe UI", L"Consolas", false,
        hexColor(0xF7FAF7),    // background - soft white-green
        hexColor(0x1A2F1A),    // text - forest
        hexColor(0x1C4532),    // heading - deep green
        hexColor(0x38A169),    // link - fresh green
        hexColor(0x4A5568),    // code - slate
        hexColor(0xE6FFED),    // codeBackground - mint
        hexColor(0x9AE6B4),    // blockquoteBorder
        hexColor(0x38A169),    // accent
        // Syntax colors
        hexColor(0x1C4532),    // syntaxKeyword - deep green
        hexColor(0x8B4513),    // syntaxString - saddle brown
        hexColor(0x708090),    // syntaxComment - slate gray
        hexColor(0xD2691E),    // syntaxNumber - chocolate
        hexColor(0x38A169),    // syntaxFunction - fresh green
        hexColor(0x6B5B95)     // syntaxType - purple
    },

    // 5. Dusk - Golden hour warmth, sunset tones
    {
        L"Dusk", L"Segoe UI", L"Consolas", false,
        hexColor(0xFFFBF5),    // background - warm white
        hexColor(0x553C10),    // text - deep amber
        hexColor(0x9C4221),    // heading - burnt orange
        hexColor(0xB7791F),    // link - gold
        hexColor(0x5F5030),    // code - olive
        hexColor(0xFEF5E7),    // codeBackground - cream
        hexColor(0xE8C48D),    // blockquoteBorder
        hexColor(0xB7791F),    // accent
        // Syntax colors
        hexColor(0x9C4221),    // syntaxKeyword - burnt orange
        hexColor(0x2E8B57),    // syntaxString - sea green
        hexColor(0x808080),    // syntaxComment - gray
        hexColor(0xB7791F),    // syntaxNumber - gold
        hexColor(0x4169E1),    // syntaxFunction - royal blue
        hexColor(0x8B4513)     // syntaxType - saddle brown
    },

    // ═══════════════════════════════════════════════════════════
    // DARK THEMES
    // ═══════════════════════════════════════════════════════════

    // 6. Midnight - Deep space, cosmic tranquility
    {
        L"Midnight", L"Segoe UI", L"Cascadia Code", true,
        hexColor(0x0D1B2A),    // background - deep navy
        hexColor(0xE0E1DD),    // text - soft blue-white
        hexColor(0xF0F4F8),    // heading - moonlight
        hexColor(0x00B4D8),    // link - electric blue
        hexColor(0x90E0EF),    // code - cyan
        hexColor(0x1B263B),    // codeBackground - deep blue
        hexColor(0x415A77),    // blockquoteBorder
        hexColor(0x00B4D8),    // accent
        // Syntax colors
        hexColor(0x00B4D8),    // syntaxKeyword - electric blue
        hexColor(0x98FB98),    // syntaxString - pale green
        hexColor(0x6C7A89),    // syntaxComment - gray-blue
        hexColor(0xFFD700),    // syntaxNumber - gold
        hexColor(0x90E0EF),    // syntaxFunction - cyan
        hexColor(0xDDA0DD)     // syntaxType - plum
    },

    // 7. Dracula - Classic dark, purples and pinks
    {
        L"Dracula", L"Segoe UI", L"Consolas", true,
        hexColor(0x282A36),    // background - deep purple-gray
        hexColor(0xF8F8F2),    // text - light gray
        hexColor(0xFF79C6),    // heading - pink
        hexColor(0x8BE9FD),    // link - cyan
        hexColor(0x50FA7B),    // code - green
        hexColor(0x21222C),    // codeBackground - darker
        hexColor(0x6272A4),    // blockquoteBorder
        hexColor(0xBD93F9),    // accent - purple
        // Syntax colors (Dracula palette)
        hexColor(0xFF79C6),    // syntaxKeyword - pink
        hexColor(0xF1FA8C),    // syntaxString - yellow
        hexColor(0x6272A4),    // syntaxComment - gray
        hexColor(0xBD93F9),    // syntaxNumber - purple
        hexColor(0x50FA7B),    // syntaxFunction - green
        hexColor(0x8BE9FD)     // syntaxType - cyan
    },

    // 8. Forest - Deep mystical greens
    {
        L"Forest", L"Segoe UI", L"Consolas", true,
        hexColor(0x0D1512),    // background - deep green-black
        hexColor(0xB8C5B2),    // text - sage
        hexColor(0x9AE6B4),    // heading - bright green
        hexColor(0x68D391),    // link - lime
        hexColor(0x81E6D9),    // code - mint
        hexColor(0x1A2A23),    // codeBackground - dark green
        hexColor(0x4A6E5A),    // blockquoteBorder
        hexColor(0x68D391),    // accent
        // Syntax colors
        hexColor(0x9AE6B4),    // syntaxKeyword - bright green
        hexColor(0xF0E68C),    // syntaxString - khaki
        hexColor(0x5F7A6A),    // syntaxComment - muted green
        hexColor(0xFFB86C),    // syntaxNumber - orange
        hexColor(0x68D391),    // syntaxFunction - lime
        hexColor(0x81E6D9)     // syntaxType - mint
    },

    // 9. Ember - Warm charcoal with fire accents
    {
        L"Ember", L"Segoe UI", L"Consolas", true,
        hexColor(0x1A1614),    // background - warm black
        hexColor(0xD4C5B9),    // text - warm gray
        hexColor(0xF6AD55),    // heading - amber
        hexColor(0xED8936),    // link - orange
        hexColor(0xFC8181),    // code - coral
        hexColor(0x252019),    // codeBackground - dark warm
        hexColor(0x5C4A3A),    // blockquoteBorder
        hexColor(0xED8936),    // accent
        // Syntax colors
        hexColor(0xF6AD55),    // syntaxKeyword - amber
        hexColor(0x98FB98),    // syntaxString - pale green
        hexColor(0x6B5B4F),    // syntaxComment - warm gray
        hexColor(0xFC8181),    // syntaxNumber - coral
        hexColor(0xED8936),    // syntaxFunction - orange
        hexColor(0xDDA0DD)     // syntaxType - plum
    },

    // 10. Abyss - True black, neon accents (OLED-friendly)
    {
        L"Abyss", L"Segoe UI Light", L"Cascadia Mono", true,
        hexColor(0x000000),    // background - pure black
        hexColor(0xFFFFFF),    // text - pure white
        hexColor(0x00FFE1),    // heading - cyan
        hexColor(0xFF00FF),    // link - magenta
        hexColor(0xAAFF00),    // code - lime
        hexColor(0x0A0A0A),    // codeBackground - near black
        hexColor(0x333333),    // blockquoteBorder
        hexColor(0x00FFE1),    // accent
        // Syntax colors (neon)
        hexColor(0xFF00FF),    // syntaxKeyword - magenta
        hexColor(0xAAFF00),    // syntaxString - lime
        hexColor(0x666666),    // syntaxComment - dark gray
        hexColor(0xFF6600),    // syntaxNumber - orange
        hexColor(0x00FFE1),    // syntaxFunction - cyan
        hexColor(0xFFFF00)     // syntaxType - yellow
    }
};

const int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);
