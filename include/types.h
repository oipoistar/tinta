#ifndef TINTA_TYPES_H
#define TINTA_TYPES_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace qmd {

// Color with RGBA components
struct Color {
    float r, g, b, a;

    Color() : r(0), g(0), b(0), a(1) {}
    Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}

    static Color fromHex(uint32_t hex) {
        return Color(
            ((hex >> 16) & 0xFF) / 255.0f,
            ((hex >> 8) & 0xFF) / 255.0f,
            (hex & 0xFF) / 255.0f,
            1.0f
        );
    }

    static Color white() { return Color(1, 1, 1, 1); }
    static Color black() { return Color(0, 0, 0, 1); }
    static Color gray(float v) { return Color(v, v, v, 1); }
};

// Rectangle for layout
struct Rect {
    float x, y, width, height;

    Rect() : x(0), y(0), width(0), height(0) {}
    Rect(float x, float y, float w, float h) : x(x), y(y), width(w), height(h) {}

    bool contains(float px, float py) const {
        return px >= x && px <= x + width && py >= y && py <= y + height;
    }
};

// Text style flags
enum class TextStyle : uint32_t {
    None = 0,
    Bold = 1 << 0,
    Italic = 1 << 1,
    Underline = 1 << 2,
    Strikethrough = 1 << 3,
    Code = 1 << 4,
};

inline TextStyle operator|(TextStyle a, TextStyle b) {
    return static_cast<TextStyle>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool hasStyle(TextStyle style, TextStyle flag) {
    return (static_cast<uint32_t>(style) & static_cast<uint32_t>(flag)) != 0;
}

// Theme colors
struct Theme {
    Color background;
    Color text;
    Color heading;
    Color link;
    Color codeBackground;
    Color codeText;
    Color blockquoteBorder;
    Color selection;

    static Theme dark() {
        Theme t;
        t.background = Color::fromHex(0x1a1a2e);
        t.text = Color::fromHex(0xe0e0e0);
        t.heading = Color::fromHex(0x64b5f6);
        t.link = Color::fromHex(0x81c784);
        t.codeBackground = Color::fromHex(0x2d2d44);
        t.codeText = Color::fromHex(0xffd54f);
        t.blockquoteBorder = Color::fromHex(0x4a4a6a);
        t.selection = Color(0.3f, 0.5f, 0.8f, 0.4f);
        return t;
    }

    static Theme light() {
        Theme t;
        t.background = Color::fromHex(0xfafafa);
        t.text = Color::fromHex(0x212121);
        t.heading = Color::fromHex(0x1565c0);
        t.link = Color::fromHex(0x2e7d32);
        t.codeBackground = Color::fromHex(0xeeeeee);
        t.codeText = Color::fromHex(0xc62828);
        t.blockquoteBorder = Color::fromHex(0xbdbdbd);
        t.selection = Color(0.2f, 0.4f, 0.9f, 0.3f);
        return t;
    }
};

} // namespace qmd

#endif // TINTA_TYPES_H
