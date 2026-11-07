#include "../App.h"
#include "../../core/Strings.h"
#include "../../core/I18n.h"
#include <windows.h>

namespace aether {

static void copyToClipboard(HWND h, const std::string& text) {
    std::wstring w = widen(text);
    if (!OpenClipboard(h)) return;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (mem) {
        memcpy(GlobalLock(mem), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(mem);
        SetClipboardData(CF_UNICODETEXT, mem);
    }
    CloseClipboard();
}

static Rect section(App& app, Rect& col, const std::string& title, int rows, Icon icon) {
    const Theme& t = Theme::current();
    Rect card = col.cutTop(64 + rows * 34.0f);
    col.cutTop(16);
    app.ui.card(card);
    Rect in = card.inset(20, 18);
    drawIcon(app.ui.c, icon, in.x, in.y + 2, 18, t.perf, 1.5f);
    app.ui.label(title, {in.x + 28, in.y, in.w - 28, 22}, t.text, 15, Weight::SemiBold);
    in.cutTop(36);
    return in;
}

void drawHardwarePage(App& app, Rect area) {
    const Theme& t = Theme::current();
    UI& ui = app.ui;
    pageHeader(app, area, T("Donanım"), T("Algılanan donanım bilgileri · algılanamayan değerler N/A olarak gösterilir"));
    if (!app.hwReady) {
        ui.label(T("Donanım algılanıyor…"), area.cutTop(30), t.text2, 15);
        ui.progressBar(area.cutTop(4).cutLeft(260), std::fmod(ui.time() * 0.4f, 1.0f), t.perf);
        return;
    }
    Rect tb = area.cutTop(40);
    if (ui.button("hw-copy", T("Panoya kopyala"), {tb.x, tb.y, 170, 36}, ButtonStyle::Secondary, true, (int)Icon::Copy)) {
        copyToClipboard(app.hwnd(), app.systemSummaryText());
        ui.toast(T("Sistem bilgisi panoya kopyalandı"), ToastKind::Success);
    }
    area.cutTop(12);
    float scroll = ui.beginScroll("hw-scroll", area);
    Rect a{area.x, area.y - scroll, area.w - 8, 10000};
    float top = a.y;
    bool two = a.w > 1050;
    Rect L = two ? Rect{a.x, a.y, a.w / 2 - 8, 10000} : a;
    Rect R = two ? Rect{a.x + a.w / 2 + 8, a.y, a.w / 2 - 8, 10000} : a;
    auto kv = [&](Rect& in, const std::string& k, const std::string& v) { keyValueRow(app, in.cutTop(34), k, v); };
    for (size_t gi = 0; gi < app.hw.gpus.size(); ++gi) {
        const GpuHardware& g = app.hw.gpus[gi];
        Rect& col = (gi % 2 == 0 || !two) ? L : R;
        std::string title = (int)gi == app.hw.primaryGpu ? std::string(T("Birincil GPU")) : std::string(T("Ek GPU"));
        Rect in = section(app, col, title, 15, Icon::Chip);
        const AdapterInfo& ad = g.adapter;
        kv(in, T("Model"), narrow(ad.name));
        kv(in, T("Üretici"), ad.vendorName() + format(" (0x%04X)", ad.vendorId));
        kv(in, T("Aygıt kimliği"), format("0x%04X · Rev 0x%02X", ad.deviceId, ad.revision));
        kv(in, T("Alt sistem kimliği"), ad.subSysId ? format("0x%08X", ad.subSysId) : "");
        kv(in, T("Mimari"), g.architecture.empty() ? "" : g.architecture + (g.architectureInferred ? std::string(" (") + T("model adından") + ")" : ""));
        kv(in, T("Ayrılmış VRAM"), ad.dedicatedVideoMemory ? formatBytes(ad.dedicatedVideoMemory) : "");
        kv(in, T("Paylaşılan bellek"), ad.sharedSystemMemory ? formatBytes(ad.sharedSystemMemory) : "");
        kv(in, T("Bellek türü"), g.memoryType);
        kv(in, T("Sürücü sürümü"), ad.driverVersion);
        kv(in, T("Sürücü tarihi"), g.driverDate);
        kv(in, T("Sürücü sağlayıcısı"), g.driverProvider);
        std::string pcie = g.pcieCurGen > 0 ? format("Gen %d x%d", g.pcieCurGen, g.pcieCurWidth) : "";
        if (!pcie.empty() && g.pcieMaxGen > 0) pcie += format(" (%s Gen %d x%d)", T("maks."), g.pcieMaxGen, g.pcieMaxWidth);
        kv(in, T("PCIe arayüzü"), pcie);
        kv(in, T("Konum"), g.location);
        kv(in, "PnP", g.pnpId);
        kv(in, T("Yazılım bağdaştırıcısı"), ad.software ? T("Evet") : T("Hayır"));
    }
    // APIs
    if (const GpuHardware* g = app.gpu()) {
        Rect in = section(app, R, T("Desteklenen grafik API'leri"), 6, Icon::Shield);
        kv(in, "Direct3D 11", std::string(T("Özellik seviyesi ")) + featureLevelName(g->adapter.maxFeatureLevel));
        kv(in, "Direct3D 12", g->d3d12FeatureLevel.empty() ? "" : std::string(T("Özellik seviyesi ")) + g->d3d12FeatureLevel);
        kv(in, T("Shader Model (D3D12)"), g->shaderModel);
        kv(in, "DirectML", g->directMLAvailable ? T("Sistem bileşeni mevcut") : "");
        kv(in, "Vulkan", g->vulkanLoader ? T("Yükleyici algılandı (cihaz doğrulanmadı)") : "");
        kv(in, "OpenCL", g->openclLoader ? T("Yükleyici algılandı (cihaz doğrulanmadı)") : "");
    }
    {
        Rect in = section(app, R, T("İşlemci"), 4, Icon::Chip);
        kv(in, T("Model"), app.hw.cpu.brand);
        kv(in, T("Üretici"), app.hw.cpu.vendor);
        kv(in, T("Çekirdek / iş parçacığı"), app.hw.cpu.cores ? format("%d / %d", app.hw.cpu.cores, app.hw.cpu.threads) : "");
        kv(in, T("Temel frekans"), app.hw.cpu.baseMHz ? format("%d MHz", app.hw.cpu.baseMHz) : "");
    }
    {
        Rect in = section(app, R, T("Sistem belleği"), 4, Icon::Memory);
        kv(in, T("Toplam"), app.hw.memory.totalBytes ? formatBytes(app.hw.memory.totalBytes) : "");
        kv(in, T("Tür"), app.hw.memory.type);
        kv(in, T("Hız"), app.hw.memory.speedMTs ? format("%d MT/s", app.hw.memory.speedMTs) : "");
        kv(in, T("Modül sayısı"), app.hw.memory.modules ? format("%d", app.hw.memory.modules) : "");
    }
    {
        Rect in = section(app, R, T("İşletim sistemi"), 2, Icon::Info);
        kv(in, T("Sürüm"), app.hw.os.name + (app.hw.os.version.empty() ? "" : " " + app.hw.os.version));
        kv(in, T("Derleme"), app.hw.os.build);
    }
    {
        Rect in = section(app, two ? L : R, T("Ekran bağdaştırıcıları ve ekranlar"), (int)std::max<size_t>(1, app.hw.displays.size()), Icon::Monitoring);
        if (app.hw.displays.empty()) kv(in, T("Ekran"), "");
        for (auto& d : app.hw.displays)
            kv(in, (d.device.empty() ? std::string(T("Ekran")) : d.device) + (d.primary ? std::string(" · ") + T("birincil") : ""),
               format("%d×%d", d.width, d.height) + (d.refreshHz > 1 ? format(" @ %d Hz", d.refreshHz) : std::string()) + " · " + d.adapter);
    }
    float endY = two ? std::max(L.y, R.y) : a.y;
    if (!two) endY = std::max(L.y, R.y);
    ui.endScroll(endY - top + 10);
}

} // namespace aether
