#pragma once
#include "Canvas.h"
#include "Icons.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace aether {

struct InputState {
    float mx = -1, my = -1;
    bool down = false, pressed = false, released = false;
    float wheel = 0;
    int key = 0;                 // virtual key pressed this frame (0 = none)
    bool shift = false, ctrl = false;
    std::wstring chars;          // text typed this frame (WM_CHAR)
};

enum class ButtonStyle { Primary, Secondary, Ghost, Danger, Accent };
enum class ToastKind { Info, Success, Warning, Error };

// Immediate-mode widget layer with animation state, keyboard focus, tooltips, toasts and scrolling.
class UI {
public:
    explicit UI(Canvas& c) : c(c) {}
    Canvas& c;

    void beginFrame(const InputState& in, float dt, bool reduceMotion);
    void endFrame();
    bool animating() const { return m_animating; }
    bool wantsHandCursor() const { return m_hand; }
    float time() const { return m_time; }
    float dt() const { return m_dt; }
    const InputState& input() const { return m_in; }

    // --- identity & state
    uint64_t id(const std::string& s) const;
    void pushId(const std::string& s);
    void popId();
    float anim(uint64_t id, float target, float speed = 14.0f);
    void setInputEnabled(bool e) { m_inputEnabled = e; }
    bool inputEnabled() const { return m_inputEnabled; }

    // --- interaction primitives
    bool hovered(const Rect& r) const;
    bool pressedOn(uint64_t id, const Rect& r, bool focusable = true);   // returns true when clicked (or activated by keyboard)
    bool isActive(uint64_t id) const { return m_active == id; }
    bool isFocused(uint64_t id) const { return m_focus == id && m_focusVisible; }
    void focusRing(const Rect& r, float radius, uint64_t id);

    // --- widgets
    bool button(const std::string& key, const std::string& label, const Rect& r, ButtonStyle style = ButtonStyle::Secondary,
                bool enabled = true, int icon = -1);
    bool iconButton(const std::string& key, Icon icon, const Rect& r, const std::string& tip, bool enabled = true);
    bool segmented(const std::string& key, const Rect& r, const std::vector<std::string>& options, int& selected, bool enabled = true);
    bool toggle(const std::string& key, const Rect& r, bool& value, bool enabled = true);
    bool sliderInt(const std::string& key, const Rect& r, int& value, int minV, int maxV, int step = 1, bool enabled = true);
    bool sliderFloat(const std::string& key, const Rect& r, float& value, float minV, float maxV, float step, bool enabled = true);
    bool selectableRow(const std::string& key, const Rect& r, bool selected);
    // Single-line text input. Returns true when Enter is pressed while focused.
    bool textField(const std::string& key, const Rect& r, std::string& value, size_t maxChars, const std::string& placeholder,
                   bool enabled = true);
    bool textFieldFocused() const { return m_textFocus != 0; }
    void progressBar(const Rect& r, float frac, const Color& color, bool animated = true);
    void ringGauge(float cx, float cy, float radius, float frac, const Color& color, float thickness);

    // Surfaces
    void card(const Rect& r, float radius = 14.0f);
    void badge(const std::string& text, float x, float y, const Color& color, float* outW = nullptr, bool right = false);
    void label(const std::string& text, const Rect& r, const Color& color, float size = 13.5f, Weight w = Weight::Regular,
               Align a = Align::Leading, bool display = false);
    void caps(const std::string& text, const Rect& r, const Color& color, Align a = Align::Leading);
    void tooltip(const std::string& text);   // attaches to the most recent hovered widget
    void hint(const std::string& key, const Rect& r, const std::string& text);   // tooltip for a passive region

    // Scroll regions
    float beginScroll(const std::string& key, const Rect& view);
    void endScroll(float contentHeight);

    // Toasts
    void toast(const std::string& text, ToastKind kind = ToastKind::Info);

private:
    InputState m_in;
    float m_dt = 0.016f, m_time = 0;
    bool m_reduceMotion = false;
    bool m_animating = false, m_hand = false, m_inputEnabled = true;
    uint64_t m_hot = 0, m_active = 0, m_focus = 0;
    bool m_focusVisible = false;
    std::vector<uint64_t> m_focusOrder, m_prevFocusOrder;
    bool m_focusNext = false, m_focusPrev = false, m_activateFocused = false;
    std::vector<uint64_t> m_idStack;
    std::unordered_map<uint64_t, float> m_anim;
    std::unordered_map<uint64_t, float> m_scroll, m_scrollTarget, m_scrollContent;
    struct ScrollCtx { uint64_t id; Rect view; float offset; };
    std::vector<ScrollCtx> m_scrollStack;
    std::vector<Rect> m_clipStack;
    uint64_t m_tipOwner = 0; float m_tipTime = 0; std::string m_tipText; uint64_t m_lastHovered = 0; bool m_tipSet = false;
    struct Toast { std::string text; ToastKind kind; float age; };
    std::vector<Toast> m_toasts;
    float m_sliderGrab = 0;
    uint64_t m_textFocus = 0;
    bool m_textSeen = false;
};

std::string fmtTemp(float celsius, int unit);   // "64 °C" / "N/A"

} // namespace aether
