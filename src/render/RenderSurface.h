#pragma once
#include "../gpu/GpuDevice.h"
#include <d2d1_1.h>
#include <dwrite.h>
#include <atomic>
#include <string>

namespace aether {

// Colours of the in-window status panel (taken from the active UI theme).
struct OverlayPalette {
    uint32_t panel = 0x0B0C0F, text = 0xF5F6F8, text2 = 0x9198A3, text3 = 0x5F6671;
    uint32_t accent = 0x8FC9FF, warn = 0xE8B65B, crit = 0xE36C6C, success = 0x7FD1A8;
    float panelAlpha = 0.80f;
};

struct SurfaceOptions {
    uint32_t width = 1280, height = 720;
    bool fullscreen = false;           // borderless fullscreen on the chosen monitor
    HMONITOR monitor = nullptr;
    std::wstring title = L"FurUltimate";
};

// Owns a workload window, D3D11 device, flip-model swap chain and a Direct2D overlay.
// Must be created, used and destroyed on the same (worker) thread.
class RenderSurface {
public:
    ~RenderSurface();
    bool create(const LUID& adapter, const SurfaceOptions& opt, std::string& error);
    void destroy();
    // Pumps window messages; returns false when the user closed the window.
    bool pump();
    // Handles pending window resizes. Returns false on failure (device lost).
    bool handleResize();

    ID3D11Device* device() const { return m_device.Get(); }
    ID3D11DeviceContext* context() const { return m_context.Get(); }
    ID3D11RenderTargetView* backbuffer() const { return m_rtv.Get(); }
    uint32_t width() const { return m_w; }
    uint32_t height() const { return m_h; }
    HWND hwnd() const { return m_hwnd; }

    HRESULT present(bool vsync);
    // Checks device health; returns S_OK or the removal reason.
    HRESULT deviceStatus() const;
    bool saveScreenshot(const std::wstring& path);

    // Overlay (Direct2D) — call between beginOverlay()/endOverlay(), after 3D rendering, before present().
    bool beginOverlay();
    void endOverlay();
    void text(float x, float y, const std::wstring& s, float size, uint32_t rgb, float alpha = 1.0f, bool semibold = false,
              float maxWidth = 900.0f);
    void panel(float x, float y, float w, float h, float radius, uint32_t rgb, float alpha);
    void bar(float x, float y, float w, float h, float frac, uint32_t rgb);
    void textRight(float xRight, float y, const std::wstring& s, float size, uint32_t rgb, float alpha = 1.0f, bool semibold = false);
    void textCenter(float cx, float y, const std::wstring& s, float size, uint32_t rgb, float alpha = 1.0f, bool semibold = false);
    void arc(float cx, float cy, float r, float a0, float a1, uint32_t rgb, float alpha, float width);
    void circle(float cx, float cy, float r, uint32_t rgb, float alpha);
    void hline(float x0, float x1, float y, uint32_t rgb, float alpha);

    // Input flags raised by the window (consumed by the engine).
    std::atomic<bool> escPressed{false}, spacePressed{false}, screenshotRequested{false}, closeRequested{false};

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    bool createTargets();
    void releaseTargets();
    IDWriteTextFormat* format(float size, bool semibold);

    HWND m_hwnd = nullptr;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain1> m_swap;
    ComPtr<ID3D11RenderTargetView> m_rtv;
    uint32_t m_w = 0, m_h = 0;
    std::atomic<bool> m_resizePending{false};
    bool m_tearing = false;
    D3D_FEATURE_LEVEL m_fl{};

    ComPtr<ID2D1Factory1> m_d2dFactory;
    ComPtr<ID2D1Device> m_d2dDevice;
    ComPtr<ID2D1DeviceContext> m_d2d;
    ComPtr<ID2D1Bitmap1> m_d2dTarget;
    ComPtr<ID2D1SolidColorBrush> m_brush;
    ComPtr<ID2D1StrokeStyle> m_roundStroke;
    ComPtr<IDWriteTextFormat> m_formats[8];
    float m_formatSizes[8] = {};
    bool m_formatSemi[8] = {};
    float m_dpiScale = 1.0f;
    int m_presentCount = 0;
};

} // namespace aether
