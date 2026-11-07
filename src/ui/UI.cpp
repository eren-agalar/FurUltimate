#include "UI.h"
#include "../core/Strings.h"
#include "../core/Settings.h"
#include "../core/I18n.h"
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace aether {

static uint64_t hashStr(const std::string& s, uint64_t seed) {
    uint64_t h = seed ^ 1469598103934665603ull;
    for (unsigned char ch : s) { h ^= ch; h *= 1099511628211ull; }
    return h ? h : 1;
}

std::string fmtTemp(float c, int unit) {
    if (std::isnan(c)) return "N/A";
    return format("%.0f %s", displayTemp(c, (TempUnit)unit), tempUnitSuffix((TempUnit)unit));
}

uint64_t UI::id(const std::string& s) const { return hashStr(s, m_idStack.empty() ? 0 : m_idStack.back()); }
void UI::pushId(const std::string& s) { m_idStack.push_back(id(s)); }
void UI::popId() { if (!m_idStack.empty()) m_idStack.pop_back(); }

void UI::beginFrame(const InputState& in, float dt, bool reduceMotion) {
    m_in = in;
    m_dt = std::clamp(dt, 0.0f, 0.1f);
    m_time += m_dt;
    m_reduceMotion = reduceMotion;
    m_animating = false;
    m_hand = false;
    m_prevFocusOrder.swap(m_focusOrder);
    m_focusOrder.clear();
    m_focusNext = m_focusPrev = m_activateFocused = false;
    m_tipSet = false;
    m_lastHovered = 0;
    if (in.key == VK_TAB) { m_focusVisible = true; (in.shift ? m_focusPrev : m_focusNext) = true; }
    if ((in.key == VK_RETURN || in.key == VK_SPACE) && m_focus && m_focusVisible) m_activateFocused = true;
    if (in.pressed) m_focusVisible = false;
    // Resolve Tab navigation against the previous frame's focus order.
    if ((m_focusNext || m_focusPrev) && !m_prevFocusOrder.empty()) {
        auto it = std::find(m_prevFocusOrder.begin(), m_prevFocusOrder.end(), m_focus);
        int idx = it == m_prevFocusOrder.end() ? -1 : (int)(it - m_prevFocusOrder.begin());
        int n = (int)m_prevFocusOrder.size();
        idx = m_focusNext ? (idx + 1) % n : (idx <= 0 ? n - 1 : idx - 1);
        m_focus = m_prevFocusOrder[idx];
    }
    for (auto& t : m_toasts) t.age += m_dt;
    m_toasts.erase(std::remove_if(m_toasts.begin(), m_toasts.end(), [](const Toast& t) { return t.age > 4.2f; }), m_toasts.end());
    if (!m_toasts.empty()) m_animating = true;
}

float UI::anim(uint64_t key, float target, float speed) {
    auto it = m_anim.find(key);
    if (it == m_anim.end()) { m_anim[key] = target; return target; }
    float& v = it->second;
    if (m_reduceMotion) { v = target; return v; }
    float k = 1.0f - std::exp(-speed * m_dt);
    v += (target - v) * k;
    if (std::fabs(target - v) > 0.002f) m_animating = true; else v = target;
    return v;
}

bool UI::hovered(const Rect& r) const {
    if (!m_inputEnabled) return false;
    if (!r.contains(m_in.mx, m_in.my)) return false;
    if (!m_clipStack.empty() && !m_clipStack.back().contains(m_in.mx, m_in.my)) return false;
    return true;
}

bool UI::pressedOn(uint64_t key, const Rect& r, bool focusable) {
    if (focusable && m_inputEnabled) m_focusOrder.push_back(key);
    bool h = hovered(r);
    if (h) { m_hot = key; m_hand = true; m_lastHovered = key; }
    if (h && m_in.pressed) { m_active = key; m_focus = key; }
    bool clicked = h && m_in.released && m_active == key;
    if (m_activateFocused && m_focus == key && m_inputEnabled) { clicked = true; m_activateFocused = false; }
    return clicked;
}

void UI::focusRing(const Rect& r, float radius, uint64_t key) {
    if (!isFocused(key)) return;
    c.strokeRound(r.inset(-3), radius + 3, Theme::current().perf.alpha(0.75f), 1.6f);
}

void UI::label(const std::string& text, const Rect& r, const Color& color, float size, Weight w, Align a, bool display) {
    TextStyle st; st.size = size; st.weight = w; st.align = a; st.display = display;
    c.text(text, r, color, st);
}

void UI::caps(const std::string& text, const Rect& r, const Color& color, Align a) {
    TextStyle st; st.size = 10.5f; st.weight = Weight::SemiBold; st.align = a; st.tracking = 0.9f;
    std::wstring w = widen(text);
    // Locale-aware upper case (Turkish i → İ handled by LCMapStringEx with tr-TR).
    wchar_t buf[512];
    int n = LCMapStringEx(I18n::language() == "tr" ? L"tr-TR" : L"en-US", LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING, w.c_str(), (int)w.size(), buf, 511, nullptr, nullptr, 0);
    std::string up = n > 0 ? narrow(std::wstring(buf, n)) : text;
    c.text(up, r, color, st);
}

void UI::card(const Rect& r, float radius) {
    const Theme& t = Theme::current();
    c.fillRound(r.offset(0, 2), radius, Color(0, 0, 0, t.shadow));
    c.fillRound(r, radius, t.surface);
    c.fillRoundGradient({r.x, r.y, r.w, std::min(r.h, 90.0f)}, radius, t.highlight, FG(0));
    c.strokeRound(r, radius, t.border, 1.0f);
}

void UI::badge(const std::string& text, float x, float y, const Color& color, float* outW, bool right) {
    TextStyle st; st.size = 11; st.weight = Weight::SemiBold; st.align = Align::Center;
    float w = c.measure(text, st) + 16;
    float bx = right ? x - w : x;
    c.fillRound({bx, y, w, 20}, 10, color.alpha(0.14f));
    c.text(text, {bx, y, w, 20}, color, st);
    if (outW) *outW = w;
}

bool UI::button(const std::string& key, const std::string& label, const Rect& r, ButtonStyle style, bool enabled, int icon) {
    const Theme& t = Theme::current();
    uint64_t k = id(key);
    bool clicked = enabled ? pressedOn(k, r) : false;
    if (!enabled && hovered(r)) m_lastHovered = k;
    float h = anim(k ^ 0x11, enabled && hovered(r) ? 1.0f : 0.0f, 18);
    float p = anim(k ^ 0x22, enabled && isActive(k) && hovered(r) ? 1.0f : 0.0f, 30);
    Rect b = r.inset(p * 0.8f);
    float rad = std::min(10.0f, r.h / 2);
    Color fg;
    switch (style) {
        case ButtonStyle::Primary:
            c.fillRound(b.offset(0, 1.5f), rad, Color(0, 0, 0, 0.35f));
            c.fillRoundGradient(b, rad, mix(t.btnTop, t.light ? Color::hex(0x3A414D) : Color::hex(0xFFFFFF), h * 0.6f), mix(t.btnBottom, t.light ? Color::hex(0x2A2F38) : Color::hex(0xDDE3EA), h * 0.5f));
            c.strokeRound(b, rad, t.light ? Color(0, 0, 0, 0.2f) : Color(1, 1, 1, 0.35f), 1);
            fg = t.btnText;
            break;
        case ButtonStyle::Accent:
            c.fillRoundGradient(b, rad, mix(t.perf, Color::hex(0xB5DCFF), h * 0.5f), mix(Color::hex(0x6FB4F2), t.perf, h * 0.5f));
            fg = Color::hex(0x07131F);
            break;
        case ButtonStyle::Secondary:
            c.fillRound(b, rad, mix(t.elevated, t.elevatedHover, h));
            c.strokeRound(b, rad, mix(t.border, t.borderStrong, h), 1);
            fg = t.text;
            break;
        case ButtonStyle::Ghost:
            c.fillRound(b, rad, FG(0.055f * h));
            fg = mix(t.text2, t.text, h);
            break;
        case ButtonStyle::Danger:
            c.fillRound(b, rad, t.critical.alpha(0.13f + 0.09f * h));
            c.strokeRound(b, rad, t.critical.alpha(0.25f + 0.2f * h), 1);
            fg = mix(t.critical, Color::hex(0xFFB3B3), h * 0.5f);
            break;
    }
    if (!enabled) fg = fg.mul(0.4f);
    TextStyle st; st.size = 13; st.weight = Weight::SemiBold; st.align = Align::Center;
    if (icon >= 0) {
        float tw = c.measure(label, st);
        float total = tw + (label.empty() ? 16 : 24);
        float x0 = b.cx() - total / 2;
        drawIcon(c, (Icon)icon, x0, b.cy() - 8, 16, fg, 1.6f);
        st.align = Align::Leading;
        c.text(label, {x0 + 24, b.y, tw + 4, b.h}, fg, st);
    } else {
        c.text(label, b, fg, st);
    }
    if (!enabled) c.fillRound(b, rad, t.bg.alpha(0.35f));
    focusRing(r, rad, k);
    return clicked && enabled;
}

bool UI::iconButton(const std::string& key, Icon icon, const Rect& r, const std::string& tip, bool enabled) {
    const Theme& t = Theme::current();
    uint64_t k = id(key);
    bool clicked = enabled ? pressedOn(k, r) : false;
    float h = anim(k ^ 0x11, enabled && hovered(r) ? 1.0f : 0.0f, 18);
    c.fillRound(r, 9, FG(0.03f + 0.06f * h));
    c.strokeRound(r, 9, mix(t.border, t.borderStrong, h), 1);
    float s = std::min(r.w, r.h) * 0.5f;
    drawIcon(c, icon, r.cx() - s / 2, r.cy() - s / 2, s, enabled ? mix(t.text2, t.text, h) : t.text3, 1.6f);
    if (!tip.empty() && hovered(r)) tooltip(tip);
    focusRing(r, 9, k);
    return clicked;
}

bool UI::segmented(const std::string& key, const Rect& r, const std::vector<std::string>& opts, int& sel, bool enabled) {
    const Theme& t = Theme::current();
    uint64_t k = id(key);
    int n = (int)opts.size();
    if (n == 0) return false;
    bool changed = false;
    c.fillRound(r, 10, t.bg2);
    c.strokeRound(r, 10, t.border, 1);
    float segW = (r.w - 4) / n;
    float target = 2 + segW * std::clamp(sel, 0, n - 1);
    float px = anim(k ^ 0x33, target, 16);
    Rect pill{r.x + px, r.y + 2, segW, r.h - 4};
    c.fillRound(pill.offset(0, 1), 8, Color(0, 0, 0, t.light ? 0.08f : 0.3f));
    c.fillRoundGradient(pill, 8, t.light ? Color::hex(0xFFFFFF) : t.elevatedHover, t.light ? Color::hex(0xFFFFFF) : t.elevated);
    c.strokeRound(pill, 8, FG(0.07f), 1);
    m_focusOrder.push_back(k);
    for (int i = 0; i < n; ++i) {
        Rect s{r.x + 2 + segW * i, r.y, segW, r.h};
        uint64_t sk = k ^ (uint64_t)(i + 1) * 0x9E3779B97F4A7C15ull;
        bool click = enabled && pressedOn(sk, s, false);
        float h = anim(sk ^ 0x44, enabled && hovered(s) ? 1.0f : 0.0f, 18);
        if (click && sel != i) { sel = i; changed = true; m_focus = k; }
        TextStyle st; st.size = 12.5f; st.weight = i == sel ? Weight::SemiBold : Weight::Medium; st.align = Align::Center;
        Color fg = i == sel ? t.text : mix(t.text2, t.text, h * 0.6f);
        if (!enabled) fg = fg.mul(0.45f);
        c.text(opts[i], s.inset(6, 0), fg, st);
    }
    if (enabled && m_focus == k && m_focusVisible) {
        if (m_in.key == VK_LEFT && sel > 0) { --sel; changed = true; }
        if (m_in.key == VK_RIGHT && sel < n - 1) { ++sel; changed = true; }
    }
    focusRing(r, 10, k);
    return changed;
}

bool UI::toggle(const std::string& key, const Rect& r, bool& v, bool enabled) {
    const Theme& t = Theme::current();
    uint64_t k = id(key);
    Rect tr{r.right() - 40, r.cy() - 11, 40, 22};
    bool clicked = enabled && pressedOn(k, r);
    if (clicked) v = !v;
    float on = anim(k ^ 0x55, v ? 1.0f : 0.0f, 18);
    c.fillRound(tr, 11, mix(FG(0.10f), t.perf.alpha(enabled ? 0.95f : 0.4f), on));
    float kx = tr.x + 11 + on * 18;
    c.fillCircle(kx, tr.cy() + 0.8f, 8.5f, Color(0, 0, 0, 0.3f));
    c.fillCircle(kx, tr.cy(), 8.5f, enabled ? Color::hex(0xF5F6F8) : Color::hex(0x8A8F98));
    focusRing(tr, 11, k);
    return clicked;
}

bool UI::sliderFloat(const std::string& key, const Rect& r, float& v, float minV, float maxV, float step, bool enabled) {
    const Theme& t = Theme::current();
    uint64_t k = id(key);
    m_focusOrder.push_back(k);
    bool h = enabled && hovered(r);
    if (h) m_hand = true;
    if (h && m_in.pressed) { m_active = k; m_focus = k; }
    bool changed = false;
    float x0 = r.x + 8, x1 = r.right() - 8;
    if (enabled && m_active == k && m_in.down) {
        float f = std::clamp((m_in.mx - x0) / (x1 - x0), 0.0f, 1.0f);
        float nv = minV + f * (maxV - minV);
        if (step > 0) nv = std::round(nv / step) * step;
        nv = std::clamp(nv, minV, maxV);
        if (nv != v) { v = nv; changed = true; }
    }
    if (enabled && m_focus == k && m_focusVisible) {
        if (m_in.key == VK_LEFT) { v = std::max(minV, v - step); changed = true; }
        if (m_in.key == VK_RIGHT) { v = std::min(maxV, v + step); changed = true; }
    }
    float f = maxV > minV ? (v - minV) / (maxV - minV) : 0;
    float af = anim(k ^ 0x66, f, 24);
    float cy = r.cy();
    c.fillRound({x0, cy - 2, x1 - x0, 4}, 2, FG(0.09f));
    c.fillRoundGradientH({x0, cy - 2, std::max(4.0f, (x1 - x0) * af), 4}, 2, t.perf.alpha(enabled ? 0.55f : 0.25f), t.perf.alpha(enabled ? 1 : 0.4f));
    float kx = x0 + (x1 - x0) * af;
    float hv = anim(k ^ 0x77, (h || m_active == k) ? 1.0f : 0.0f, 18);
    c.fillCircle(kx, cy + 1, 8 + hv, Color(0, 0, 0, 0.35f));
    c.fillCircle(kx, cy, 7 + hv, enabled ? Color::hex(0xF1F3F6) : Color::hex(0x7A7F88));
    focusRing({kx - 9, cy - 9, 18, 18}, 9, k);
    return changed;
}

bool UI::sliderInt(const std::string& key, const Rect& r, int& value, int minV, int maxV, int step, bool enabled) {
    float f = (float)value;
    bool ch = sliderFloat(key, r, f, (float)minV, (float)maxV, (float)step, enabled);
    int nv = (int)std::lround(f);
    if (nv != value) { value = nv; return true; }
    return ch && false;
}

bool UI::textField(const std::string& key, const Rect& r, std::string& value, size_t maxChars, const std::string& placeholder,
                   bool enabled) {
    const Theme& t = Theme::current();
    uint64_t k = id(key);
    bool h = enabled && hovered(r);
    if (h) m_hand = true;
    if (enabled && m_inputEnabled && m_in.pressed) {
        if (h) { m_textFocus = k; m_focus = k; }
        else if (m_textFocus == k) m_textFocus = 0;
    }
    if (enabled && m_inputEnabled) m_focusOrder.push_back(k);
    if (m_focus == k && m_focusVisible) m_textFocus = k;   // reached with Tab
    bool focused = m_textFocus == k && enabled;
    if (focused) m_textSeen = true;
    bool submitted = false;
    if (focused) {
        std::wstring w = widen(value);
        for (wchar_t ch : m_in.chars) {
            if (ch == L'\b') { if (!w.empty()) w.pop_back(); }
            else if (ch == L'\r' || ch == L'\n') submitted = true;
            else if (ch >= 32 && w.size() < maxChars) w.push_back(ch);
        }
        if (m_in.key == VK_ESCAPE) m_textFocus = 0;
        value = narrow(w);
        m_animating = true;   // caret blink
    }
    float fa = anim(k ^ 0xF0C, focused ? 1.0f : 0.0f, 16);
    c.fillRound(r, 10, t.bg2);
    c.strokeRound(r, 10, mix(h ? t.borderStrong : t.border, t.perf.alpha(0.8f), fa), focused ? 1.5f : 1.0f);
    TextStyle st; st.size = 13.5f;
    Rect tr{r.x + 12, r.y, r.w - 24, r.h};
    if (value.empty() && !focused) c.text(placeholder, tr, t.text3, st);
    else c.text(value, tr, enabled ? t.text : t.text3, st);
    if (focused && std::fmod(m_time, 1.0f) < 0.6f) {
        float tw = value.empty() ? 0 : c.measure(value, st);
        float cx = std::min(tr.x + tw + 1, tr.right());
        c.line(cx, r.cy() - 9, cx, r.cy() + 9, t.perf, 1.5f);
    }
    return submitted;
}

bool UI::selectableRow(const std::string& key, const Rect& r, bool selected) {
    const Theme& t = Theme::current();
    uint64_t k = id(key);
    bool clicked = pressedOn(k, r);
    float h = anim(k ^ 0x88, hovered(r) ? 1.0f : 0.0f, 18);
    float s = anim(k ^ 0x99, selected ? 1.0f : 0.0f, 16);
    if (s > 0.01f) {
        c.fillRound(r, 10, t.elevated.mul(s));
        c.strokeRound(r, 10, t.borderStrong.mul(s), 1);
    }
    if (h > 0.01f) c.fillRound(r, 10, FG(0.035f * h));
    focusRing(r, 10, k);
    return clicked;
}

void UI::progressBar(const Rect& r, float frac, const Color& color, bool animated) {
    frac = std::clamp(frac, 0.0f, 1.0f);
    float rad = r.h / 2;
    c.fillRound(r, rad, FG(0.07f));
    if (frac > 0) {
        Rect f{r.x, r.y, std::max(r.h, r.w * frac), r.h};
        c.fillRoundGradientH(f, rad, color.alpha(0.55f), color);
        if (animated && !m_reduceMotion) {   // subtle moving sheen
            float sx = std::fmod(m_time * 0.6f, 1.6f) - 0.3f;
            float x = f.x + f.w * sx;
            c.pushClip(f);
            c.fillRoundGradientH({x - 40, f.y, 40, f.h}, 0, Color(1, 1, 1, 0), Color(1, 1, 1, 0.25f));
            c.fillRoundGradientH({x, f.y, 40, f.h}, 0, Color(1, 1, 1, 0.25f), Color(1, 1, 1, 0));
            c.popClip();
            m_animating = true;
        }
    }
}

void UI::ringGauge(float cx, float cy, float rad, float frac, const Color& color, float th) {
    const float a0 = 2.356f, sweep = 4.712f;   // 270° gauge, opening at the bottom
    c.arc(cx, cy, rad, a0, a0 + sweep, FG(0.07f), th);
    if (frac > 0.001f) c.arc(cx, cy, rad, a0, a0 + sweep * std::clamp(frac, 0.0f, 1.0f), color, th);
}

void UI::tooltip(const std::string& text) {
    if (m_lastHovered == 0) return;
    if (m_tipOwner != m_lastHovered) { m_tipOwner = m_lastHovered; m_tipTime = 0; }
    m_tipText = text;
    m_tipSet = true;
}

void UI::hint(const std::string& key, const Rect& r, const std::string& text) {
    if (!hovered(r)) return;
    m_lastHovered = id(key);
    tooltip(text);
}

float UI::beginScroll(const std::string& key, const Rect& view) {
    uint64_t k = id(key);
    float& target = m_scrollTarget[k];
    float content = m_scrollContent.count(k) ? m_scrollContent[k] : view.h;
    float maxOff = std::max(0.0f, content - view.h);
    if (hovered(view) && m_in.wheel != 0) target -= m_in.wheel * 90.0f;
    target = std::clamp(target, 0.0f, maxOff);
    float off = anim(k ^ 0xABC, target, 20);
    off = std::clamp(off, 0.0f, maxOff);
    c.pushClip(view);
    m_clipStack.push_back(view);
    m_scrollStack.push_back({k, view, off});
    return off;
}

void UI::endScroll(float contentHeight) {
    if (m_scrollStack.empty()) return;
    ScrollCtx s = m_scrollStack.back();
    m_scrollStack.pop_back();
    m_clipStack.pop_back();
    c.popClip();
    m_scrollContent[s.id] = contentHeight;
    if (contentHeight > s.view.h + 1) {   // minimal overlay scrollbar
        float ratio = s.view.h / contentHeight;
        float barH = std::max(30.0f, s.view.h * ratio);
        float maxOff = contentHeight - s.view.h;
        float y = s.view.y + (s.view.h - barH) * (maxOff > 0 ? s.offset / maxOff : 0);
        float vis = anim(s.id ^ 0xDEF, hovered(s.view) ? 1.0f : 0.35f, 8);
        c.fillRound({s.view.right() - 6, y, 3.5f, barH}, 2, FG(0.16f * vis));
    }
}

void UI::toast(const std::string& text, ToastKind kind) {
    m_toasts.push_back({text, kind, 0});
    if (m_toasts.size() > 4) m_toasts.erase(m_toasts.begin());
}

void UI::endFrame() {
    if (!m_textSeen) m_textFocus = 0;   // the focused field was not drawn this frame (page changed)
    m_textSeen = false;
    // Release the active widget only after this frame's widgets have seen the button-up event,
    // otherwise the click (press + release on the same widget) is never recognised.
    if (!m_in.down) m_active = 0;
    const Theme& t = Theme::current();
    // Tooltip
    if (m_tipSet && m_tipOwner == m_lastHovered) {
        m_tipTime += m_dt;
        if (m_tipTime > 0.45f) {
            TextStyle st; st.size = 12; st.wrap = true;
            float th = 0;
            float tw = std::min(320.0f, c.measure(m_tipText, st, 320, &th)) + 2;
            c.measure(m_tipText, st, tw, &th);
            float x = std::min(m_in.mx + 14, c.width() - tw - 28), y = m_in.my + 20;
            if (y + th + 16 > c.height()) y = m_in.my - th - 24;
            float a = std::min(1.0f, (m_tipTime - 0.45f) * 8);
            Rect r{x, y, tw + 20, th + 14};
            c.fillRound(r.offset(0, 3), 8, Color(0, 0, 0, 0.35f * a));
            c.fillRound(r, 8, t.elevated.alpha(0.98f * a));
            c.strokeRound(r, 8, t.borderStrong.mul(a), 1);
            c.text(m_tipText, {x + 10, y + 7, tw, th}, t.text.mul(a), st);
            if (a < 1) m_animating = true;
        } else {
            m_animating = true;
        }
    } else {
        m_tipOwner = 0; m_tipTime = 0;
    }
    // Toasts (bottom-right)
    float y = c.height() - 24;
    for (int i = (int)m_toasts.size() - 1; i >= 0; --i) {
        Toast& tt = m_toasts[i];
        float in = std::min(1.0f, tt.age * 6), out = std::clamp((4.2f - tt.age) * 3, 0.0f, 1.0f);
        float a = std::min(in, out);
        TextStyle st; st.size = 13; st.weight = Weight::Medium;
        float w = std::min(420.0f, c.measure(tt.text, st) + 56);
        Rect r{c.width() - w - 24, y - 46 + (1 - in) * 12, w, 44};
        Color kc = tt.kind == ToastKind::Success ? t.success : tt.kind == ToastKind::Warning ? t.warning : tt.kind == ToastKind::Error ? t.critical : t.perf;
        c.fillRound(r.offset(0, 4), 12, Color(0, 0, 0, 0.4f * a));
        c.fillRound(r, 12, t.elevated.alpha(0.97f * a));
        c.strokeRound(r, 12, t.borderStrong.mul(a), 1);
        c.fillCircle(r.x + 20, r.cy(), 4, kc.mul(a));
        c.text(tt.text, {r.x + 34, r.y, r.w - 44, r.h}, t.text.mul(a), st);
        y -= 54;
    }
}

} // namespace aether
