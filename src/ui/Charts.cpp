#include "Charts.h"
#include "../core/Strings.h"
#include "../core/I18n.h"
#include <algorithm>
#include <cmath>

namespace aether {

void lineChart(UI& ui, const std::string& key, const Rect& r, const std::vector<ChartSeries>& series, const ChartOptions& opt) {
    Canvas& c = ui.c;
    const Theme& t = Theme::current();
    float lo = opt.minY, hi = opt.maxY;
    if (hi <= lo) {
        hi = lo + 1;
        bool anyValue = false;
        for (auto& s : series)
            for (float v : s.values) if (!std::isnan(v)) { hi = std::max(hi, v); anyValue = true; }
        if (!anyValue) hi = lo + 100;
        float mag = std::pow(10.0f, std::floor(std::log10(std::max(1e-3f, hi))));
        hi = std::ceil(hi * 1.12f / (mag * 0.5f)) * mag * 0.5f;
    }
    float axisW = opt.axisLabels ? 38.0f : 0.0f, timeH = (opt.axisLabels && opt.spanSeconds > 0) ? 18.0f : 0.0f;
    Rect plot{r.x + axisW, r.y + 6, r.w - axisW - 4, r.h - timeH - 10};
    TextStyle ax; ax.size = 10.5f; ax.align = Align::Trailing; ax.weight = Weight::Medium;
    if (opt.grid) {
        for (int i = 0; i <= 4; ++i) {
            float y = plot.y + plot.h * i / 4.0f;
            c.line(plot.x, y, plot.right(), y, FG(i == 4 ? 0.07f : 0.035f), 1);
            if (opt.axisLabels) {
                float v = hi - (hi - lo) * i / 4.0f;
                c.text(format(opt.decimals ? "%.1f" : "%.0f", v), {r.x, y - 8, axisW - 8, 16}, t.text3, ax);
            }
        }
    }
    if (timeH > 0) {
        TextStyle ts = ax; ts.align = Align::Leading;
        c.text(format("-%s", formatDuration(opt.spanSeconds).c_str()), {plot.x, plot.bottom() + 4, 80, 14}, t.text3, ts);
        ts.align = Align::Trailing;
        c.text(T("şimdi"), {plot.right() - 80, plot.bottom() + 4, 80, 14}, t.text3, ts);
    }
    bool any = false;
    for (auto& s : series) {
        size_t n = s.values.size();
        if (n < 2) continue;
        std::vector<Pt> seg;
        auto flush = [&] {
            if (seg.size() >= 2) {
                if (s.fill) c.fillArea(seg, plot.bottom(), s.color.alpha(0.20f), s.color.alpha(0.0f));
                c.polyline(seg, s.color, 1.7f);
            }
            seg.clear();
        };
        for (size_t i = 0; i < n; ++i) {
            float v = s.values[i];
            if (std::isnan(v)) { flush(); continue; }
            float x = plot.x + plot.w * (float)i / (float)(n - 1);
            float y = plot.bottom() - plot.h * std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
            seg.push_back({x, y});
        }
        if (!seg.empty()) {
            Pt last = seg.back();
            flush();
            c.fillCircle(last.x, last.y, 7, s.color.alpha(0.18f));
            c.fillCircle(last.x, last.y, 3, s.color);
            any = true;
        }
    }
    if (!any) {
        TextStyle st; st.size = 12.5f; st.align = Align::Center;
        c.text(T("Veri bekleniyor…"), plot, t.text3, st);
    }
    // Hover crosshair with values.
    if (opt.hover && any && ui.hovered(plot)) {
        float mx = ui.input().mx;
        float f = (mx - plot.x) / plot.w;
        c.line(mx, plot.y, mx, plot.bottom(), FG(0.14f), 1);
        std::vector<std::string> lines;
        for (auto& s : series) {
            if (s.values.size() < 2) continue;
            size_t idx = (size_t)std::lround(f * (s.values.size() - 1));
            idx = std::min(idx, s.values.size() - 1);
            float v = s.values[idx];
            float y = plot.bottom() - plot.h * std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
            if (!std::isnan(v)) c.fillCircle(plot.x + plot.w * idx / (float)(s.values.size() - 1), y, 3.5f, s.color);
            lines.push_back(s.name + ": " + (std::isnan(v) ? std::string("N/A") : format(opt.decimals ? "%.1f %s" : "%.0f %s", v, s.unit.c_str())));
        }
        TextStyle st; st.size = 11.5f; st.weight = Weight::Medium;
        float w = 0;
        for (auto& l : lines) w = std::max(w, c.measure(l, st));
        float h = 10 + 17.0f * lines.size();
        float bx = mx + 12 + w + 20 > plot.right() ? mx - 12 - w - 20 : mx + 12;
        Rect box{bx, plot.y + 4, w + 20, h};
        c.fillRound(box, 8, t.elevated.alpha(0.96f));
        c.strokeRound(box, 8, t.borderStrong, 1);
        for (size_t i = 0; i < lines.size(); ++i) c.text(lines[i], {box.x + 10, box.y + 5 + 17.0f * i, w + 4, 17}, t.text, st);
    }
}

} // namespace aether
