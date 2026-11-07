#include "Icons.h"
#include <cmath>

namespace aether {

void drawIcon(Canvas& c, Icon icon, float x, float y, float size, const Color& col, float sw) {
    const float k = size / 20.0f;
    auto X = [&](float v) { return x + v * k; };
    auto Y = [&](float v) { return y + v * k; };
    float w = sw * std::max(0.85f, k);
    auto L = [&](float x1, float y1, float x2, float y2) { c.line(X(x1), Y(y1), X(x2), Y(y2), col, w); };
    auto P = [&](std::initializer_list<Pt> pts, bool closed = false, bool fill = false) {
        std::vector<Pt> v;
        for (auto& p : pts) v.push_back({X(p.x), Y(p.y)});
        c.path(v, closed, col, w, fill);
    };
    auto R = [&](float rx, float ry, float rw, float rh, float rad) { c.strokeRound({X(rx), Y(ry), rw * k, rh * k}, rad * k, col, w); };
    auto C = [&](float cx, float cy, float r) { c.strokeCircle(X(cx), Y(cy), r * k, col, w); };
    auto F = [&](float cx, float cy, float r) { c.fillCircle(X(cx), Y(cy), r * k, col); };
    switch (icon) {
        case Icon::Overview:
            R(3, 3, 6, 6, 1.8f); R(11, 3, 6, 6, 1.8f); R(3, 11, 6, 6, 1.8f);
            c.fillRound({X(11), Y(11), 6 * k, 6 * k}, 1.8f * k, col.mul(0.9f));
            break;
        case Icon::Stress: P({{2, 11}, {6, 11}, {8, 6}, {11, 15}, {13, 9}, {14.5f, 11}, {18, 11}}); break;
        case Icon::Benchmark:
            c.arc(X(10), Y(11.5f), 7.2f * k, 2.53f, 6.9f, col, w);
            L(10, 11.5f, 13.6f, 7.4f); F(10, 11.5f, 1.5f);
            break;
        case Icon::Monitoring:
            R(2.5f, 3.5f, 15, 13, 2.5f);
            P({{5, 12.5f}, {7.5f, 9.5f}, {10, 11}, {12.5f, 7}, {15, 9}});
            break;
        case Icon::Results:
            L(4, 16.5f, 4, 11); L(8, 16.5f, 8, 7.5f); L(12, 16.5f, 12, 9.5f); L(16, 16.5f, 16, 4);
            break;
        case Icon::Hardware:
        case Icon::Chip:
            R(5, 5, 10, 10, 2); R(8, 8, 4, 4, 0.8f);
            for (float t : {7.5f, 10.0f, 12.5f}) { L(t, 2.5f, t, 5); L(t, 15, t, 17.5f); L(2.5f, t, 5, t); L(15, t, 17.5f, t); }
            break;
        case Icon::Settings:
            L(3, 6, 17, 6); L(3, 10, 17, 10); L(3, 14, 17, 14);
            c.fillCircle(X(7), Y(6), 2.1f * k, col); c.fillCircle(X(13), Y(10), 2.1f * k, col); c.fillCircle(X(9), Y(14), 2.1f * k, col);
            break;
        case Icon::Play: P({{6.5f, 4.5f}, {15.5f, 10}, {6.5f, 15.5f}}, true, true); break;
        case Icon::Pause: c.fillRound({X(5.5f), Y(4.5f), 3 * k, 11 * k}, 1 * k, col); c.fillRound({X(11.5f), Y(4.5f), 3 * k, 11 * k}, 1 * k, col); break;
        case Icon::Stop: c.fillRound({X(5), Y(5), 10 * k, 10 * k}, 2 * k, col); break;
        case Icon::Export: L(10, 3, 10, 12.5f); P({{6.5f, 6.5f}, {10, 3}, {13.5f, 6.5f}}); P({{4, 11}, {4, 16.5f}, {16, 16.5f}, {16, 11}}); break;
        case Icon::Compare: L(3.5f, 7, 15.5f, 7); P({{12.5f, 4}, {15.5f, 7}, {12.5f, 10}}); L(16.5f, 13, 4.5f, 13); P({{7.5f, 10}, {4.5f, 13}, {7.5f, 16}}); break;
        case Icon::Trash: L(3.5f, 5.5f, 16.5f, 5.5f); P({{8, 5.5f}, {8, 3.5f}, {12, 3.5f}, {12, 5.5f}}); P({{5, 5.5f}, {6, 16.5f}, {14, 16.5f}, {15, 5.5f}}); break;
        case Icon::Folder: P({{2.5f, 15.5f}, {2.5f, 4.5f}, {7.5f, 4.5f}, {9.5f, 6.5f}, {17.5f, 6.5f}, {17.5f, 15.5f}}, true); break;
        case Icon::Camera: R(2.5f, 6, 15, 10.5f, 2.2f); P({{7, 6}, {8.2f, 3.8f}, {11.8f, 3.8f}, {13, 6}}); C(10, 11.2f, 2.8f); break;
        case Icon::Check: P({{4, 10.5f}, {8, 14.5f}, {16, 5.5f}}); break;
        case Icon::Warning: P({{10, 3}, {18, 16.5f}, {2, 16.5f}}, true); L(10, 8, 10, 11.5f); F(10, 14, 0.9f); break;
        case Icon::Info: C(10, 10, 7.5f); L(10, 9, 10, 14); F(10, 6.3f, 0.9f); break;
        case Icon::ChevronRight: P({{7.5f, 4.5f}, {13, 10}, {7.5f, 15.5f}}); break;
        case Icon::ChevronDown: P({{4.5f, 7.5f}, {10, 13}, {15.5f, 7.5f}}); break;
        case Icon::Close: L(5, 5, 15, 15); L(15, 5, 5, 15); break;
        case Icon::Refresh: c.arc(X(10), Y(10), 6.5f * k, -1.2f, 4.2f, col, w); P({{14.5f, 2.8f}, {13.3f, 4.6f}, {15.5f, 5.6f}}); break;
        case Icon::Copy: R(6.5f, 6.5f, 10, 10, 2); P({{4.5f, 13}, {3.5f, 13}, {3.5f, 3.5f}, {13, 3.5f}, {13, 4.5f}}); break;
        case Icon::Thermometer: R(8, 2.5f, 4, 10, 2); C(10, 14.5f, 3); L(10, 7, 10, 12); break;
        case Icon::Fan:
            C(10, 10, 1.6f);
            for (int i = 0; i < 3; ++i) {
                float a = i * 2.094f;
                c.arc(X(10 + std::cos(a) * 3.8f), Y(10 + std::sin(a) * 3.8f), 3.6f * k, a + 0.9f, a + 3.6f, col, w);
            }
            break;
        case Icon::Bolt: P({{11.5f, 2.5f}, {5, 11.5f}, {9.5f, 11.5f}, {8.5f, 17.5f}, {15, 8.5f}, {10.5f, 8.5f}}, true); break;
        case Icon::Clock: C(10, 10, 7.5f); P({{10, 5.5f}, {10, 10}, {13, 12}}); break;
        case Icon::Memory: R(2.5f, 6, 15, 8, 1.5f); for (float t : {6.0f, 9.0f, 12.0f}) R(t - 0.2f, 8.2f, 2.2f, 3.6f, 0.5f); L(5, 14, 5, 16.5f); L(15, 14, 15, 16.5f); break;
        case Icon::Gauge: c.arc(X(10), Y(12), 7 * k, 3.4f, 6.02f, col, w); L(10, 12, 13, 8.5f); break;
        case Icon::Trophy:
            P({{6, 3.5f}, {14, 3.5f}, {14, 8}, {13.2f, 10.6f}, {10, 12.5f}, {6.8f, 10.6f}, {6, 8}}, true);
            P({{6, 5}, {3.5f, 5}, {3.5f, 6.8f}, {6.2f, 9.2f}}); P({{14, 5}, {16.5f, 5}, {16.5f, 6.8f}, {13.8f, 9.2f}});
            L(10, 12.5f, 10, 15); L(6.5f, 16.5f, 13.5f, 16.5f); L(7.5f, 15, 12.5f, 15);
            break;
        case Icon::AI:
            R(4.5f, 4.5f, 11, 11, 2.5f);
            for (float t : {8.0f, 12.0f}) { L(t, 2, t, 4.5f); L(t, 15.5f, t, 18); L(2, t, 4.5f, t); L(15.5f, t, 18, t); }
            P({{10, 6.8f}, {10.9f, 9.1f}, {13.2f, 10}, {10.9f, 10.9f}, {10, 13.2f}, {9.1f, 10.9f}, {6.8f, 10}, {9.1f, 9.1f}}, true, true);
            break;
        case Icon::Globe:
            C(10, 10, 7.5f); L(2.5f, 10, 17.5f, 10);
            c.arc(X(10), Y(10), 7.5f * k, -1.5708f, 1.5708f, col, w);
            P({{10, 2.5f}, {7.2f, 6}, {6.4f, 10}, {7.2f, 14}, {10, 17.5f}}); P({{10, 2.5f}, {12.8f, 6}, {13.6f, 10}, {12.8f, 14}, {10, 17.5f}});
            break;
        case Icon::User:
            C(10, 7, 3.5f); c.arc(X(10), Y(18), 6.5f * k, 3.4f, 6.02f, col, w);
            break;
        case Icon::Shield: P({{10, 2.5f}, {16.5f, 5}, {16, 11}, {10, 17.5f}, {4, 11}, {3.5f, 5}}, true); P({{7, 10}, {9.2f, 12.2f}, {13.2f, 7.8f}}); break;
    }
}

// FurUltimate brand mark: neon gradient hexagon badge with a living flame core, orbiting energy arcs and a spark.
void drawLogoMark(Canvas& c, float cx, float cy, float R, float phase, bool glow) {
    const Color gold = Color::hex(0xFFC24A), ember = Color::hex(0xFF5A2E), magenta = Color::hex(0xFF2D8A),
                violet = Color::hex(0x8B5CFF), core = Color::hex(0xFFF5DA);
    const float pulse = 0.5f + 0.5f * std::sin(phase * 7.0f);
    auto ramp = [&](float t) {   // gold -> ember -> magenta -> violet -> gold (closed loop around the badge)
        t -= std::floor(t);
        float k = t * 4.0f; int i = (int)k; float f = k - i;
        const Color stops[5] = {gold, ember, magenta, violet, gold};
        return mix(stops[i], stops[i + 1], f);
    };
    if (glow) {
        c.fillRadial(cx, cy, R * 2.5f, R * 2.5f, magenta.alpha(0.16f + 0.08f * pulse), magenta.alpha(0.0f));
        c.fillRadial(cx, cy + R * 0.2f, R * 1.5f, R * 1.5f, ember.alpha(0.20f + 0.10f * pulse), ember.alpha(0.0f));
    }
    // Hexagon badge (pointy top), dark glass fill + per-edge gradient rim.
    std::vector<Pt> hex, inner;
    for (int k = 0; k < 6; ++k) {
        float a = -1.5707963f + k * 1.0471976f;
        hex.push_back({cx + std::cos(a) * R, cy + std::sin(a) * R});
        inner.push_back({cx + std::cos(a) * R * 0.97f, cy + std::sin(a) * R * 0.97f});
    }
    c.path(inner, true, Color::hex(0x120C1A), 1, true);
    const float rim = std::max(1.3f, R * 0.13f);
    const float spin = phase * 0.35f;
    for (int k = 0; k < 6; ++k) {
        Color col = ramp(k / 6.0f + spin);
        c.line(hex[k].x, hex[k].y, hex[(k + 1) % 6].x, hex[(k + 1) % 6].y, col, rim);
        c.fillCircle(hex[k].x, hex[k].y, rim * 0.5f, col);
    }
    // Orbiting energy arcs and spark.
    const float ro = R * 1.32f, a0 = phase * 2.2f;
    const float aw = std::max(1.0f, R * 0.075f);
    c.arc(cx, cy, ro, a0, a0 + 0.95f, magenta.alpha(0.85f), aw);
    c.arc(cx, cy, ro, a0 + 3.14159f, a0 + 3.14159f + 0.95f, violet.alpha(0.85f), aw);
    c.fillRadial(cx + std::cos(a0 + 0.95f) * ro, cy + std::sin(a0 + 0.95f) * ro, aw * 2.6f, aw * 2.6f, core, core.alpha(0.0f));
    c.fillCircle(cx + std::cos(a0 + 0.95f) * ro, cy + std::sin(a0 + 0.95f) * ro, aw * 0.9f, core);
    // Flame: three nested teardrops with a flickering tip.
    auto flame = [&](float scale, float yOff, float flick, const Color& col) {
        std::vector<Pt> pts;
        const float h = R * 0.60f * scale, w = R * 0.50f * scale, fy = cy + R * 0.10f + yOff;
        for (int i = 0; i < 40; ++i) {
            float th = i / 40.0f * 6.2831853f;
            float x = w * std::sin(th) * std::pow(std::fabs(std::sin(th * 0.5f)), 1.5f);
            float wt = std::pow((1.0f + std::cos(th)) * 0.5f, 3.0f);
            pts.push_back({cx + x + flick * wt * R, fy - h * std::cos(th)});
        }
        c.path(pts, true, col, 1, true);
    };
    const float fl = std::sin(phase * 9.0f) * 0.06f + std::sin(phase * 23.0f) * 0.025f;
    flame(1.0f, 0, fl, ember);
    flame(0.70f, R * 0.14f, fl * 0.7f, mix(gold, ember, 0.25f));
    flame(0.38f, R * 0.30f, fl * 0.4f, core);
}

} // namespace aether
