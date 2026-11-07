#include "Theme.h"

namespace aether {

namespace {

struct Palette {
    const char* name;
    uint32_t bg, bg2, surface, elevated, elevatedHover, text, text2, text3, accent, perf, warning, critical, success, sidebar;
    bool light;
};

// Seven restrained palettes: no neon, controlled contrast, one performance accent per theme.
const Palette kPalettes[Theme::kCount] = {
    {"Grafit",   0x090A0C, 0x111317, 0x171A20, 0x20242B, 0x2A2F38, 0xF5F6F8, 0x9198A3, 0x5F6671, 0xC8D0DA, 0x8FC9FF, 0xE8B65B, 0xE36C6C, 0x7FD1A8, 0x0C0D10, false},
    {"Gece",     0x040506, 0x0A0B0E, 0x101216, 0x181B21, 0x22262E, 0xF5F6F8, 0x9198A3, 0x5F6671, 0xC8D0DA, 0x8FC9FF, 0xE8B65B, 0xE36C6C, 0x7FD1A8, 0x060708, false},
    {"Okyanus",  0x060B12, 0x0B1320, 0x101A29, 0x172436, 0x1F2F45, 0xF2F6FA, 0x8C9AAD, 0x5A6878, 0xBFD3E6, 0x5CC8E8, 0xE8B65B, 0xE36C6C, 0x6FD3A6, 0x080E17, false},
    {"Zümrüt",   0x070B0A, 0x0D1412, 0x121B18, 0x192521, 0x22302B, 0xF3F7F5, 0x8FA39B, 0x5C6E67, 0xC5D6CF, 0x6FD3A6, 0xE8B65B, 0xE36C6C, 0x8FC9FF, 0x0A100E, false},
    {"Kehribar", 0x0D0A07, 0x15110C, 0x1C1711, 0x261F17, 0x31281E, 0xF8F4EE, 0xA69A8A, 0x6E6457, 0xE2D3BC, 0xF0B860, 0xE8875B, 0xE36C6C, 0x9BCB8A, 0x100C08, false},
    {"Bordo",    0x0D0709, 0x150C0F, 0x1D1115, 0x28181E, 0x342029, 0xF8F2F4, 0xA8919A, 0x6F5A62, 0xE2C8D1, 0xE8798F, 0xE8B65B, 0xFF5A5A, 0x7FD1A8, 0x10080B, false},
    {"Açık",     0xF3F4F6, 0xE9EBEF, 0xFFFFFF, 0xF5F6F8, 0xECEEF1, 0x14171C, 0x5B6370, 0x8C939E, 0x3A4350, 0x2F6FCC, 0xB7791F, 0xC94848, 0x2E8B62, 0xEEF0F3, true},
};

Theme build(const Palette& p) {
    Theme t;
    t.name = p.name;
    t.light = p.light;
    t.bg = Color::hex(p.bg); t.bg2 = Color::hex(p.bg2); t.surface = Color::hex(p.surface); t.elevated = Color::hex(p.elevated);
    t.elevatedHover = Color::hex(p.elevatedHover);
    t.text = Color::hex(p.text); t.text2 = Color::hex(p.text2); t.text3 = Color::hex(p.text3);
    t.accent = Color::hex(p.accent); t.perf = Color::hex(p.perf);
    t.warning = Color::hex(p.warning); t.critical = Color::hex(p.critical); t.success = Color::hex(p.success);
    t.sidebar = Color::hex(p.sidebar);
    if (p.light) {
        t.fg = Color(0, 0, 0, 1);
        t.border = Color(0, 0, 0, 0.08f); t.borderStrong = Color(0, 0, 0, 0.14f); t.highlight = Color(1, 1, 1, 0.0f);
        t.btnTop = Color::hex(0x2A2F38); t.btnBottom = Color::hex(0x14171C); t.btnText = Color::hex(0xFFFFFF);
        t.shadow = 0.06f;
    } else {
        t.fg = Color(1, 1, 1, 1);
        t.border = Color(1, 1, 1, 0.055f); t.borderStrong = Color(1, 1, 1, 0.10f); t.highlight = Color(1, 1, 1, 0.035f);
        t.btnTop = Color::hex(0xEEF1F5); t.btnBottom = mix(t.accent, Color::hex(0xFFFFFF), 0.15f); t.btnText = Color::hex(0x0B0C0F);
        t.shadow = 0.22f;
    }
    return t;
}

int g_theme = 0;

} // namespace

const Theme& Theme::get(int index) {
    static Theme themes[kCount] = {build(kPalettes[0]), build(kPalettes[1]), build(kPalettes[2]), build(kPalettes[3]),
                                   build(kPalettes[4]), build(kPalettes[5]), build(kPalettes[6])};
    return themes[(index >= 0 && index < kCount) ? index : 0];
}
const Theme& Theme::current() { return get(g_theme); }
int Theme::currentIndex() { return g_theme; }
void Theme::select(int index) { g_theme = (index >= 0 && index < kCount) ? index : 0; }
Color FG(float alpha) { return Theme::current().fg.alpha(alpha); }

} // namespace aether
