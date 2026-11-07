#pragma once
#include "Theme.h"
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <map>
#include <string>
#include <vector>

namespace aether {

using Microsoft::WRL::ComPtr;

struct Pt { float x = 0, y = 0; };

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Rect inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect inset(float dx, float dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    Rect offset(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
    // "Rect-cut" layout helpers: remove a slice from this rect and return it.
    Rect cutTop(float a) { Rect r{x, y, w, a}; y += a; h -= a; return r; }
    Rect cutBottom(float a) { Rect r{x, y + h - a, w, a}; h -= a; return r; }
    Rect cutLeft(float a) { Rect r{x, y, a, h}; x += a; w -= a; return r; }
    Rect cutRight(float a) { Rect r{x + w - a, y, a, h}; w -= a; return r; }
};

enum class Align { Leading, Center, Trailing };
enum class Weight { Regular = 400, Medium = 500, SemiBold = 600, Light = 300 };

struct TextStyle {
    float size = 13.5f;
    Weight weight = Weight::Regular;
    bool display = false;            // Inter Display (large numerals / headings)
    Align align = Align::Leading;
    Align valign = Align::Center;
    bool wrap = false;
    float tracking = 0;              // letter spacing in DIP (uppercase labels)
};

// Direct2D canvas bound to a window swap chain. All coordinates in DIPs.
class Canvas {
public:
    bool init(HWND hwnd, std::string& error);
    void shutdown();
    void setDpi(float dpi);
    float dpi() const { return m_dpi; }
    float scale() const { return m_dpi / 96.0f; }
    bool resize(uint32_t pxW, uint32_t pxH);
    float width() const { return m_pxW / scale(); }
    float height() const { return m_pxH / scale(); }
    bool begin();
    // Ends drawing and presents. Returns false if the device was lost and has been recreated.
    bool end();

    void clear(const Color& c);
    void fillRect(const Rect& r, const Color& c);
    void fillRound(const Rect& r, float radius, const Color& c);
    void strokeRound(const Rect& r, float radius, const Color& c, float width = 1.0f);
    void fillRoundGradient(const Rect& r, float radius, const Color& top, const Color& bottom);
    void fillRoundGradientH(const Rect& r, float radius, const Color& left, const Color& right);
    void fillRadial(float cx, float cy, float rx, float ry, const Color& inner, const Color& outer);
    void line(float x1, float y1, float x2, float y2, const Color& c, float width = 1.0f);
    void fillCircle(float cx, float cy, float r, const Color& c);
    void strokeCircle(float cx, float cy, float r, const Color& c, float width = 1.0f);
    void arc(float cx, float cy, float r, float a0, float a1, const Color& c, float width, bool roundCaps = true);
    void polyline(const std::vector<Pt>& pts, const Color& c, float width = 1.5f, bool round = true);
    void fillArea(const std::vector<Pt>& pts, float baseline, const Color& top, const Color& bottom);
    void path(const std::vector<Pt>& pts, bool closed, const Color& c, float width, bool fill = false);

    void text(const std::string& utf8, const Rect& r, const Color& c, const TextStyle& st = {});
    float measure(const std::string& utf8, const TextStyle& st, float maxWidth = 10000.0f, float* outHeight = nullptr);

    void pushClip(const Rect& r);
    void popClip();
    void pushOpacity(float a);
    void popOpacity();
    void setTranslation(float dx, float dy);

    ID2D1DeviceContext* d2d() const { return m_ctx.Get(); }
    bool captureToPng(const std::wstring& path);
    // Captures the next presented frame (taken after EndDraw, before Present).
    void requestCapture(const std::wstring& path) { m_capturePath = path; }
    bool takeCaptureResult(std::wstring& path, bool& ok);

private:
    bool createDevice(std::string& error);
    bool createTargets();
    void releaseTargets();
    IDWriteTextFormat* format(const TextStyle& st);
    ID2D1SolidColorBrush* brush(const Color& c);
    ID2D1GradientStopCollection* stops(const Color& a, const Color& b);

    HWND m_hwnd = nullptr;
    float m_dpi = 96;
    uint32_t m_pxW = 1, m_pxH = 1;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_d3dctx;
    ComPtr<IDXGISwapChain1> m_swap;
    ComPtr<ID2D1Factory1> m_factory;
    ComPtr<ID2D1Device> m_d2dDevice;
    ComPtr<ID2D1DeviceContext> m_ctx;
    ComPtr<ID2D1Bitmap1> m_target;
    ComPtr<ID2D1SolidColorBrush> m_brush;
    ComPtr<ID2D1StrokeStyle> m_roundStroke;
    std::map<uint64_t, ComPtr<IDWriteTextFormat>> m_formats;
    std::map<std::string, ComPtr<ID2D1GradientStopCollection>> m_stops;
    std::vector<int> m_layerStack;
    bool m_drawing = false;
    std::wstring m_capturePath, m_capturedPath;
    int m_captureState = 0;   // 0 none, 1 ok, 2 failed
};

std::wstring utf8w(const std::string& s);

} // namespace aether
