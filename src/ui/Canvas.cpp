#include "Canvas.h"
#include "Fonts.h"
#include "../core/Log.h"
#include "../core/Image.h"
#include "../core/Strings.h"
#include <dwrite_1.h>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace aether {

std::wstring utf8w(const std::string& s) { return widen(s); }

static D2D1_COLOR_F d2c(const Color& c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }
static D2D1_RECT_F d2r(const Rect& r) { return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h); }

bool Canvas::init(HWND hwnd, std::string& error) {
    m_hwnd = hwnd;
    D2D1_FACTORY_OPTIONS fo{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo, (void**)m_factory.GetAddressOf()))) {
        error = "Direct2D 1.1 kullanılamıyor";
        return false;
    }
    D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                                   D2D1_LINE_JOIN_ROUND);
    m_factory->CreateStrokeStyle(sp, nullptr, 0, m_roundStroke.GetAddressOf());
    RECT rc; GetClientRect(hwnd, &rc);
    m_pxW = std::max<LONG>(1, rc.right); m_pxH = std::max<LONG>(1, rc.bottom);
    return createDevice(error);
}

bool Canvas::createDevice(std::string& error) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = E_FAIL;
    for (D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        for (int start = 0; start < 2 && FAILED(hr); ++start) {
            hr = D3D11CreateDevice(nullptr, type, nullptr, flags, levels + start, (UINT)(4 - start), D3D11_SDK_VERSION,
                                   m_device.ReleaseAndGetAddressOf(), nullptr, m_d3dctx.ReleaseAndGetAddressOf());
        }
        if (SUCCEEDED(hr)) { if (type == D3D_DRIVER_TYPE_WARP) LOGW("UI uses WARP software rasterizer"); break; }
    }
    if (FAILED(hr)) { error = "Arayüz için Direct3D cihazı oluşturulamadı"; return false; }
    ComPtr<IDXGIDevice1> dxgi;
    m_device.As(&dxgi);
    if (FAILED(m_factory->CreateDevice(dxgi.Get(), m_d2dDevice.ReleaseAndGetAddressOf())) ||
        FAILED(m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, m_ctx.ReleaseAndGetAddressOf()))) {
        error = "Direct2D cihaz bağlamı oluşturulamadı";
        return false;
    }
    ComPtr<IDXGIAdapter> adapter;
    dxgi->GetAdapter(adapter.GetAddressOf());
    ComPtr<IDXGIFactory2> f2;
    adapter->GetParent(__uuidof(IDXGIFactory2), (void**)f2.GetAddressOf());
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = m_pxW; sd.Height = m_pxH;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    hr = f2->CreateSwapChainForHwnd(m_device.Get(), m_hwnd, &sd, nullptr, nullptr, m_swap.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; sd.BufferCount = 1;
        hr = f2->CreateSwapChainForHwnd(m_device.Get(), m_hwnd, &sd, nullptr, nullptr, m_swap.ReleaseAndGetAddressOf());
    }
    if (FAILED(hr)) { error = "Arayüz swap chain oluşturulamadı"; return false; }
    f2->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
    m_brush.Reset();
    m_stops.clear();
    return createTargets();
}

bool Canvas::createTargets() {
    ComPtr<IDXGISurface> surf;
    if (FAILED(m_swap->GetBuffer(0, __uuidof(IDXGISurface), (void**)surf.GetAddressOf()))) return false;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), m_dpi, m_dpi);
    if (FAILED(m_ctx->CreateBitmapFromDxgiSurface(surf.Get(), &bp, m_target.ReleaseAndGetAddressOf()))) return false;
    m_ctx->SetTarget(m_target.Get());
    m_ctx->SetDpi(m_dpi, m_dpi);
    m_ctx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (!m_brush) m_ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), m_brush.GetAddressOf());
    return true;
}

void Canvas::releaseTargets() {
    if (m_ctx) m_ctx->SetTarget(nullptr);
    m_target.Reset();
}

void Canvas::shutdown() {
    releaseTargets();
    m_formats.clear(); m_stops.clear(); m_brush.Reset();
    m_ctx.Reset(); m_d2dDevice.Reset(); m_swap.Reset(); m_d3dctx.Reset(); m_device.Reset(); m_factory.Reset();
}

void Canvas::setDpi(float dpi) {
    m_dpi = dpi;
    if (m_ctx) m_ctx->SetDpi(dpi, dpi);
}

bool Canvas::resize(uint32_t w, uint32_t h) {
    w = std::max(1u, w); h = std::max(1u, h);
    if (w == m_pxW && h == m_pxH && m_target) return true;
    m_pxW = w; m_pxH = h;
    releaseTargets();
    HRESULT hr = m_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        std::string err;
        LOGW("UI ResizeBuffers failed (0x%08lX) — recreating device", (unsigned long)hr);
        return createDevice(err);
    }
    return createTargets();
}

bool Canvas::begin() {
    if (!m_target && !createTargets()) return false;
    m_ctx->BeginDraw();
    m_ctx->SetTransform(D2D1::Matrix3x2F::Identity());
    m_drawing = true;
    return true;
}

bool Canvas::end() {
    if (!m_drawing) return true;
    m_drawing = false;
    while (!m_layerStack.empty()) { if (m_layerStack.back() == 0) m_ctx->PopAxisAlignedClip(); else m_ctx->PopLayer(); m_layerStack.pop_back(); }
    HRESULT hr = m_ctx->EndDraw();
    HRESULT hp = S_OK;
    if (SUCCEEDED(hr) && !m_capturePath.empty()) {
        bool ok = captureToPng(m_capturePath);
        m_capturedPath = m_capturePath;
        m_captureState = ok ? 1 : 2;
        m_capturePath.clear();
    }
    if (SUCCEEDED(hr)) hp = m_swap->Present(1, 0);
    if (hr == (HRESULT)D2DERR_RECREATE_TARGET || hp == DXGI_ERROR_DEVICE_REMOVED || hp == DXGI_ERROR_DEVICE_RESET) {
        LOGW("UI device lost — recreating rendering resources");
        std::string err;
        releaseTargets();
        m_swap.Reset();
        if (!createDevice(err)) LOGE("UI device recreation failed: %s", err.c_str());
        return false;
    }
    return true;
}

ID2D1SolidColorBrush* Canvas::brush(const Color& c) { m_brush->SetColor(d2c(c)); return m_brush.Get(); }

ID2D1GradientStopCollection* Canvas::stops(const Color& a, const Color& b) {
    char key[160];
    snprintf(key, sizeof(key), "%.3f,%.3f,%.3f,%.3f|%.3f,%.3f,%.3f,%.3f", a.r, a.g, a.b, a.a, b.r, b.g, b.b, b.a);
    auto it = m_stops.find(key);
    if (it != m_stops.end()) return it->second.Get();
    if (m_stops.size() > 512) m_stops.clear();
    // Wine's Direct2D treats gradient stop colours as premultiplied; native Windows expects straight alpha.
    static const bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
    auto pm = [](const Color& c) { return wine ? Color(c.r * c.a, c.g * c.a, c.b * c.a, c.a) : c; };
    D2D1_GRADIENT_STOP gs[2] = {{0.0f, d2c(pm(a))}, {1.0f, d2c(pm(b))}};
    ComPtr<ID2D1GradientStopCollection> col;
    m_ctx->CreateGradientStopCollection(gs, 2, col.GetAddressOf());
    m_stops[key] = col;
    return col.Get();
}

void Canvas::clear(const Color& c) { m_ctx->Clear(d2c(c)); }
void Canvas::fillRect(const Rect& r, const Color& c) { m_ctx->FillRectangle(d2r(r), brush(c)); }
void Canvas::fillRound(const Rect& r, float rad, const Color& c) {
    m_ctx->FillRoundedRectangle(D2D1::RoundedRect(d2r(r), rad, rad), brush(c));
}
void Canvas::strokeRound(const Rect& r, float rad, const Color& c, float w) {
    Rect rr = r.inset(w * 0.5f);
    m_ctx->DrawRoundedRectangle(D2D1::RoundedRect(d2r(rr), rad, rad), brush(c), w);
}
void Canvas::fillRoundGradient(const Rect& r, float rad, const Color& top, const Color& bottom) {
    ComPtr<ID2D1LinearGradientBrush> b;
    m_ctx->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.x, r.y), D2D1::Point2F(r.x, r.bottom())),
                                     stops(top, bottom), b.GetAddressOf());
    if (b) m_ctx->FillRoundedRectangle(D2D1::RoundedRect(d2r(r), rad, rad), b.Get());
}
void Canvas::fillRoundGradientH(const Rect& r, float rad, const Color& l, const Color& rr) {
    ComPtr<ID2D1LinearGradientBrush> b;
    m_ctx->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.x, r.y), D2D1::Point2F(r.right(), r.y)),
                                     stops(l, rr), b.GetAddressOf());
    if (b) m_ctx->FillRoundedRectangle(D2D1::RoundedRect(d2r(r), rad, rad), b.Get());
}
void Canvas::fillRadial(float cx, float cy, float rx, float ry, const Color& inner, const Color& outer) {
    ComPtr<ID2D1RadialGradientBrush> b;
    m_ctx->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), rx, ry),
                                     stops(inner, outer), b.GetAddressOf());
    if (b) m_ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), b.Get());
}
void Canvas::line(float x1, float y1, float x2, float y2, const Color& c, float w) {
    m_ctx->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush(c), w, m_roundStroke.Get());
}
void Canvas::fillCircle(float cx, float cy, float r, const Color& c) { m_ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush(c)); }
void Canvas::strokeCircle(float cx, float cy, float r, const Color& c, float w) {
    m_ctx->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush(c), w);
}

void Canvas::arc(float cx, float cy, float r, float a0, float a1, const Color& c, float w, bool roundCaps) {
    float sweep = a1 - a0;
    if (std::fabs(sweep) < 1e-4f) return;
    // Tessellated arc: identical output on every Direct2D implementation.
    int n = std::clamp((int)(std::fabs(sweep) * std::max(r, 4.0f) / 2.5f), 6, 256);
    std::vector<Pt> pts;
    pts.reserve(n + 1);
    for (int i = 0; i <= n; ++i) {
        float a = a0 + sweep * i / n;
        pts.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
    }
    (void)roundCaps;
    path(pts, false, c, w, false);
}

void Canvas::path(const std::vector<Pt>& pts, bool closed, const Color& c, float w, bool fill) {
    if (pts.size() < 2) return;
    ComPtr<ID2D1PathGeometry> g;
    m_factory->CreatePathGeometry(g.GetAddressOf());
    ComPtr<ID2D1GeometrySink> s;
    g->Open(s.GetAddressOf());
    s->BeginFigure(D2D1::Point2F(pts[0].x, pts[0].y), fill ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t i = 1; i < pts.size(); ++i) s->AddLine(D2D1::Point2F(pts[i].x, pts[i].y));
    s->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    s->Close();
    if (fill) m_ctx->FillGeometry(g.Get(), brush(c));
    else m_ctx->DrawGeometry(g.Get(), brush(c), w, m_roundStroke.Get());
}

void Canvas::polyline(const std::vector<Pt>& pts, const Color& c, float w, bool) { path(pts, false, c, w, false); }

void Canvas::fillArea(const std::vector<Pt>& pts, float baseline, const Color& top, const Color& bottom) {
    if (pts.size() < 2) return;
    ComPtr<ID2D1PathGeometry> g;
    m_factory->CreatePathGeometry(g.GetAddressOf());
    ComPtr<ID2D1GeometrySink> s;
    g->Open(s.GetAddressOf());
    s->BeginFigure(D2D1::Point2F(pts[0].x, baseline), D2D1_FIGURE_BEGIN_FILLED);
    float minY = baseline;
    for (auto& p : pts) { s->AddLine(D2D1::Point2F(p.x, p.y)); minY = std::min(minY, p.y); }
    s->AddLine(D2D1::Point2F(pts.back().x, baseline));
    s->EndFigure(D2D1_FIGURE_END_CLOSED);
    s->Close();
    ComPtr<ID2D1LinearGradientBrush> b;
    m_ctx->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, minY), D2D1::Point2F(0, baseline)),
                                     stops(top, bottom), b.GetAddressOf());
    if (b) m_ctx->FillGeometry(g.Get(), b.Get());
}

IDWriteTextFormat* Canvas::format(const TextStyle& st) {
    uint64_t key = (uint64_t)(st.size * 100) | ((uint64_t)st.weight << 20) | ((uint64_t)st.display << 32) |
                   ((uint64_t)st.align << 33) | ((uint64_t)st.valign << 36) | ((uint64_t)st.wrap << 39);
    auto it = m_formats.find(key);
    if (it != m_formats.end()) return it->second.Get();
    ComPtr<IDWriteTextFormat> f;
    if (FAILED(Fonts::factory()->CreateTextFormat(st.display ? Fonts::displayFamily() : Fonts::family(), Fonts::collection(),
                                                  (DWRITE_FONT_WEIGHT)st.weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                  st.size, L"tr-TR", f.GetAddressOf())))
        return nullptr;
    f->SetTextAlignment(st.align == Align::Leading ? DWRITE_TEXT_ALIGNMENT_LEADING
                        : st.align == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_TRAILING);
    f->SetParagraphAlignment(st.valign == Align::Leading ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
                             : st.valign == Align::Center ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_FAR);
    f->SetWordWrapping(st.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!st.wrap) {
        ComPtr<IDWriteInlineObject> sign;
        Fonts::factory()->CreateEllipsisTrimmingSign(f.Get(), sign.GetAddressOf());
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        f->SetTrimming(&trim, sign.Get());
    }
    m_formats[key] = f;
    return f.Get();
}

void Canvas::text(const std::string& utf8, const Rect& r, const Color& c, const TextStyle& st) {
    if (utf8.empty() || r.w <= 0) return;
    IDWriteTextFormat* f = format(st);
    if (!f) return;
    std::wstring w = widen(utf8);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(Fonts::factory()->CreateTextLayout(w.c_str(), (UINT32)w.size(), f, r.w, std::max(1.0f, r.h), layout.GetAddressOf()))) return;
    if (st.tracking != 0) {
        ComPtr<IDWriteTextLayout1> l1;
        if (SUCCEEDED(layout.As(&l1))) l1->SetCharacterSpacing(0, st.tracking, 0, DWRITE_TEXT_RANGE{0, (UINT32)w.size()});
    }
    m_ctx->DrawTextLayout(D2D1::Point2F(r.x, r.y), layout.Get(), brush(c), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float Canvas::measure(const std::string& utf8, const TextStyle& st, float maxWidth, float* outHeight) {
    IDWriteTextFormat* f = format(st);
    if (!f || utf8.empty()) { if (outHeight) *outHeight = st.size * 1.2f; return 0; }
    std::wstring w = widen(utf8);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(Fonts::factory()->CreateTextLayout(w.c_str(), (UINT32)w.size(), f, maxWidth, 10000.0f, layout.GetAddressOf()))) return 0;
    if (st.tracking != 0) {
        ComPtr<IDWriteTextLayout1> l1;
        if (SUCCEEDED(layout.As(&l1))) l1->SetCharacterSpacing(0, st.tracking, 0, DWRITE_TEXT_RANGE{0, (UINT32)w.size()});
    }
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    if (outHeight) *outHeight = m.height;
    return m.widthIncludingTrailingWhitespace;
}

void Canvas::pushClip(const Rect& r) { m_ctx->PushAxisAlignedClip(d2r(r), D2D1_ANTIALIAS_MODE_ALIASED); m_layerStack.push_back(0); }
void Canvas::popClip() { if (!m_layerStack.empty() && m_layerStack.back() == 0) { m_ctx->PopAxisAlignedClip(); m_layerStack.pop_back(); } }
void Canvas::pushOpacity(float a) {
    D2D1_LAYER_PARAMETERS1 p = D2D1::LayerParameters1();
    p.opacity = a;
    m_ctx->PushLayer(p, nullptr);
    m_layerStack.push_back(1);
}
void Canvas::popOpacity() { if (!m_layerStack.empty() && m_layerStack.back() == 1) { m_ctx->PopLayer(); m_layerStack.pop_back(); } }
void Canvas::setTranslation(float dx, float dy) { m_ctx->SetTransform(D2D1::Matrix3x2F::Translation(dx, dy)); }

bool Canvas::takeCaptureResult(std::wstring& path, bool& ok) {
    if (m_captureState == 0) return false;
    path = m_capturedPath; ok = m_captureState == 1; m_captureState = 0;
    return true;
}

bool Canvas::captureToPng(const std::wstring& path) {
    ComPtr<ID3D11Texture2D> bb;
    if (FAILED(m_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)bb.GetAddressOf()))) return false;
    D3D11_TEXTURE2D_DESC d; bb->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> st;
    if (FAILED(m_device->CreateTexture2D(&d, nullptr, st.GetAddressOf()))) return false;
    m_d3dctx->CopyResource(st.Get(), bb.Get());
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(m_d3dctx->Map(st.Get(), 0, D3D11_MAP_READ, 0, &ms))) return false;
    std::vector<uint8_t> px((size_t)d.Width * d.Height * 4);
    for (UINT y = 0; y < d.Height; ++y) {
        memcpy(px.data() + (size_t)y * d.Width * 4, (uint8_t*)ms.pData + (size_t)y * ms.RowPitch, d.Width * 4);
        for (UINT x = 0; x < d.Width; ++x) px[((size_t)y * d.Width + x) * 4 + 3] = 255;
    }
    m_d3dctx->Unmap(st.Get(), 0);
    return savePngBGRA(path, d.Width, d.Height, d.Width * 4, px.data());
}

} // namespace aether
