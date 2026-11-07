#pragma once
#include <cstdint>

namespace aether {

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1) : r(r_), g(g_), b(b_), a(a_) {}
    static constexpr Color hex(uint32_t rgb, float a = 1) {
        return Color(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
    }
    constexpr Color alpha(float na) const { return Color(r, g, b, na); }
    constexpr Color mul(float k) const { return Color(r, g, b, a * k); }
};

inline Color mix(const Color& x, const Color& y, float t) {
    return Color(x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t);
}

// Design tokens. All sizes are in device-independent pixels (DIP).
struct Theme {
    Color bg, bg2, surface, elevated, text, text2, text3, accent, perf, warning, critical, success;
    Color border, borderStrong, highlight, sidebar;
    Color fg;                       // contrast base for translucent overlays (white on dark, black on light)
    Color btnTop, btnBottom, btnText; // primary button
    Color elevatedHover;
    float shadow = 0.22f;           // card shadow opacity
    bool light = false;
    const char* name = "";          // Turkish source name (translated with T())

    static constexpr int kCount = 7;
    static const Theme& get(int index);
    static const Theme& current();
    static int currentIndex();
    static void select(int index);
};

// Translucent contrast colour of the active theme (replaces hard-coded white overlays).
Color FG(float alpha);

namespace sp {   // spacing scale
inline constexpr float x1 = 4, x2 = 8, x3 = 12, x4 = 16, x5 = 20, x6 = 24, x8 = 32, x10 = 40, x12 = 48;
}
namespace radius {
inline constexpr float sm = 6, md = 10, lg = 14, xl = 18;
}
namespace type {   // type scale
inline constexpr float caption = 11, small = 12, body = 13.5f, bodyLarge = 15, title = 18, heading = 26, display = 34, hero = 56;
}

} // namespace aether
