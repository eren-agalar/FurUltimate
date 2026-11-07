#include "RenderSurface.h"
#include "../ui/Fonts.h"
#include "../core/Log.h"
#include "../core/Image.h"
#include "../resource.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace aether {

static const wchar_t* kClassName = L"FurUltimateWorkload";

RenderSurface::~RenderSurface() { destroy(); }

LRESULT CALLBACK RenderSurface::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (RenderSurface*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = (CREATESTRUCTW*)lp;
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            break;
        }
        case WM_SIZE:
            if (self && wp != SIZE_MINIMIZED) self->m_resizePending = true;
            return 0;
        case WM_KEYDOWN:
            if (!self) break;
            if (wp == VK_ESCAPE) self->escPressed = true;
            else if (wp == VK_SPACE) self->spacePressed = true;
            else if (wp == VK_F12 || wp == 'S') self->screenshotRequested = true;
            return 0;
        case WM_CLOSE:
            if (self) self->closeRequested = true;
            return 0;   // the engine decides when to destroy the window
        case WM_ERASEBKGND:
            return 1;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

bool RenderSurface::create(const LUID& adapter, const SurfaceOptions& opt, std::string& error) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);   // may already be registered by a previous run

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT r{0, 0, (LONG)opt.width, (LONG)opt.height};
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (opt.fullscreen) {
        style = WS_POPUP;
        MONITORINFO mi{sizeof(mi)};
        HMONITOR mon = opt.monitor ? opt.monitor : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        GetMonitorInfoW(mon, &mi);
        r = mi.rcMonitor;
        x = r.left; y = r.top;
    } else {
        UINT dpi = 96;
        HMONITOR mon = opt.monitor ? opt.monitor : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(mon, &mi);
        AdjustWindowRectEx(&r, style, FALSE, 0);
        int ww = r.right - r.left, wh = r.bottom - r.top;
        x = mi.rcWork.left + std::max(0, (int)(mi.rcWork.right - mi.rcWork.left - ww) / 2);
        y = mi.rcWork.top + std::max(0, (int)(mi.rcWork.bottom - mi.rcWork.top - wh) / 2);
        r = RECT{0, 0, ww, wh};
        (void)dpi;
    }
    m_hwnd = CreateWindowExW(0, kClassName, opt.title.c_str(), style | WS_VISIBLE, x, y,
                             opt.fullscreen ? r.right - r.left : r.right, opt.fullscreen ? r.bottom - r.top : r.bottom,
                             nullptr, nullptr, inst, this);
    if (!m_hwnd) { error = "Test penceresi oluşturulamadı"; return false; }
    SetForegroundWindow(m_hwnd);

    HRESULT hr = GpuDeviceManager::createDevice(adapter, m_device, m_context, m_fl);
    if (FAILED(hr)) { error = "Direct3D 11 cihazı oluşturulamadı (" + GpuDeviceManager::hresultText(hr) + ")"; return false; }
    if (m_fl < D3D_FEATURE_LEVEL_11_0) { error = "Bu GPU Direct3D 11 özellik seviyesi 11_0 desteklemiyor"; return false; }

    ComPtr<IDXGIDevice1> dxgiDev;
    m_device.As(&dxgiDev);
    dxgiDev->SetMaximumFrameLatency(2);
    ComPtr<IDXGIAdapter> dxgiAdapter;
    dxgiDev->GetAdapter(dxgiAdapter.GetAddressOf());
    ComPtr<IDXGIFactory2> factory;
    dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), (void**)factory.GetAddressOf());
    if (!factory) { error = "DXGI 1.2 gerekli"; return false; }

    m_tearing = GpuDeviceManager::tearingSupported();
    RECT cr; GetClientRect(m_hwnd, &cr);
    m_w = std::max<LONG>(1, cr.right - cr.left); m_h = std::max<LONG>(1, cr.bottom - cr.top);
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = m_w; sd.Height = m_h;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 3;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = m_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    hr = factory->CreateSwapChainForHwnd(m_device.Get(), m_hwnd, &sd, nullptr, nullptr, m_swap.GetAddressOf());
    if (FAILED(hr)) {   // Windows 8.1-era fallback: FLIP_SEQUENTIAL, then legacy blit model
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; sd.Flags = 0; m_tearing = false;
        hr = factory->CreateSwapChainForHwnd(m_device.Get(), m_hwnd, &sd, nullptr, nullptr, m_swap.GetAddressOf());
    }
    if (FAILED(hr)) { error = "Swap chain oluşturulamadı (" + GpuDeviceManager::hresultText(hr) + ")"; return false; }
    factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

    D2D1_FACTORY_OPTIONS fo{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo, (void**)m_d2dFactory.GetAddressOf())) ||
        FAILED(m_d2dFactory->CreateDevice(dxgiDev.Get(), m_d2dDevice.GetAddressOf())) ||
        FAILED(m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, m_d2d.GetAddressOf()))) {
        LOGW("Direct2D overlay unavailable");
        m_d2d.Reset();
    }
    UINT dpi = 96;
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    if (auto fn = (GetDpiForWindowFn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")) dpi = fn(m_hwnd);
    m_dpiScale = dpi / 96.0f;
    Fonts::init();
    return createTargets();
}

bool RenderSurface::createTargets() {
    ComPtr<ID3D11Texture2D> bb;
    if (FAILED(m_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)bb.GetAddressOf()))) return false;
    if (FAILED(m_device->CreateRenderTargetView(bb.Get(), nullptr, m_rtv.GetAddressOf()))) return false;
    if (m_d2d) {
        ComPtr<IDXGISurface> surf;
        bb.As(&surf);
        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                                             D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        if (FAILED(m_d2d->CreateBitmapFromDxgiSurface(surf.Get(), &bp, m_d2dTarget.GetAddressOf()))) m_d2dTarget.Reset();
        if (!m_brush) m_d2d->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), m_brush.GetAddressOf());
    }
    return true;
}

void RenderSurface::releaseTargets() {
    if (m_d2d) m_d2d->SetTarget(nullptr);
    m_d2dTarget.Reset();
    m_rtv.Reset();
    if (m_context) { m_context->OMSetRenderTargets(0, nullptr, nullptr); m_context->Flush(); }
}

bool RenderSurface::handleResize() {
    if (!m_resizePending.exchange(false)) return true;
    RECT cr; GetClientRect(m_hwnd, &cr);
    uint32_t w = std::max<LONG>(1, cr.right - cr.left), h = std::max<LONG>(1, cr.bottom - cr.top);
    if (w == m_w && h == m_h) return true;
    releaseTargets();
    HRESULT hr = m_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, m_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    if (FAILED(hr)) { LOGE("ResizeBuffers failed: %s", GpuDeviceManager::hresultText(hr).c_str()); return false; }
    m_w = w; m_h = h;
    return createTargets();
}

void RenderSurface::destroy() {
    releaseTargets();
    for (auto& f : m_formats) f.Reset();
    m_brush.Reset(); m_roundStroke.Reset(); m_d2d.Reset(); m_d2dDevice.Reset(); m_d2dFactory.Reset();
    if (m_swap) {
        BOOL fs = FALSE;
        m_swap->GetFullscreenState(&fs, nullptr);
        if (fs) m_swap->SetFullscreenState(FALSE, nullptr);
    }
    m_swap.Reset();
    if (m_context) { m_context->ClearState(); m_context->Flush(); }
    m_context.Reset(); m_device.Reset();
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
}

bool RenderSurface::pump() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return !closeRequested;
}

HRESULT RenderSurface::present(bool vsync) {
    // Fault injection for automated device-lost tests: AETHER_SIMULATE_DEVICE_LOST=<present count>.
    static const int injectAfter = [] {
        char buf[32] = {};
        return GetEnvironmentVariableA("AETHER_SIMULATE_DEVICE_LOST", buf, sizeof(buf)) ? atoi(buf) : 0;
    }();
    if (injectAfter > 0 && ++m_presentCount >= injectAfter) {
        LOGW("Simulated device loss (fault injection)");
        return DXGI_ERROR_DEVICE_REMOVED;
    }
    UINT flags = (!vsync && m_tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    return m_swap->Present(vsync ? 1 : 0, flags);
}

HRESULT RenderSurface::deviceStatus() const { return m_device ? m_device->GetDeviceRemovedReason() : E_FAIL; }

bool RenderSurface::saveScreenshot(const std::wstring& path) {
    ComPtr<ID3D11Texture2D> bb;
    if (FAILED(m_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)bb.GetAddressOf()))) return false;
    D3D11_TEXTURE2D_DESC d; bb->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(m_device->CreateTexture2D(&d, nullptr, staging.GetAddressOf()))) return false;
    m_context->CopyResource(staging.Get(), bb.Get());
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms))) return false;
    std::vector<uint8_t> px((size_t)d.Width * d.Height * 4);
    for (UINT y = 0; y < d.Height; ++y) {
        const uint8_t* src = (const uint8_t*)ms.pData + (size_t)y * ms.RowPitch;
        uint8_t* dst = px.data() + (size_t)y * d.Width * 4;
        memcpy(dst, src, d.Width * 4);
        for (UINT x = 0; x < d.Width; ++x) dst[x * 4 + 3] = 255;
    }
    m_context->Unmap(staging.Get(), 0);
    return savePngBGRA(path, d.Width, d.Height, d.Width * 4, px.data());
}

IDWriteTextFormat* RenderSurface::format(float size, bool semibold) {
    for (int i = 0; i < 8; ++i)
        if (m_formats[i] && m_formatSizes[i] == size && m_formatSemi[i] == semibold) return m_formats[i].Get();
    for (int i = 0; i < 8; ++i) {
        if (m_formats[i]) continue;
        if (FAILED(Fonts::factory()->CreateTextFormat(Fonts::family(), Fonts::collection(),
                                                      semibold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_REGULAR,
                                                      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"tr-TR",
                                                      m_formats[i].GetAddressOf())))
            return nullptr;
        m_formatSizes[i] = size; m_formatSemi[i] = semibold;
        return m_formats[i].Get();
    }
    return m_formats[0].Get();
}

bool RenderSurface::beginOverlay() {
    if (!m_d2d || !m_d2dTarget || !Fonts::factory()) return false;
    m_d2d->SetTarget(m_d2dTarget.Get());
    m_d2d->SetDpi(96.0f * m_dpiScale, 96.0f * m_dpiScale);
    m_d2d->BeginDraw();
    m_d2d->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return true;
}

void RenderSurface::endOverlay() {
    if (!m_d2d) return;
    m_d2d->EndDraw();
    m_d2d->SetTarget(nullptr);
}

static D2D1_COLOR_F rgbColor(uint32_t rgb, float a) {
    return D2D1::ColorF(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}

void RenderSurface::text(float x, float y, const std::wstring& s, float size, uint32_t rgb, float alpha, bool semibold, float maxWidth) {
    IDWriteTextFormat* f = format(size, semibold);
    if (!f) return;
    m_brush->SetColor(rgbColor(rgb, alpha));
    m_d2d->DrawTextW(s.c_str(), (UINT32)s.size(), f, D2D1::RectF(x, y, x + maxWidth, y + size * 2.0f), m_brush.Get());
}

static void drawAligned(ID2D1DeviceContext* d2d, ID2D1SolidColorBrush* brush, IDWriteTextFormat* f, float x, float y, float w,
                        const std::wstring& s, float size, DWRITE_TEXT_ALIGNMENT a) {
    f->SetTextAlignment(a);
    d2d->DrawTextW(s.c_str(), (UINT32)s.size(), f, D2D1::RectF(x, y, x + w, y + size * 2.0f), brush);
    f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
}

void RenderSurface::textRight(float xRight, float y, const std::wstring& s, float size, uint32_t rgb, float alpha, bool semibold) {
    IDWriteTextFormat* f = format(size, semibold);
    if (!f) return;
    m_brush->SetColor(rgbColor(rgb, alpha));
    drawAligned(m_d2d.Get(), m_brush.Get(), f, xRight - 600, y, 600, s, size, DWRITE_TEXT_ALIGNMENT_TRAILING);
}

void RenderSurface::textCenter(float cx, float y, const std::wstring& s, float size, uint32_t rgb, float alpha, bool semibold) {
    IDWriteTextFormat* f = format(size, semibold);
    if (!f) return;
    m_brush->SetColor(rgbColor(rgb, alpha));
    drawAligned(m_d2d.Get(), m_brush.Get(), f, cx - 300, y, 600, s, size, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void RenderSurface::arc(float cx, float cy, float r, float a0, float a1, uint32_t rgb, float alpha, float width) {
    if (std::fabs(a1 - a0) < 1e-3f) return;
    ComPtr<ID2D1PathGeometry> g;
    if (FAILED(m_d2dFactory->CreatePathGeometry(g.GetAddressOf()))) return;
    ComPtr<ID2D1GeometrySink> sink;
    g->Open(sink.GetAddressOf());
    int n = std::max(8, (int)(std::fabs(a1 - a0) * r / 2.5f));
    sink->BeginFigure(D2D1::Point2F(cx + std::cos(a0) * r, cy + std::sin(a0) * r), D2D1_FIGURE_BEGIN_HOLLOW);
    for (int i = 1; i <= n; ++i) {
        float a = a0 + (a1 - a0) * i / n;
        sink->AddLine(D2D1::Point2F(cx + std::cos(a) * r, cy + std::sin(a) * r));
    }
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    if (!m_roundStroke) {
        D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        m_d2dFactory->CreateStrokeStyle(sp, nullptr, 0, m_roundStroke.GetAddressOf());
    }
    m_brush->SetColor(rgbColor(rgb, alpha));
    m_d2d->DrawGeometry(g.Get(), m_brush.Get(), width, m_roundStroke.Get());
}

void RenderSurface::circle(float cx, float cy, float r, uint32_t rgb, float alpha) {
    m_brush->SetColor(rgbColor(rgb, alpha));
    m_d2d->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), m_brush.Get());
}

void RenderSurface::hline(float x0, float x1, float y, uint32_t rgb, float alpha) {
    m_brush->SetColor(rgbColor(rgb, alpha));
    m_d2d->DrawLine(D2D1::Point2F(x0, y), D2D1::Point2F(x1, y), m_brush.Get(), 1.0f);
}

void RenderSurface::panel(float x, float y, float w, float h, float radius, uint32_t rgb, float alpha) {
    m_brush->SetColor(rgbColor(rgb, alpha));
    m_d2d->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), radius, radius), m_brush.Get());
}

void RenderSurface::bar(float x, float y, float w, float h, float frac, uint32_t rgb) {
    m_brush->SetColor(rgbColor(0xFFFFFF, 0.10f));
    m_d2d->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), h / 2, h / 2), m_brush.Get());
    frac = frac < 0 ? 0 : (frac > 1 ? 1 : frac);
    if (frac > 0) {
        m_brush->SetColor(rgbColor(rgb, 1.0f));
        m_d2d->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w * frac, y + h), h / 2, h / 2), m_brush.Get());
    }
}

} // namespace aether
