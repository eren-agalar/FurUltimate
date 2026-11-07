#include "Fonts.h"
#include "../resource.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../core/FileIO.h"
#include <windows.h>
#include <mutex>

namespace aether {

namespace {
ComPtr<IDWriteFactory> g_factory;
ComPtr<IDWriteFontCollection1> g_collection;
bool g_hasInter = false, g_hasDisplay = false;
std::once_flag g_once;

struct FontRes { int id; const wchar_t* file; };
const FontRes kFonts[] = {
    {IDR_FONT_REGULAR, L"Inter-Regular.ttf"},       {IDR_FONT_MEDIUM, L"Inter-Medium.ttf"},
    {IDR_FONT_SEMIBOLD, L"Inter-SemiBold.ttf"},     {IDR_FONT_DISPLAY_LIGHT, L"InterDisplay-Light.ttf"},
    {IDR_FONT_DISPLAY_SEMI, L"InterDisplay-SemiBold.ttf"},
};
} // namespace

bool Fonts::fontBytes(int id, std::vector<uint8_t>& out) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!r) return false;
    HGLOBAL h = LoadResource(nullptr, r);
    DWORD size = SizeofResource(nullptr, r);
    const uint8_t* p = (const uint8_t*)LockResource(h);
    if (!p || !size) return false;
    out.assign(p, p + size);
    return true;
}

bool Fonts::init() {
    std::call_once(g_once, [] {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)g_factory.GetAddressOf()))) {
            LOGE("DWriteCreateFactory failed");
            return;
        }
        ComPtr<IDWriteFactory3> f3;
        if (FAILED(g_factory.As(&f3))) { LOGW("IDWriteFactory3 unavailable; using system fonts"); return; }
        ComPtr<IDWriteFontSetBuilder> builder;
        if (FAILED(f3->CreateFontSetBuilder(builder.GetAddressOf()))) return;
        int added = 0;
        for (auto& f : kFonts) {
            std::wstring path = Paths::fonts() + L"\\" + f.file;
            std::vector<uint8_t> bytes;
            if (!fileExists(path)) {
                if (!fontBytes(f.id, bytes) || !writeFile(path, bytes.data(), bytes.size())) continue;
            }
            ComPtr<IDWriteFontFaceReference> ref;
            if (FAILED(f3->CreateFontFaceReference(path.c_str(), nullptr, 0, DWRITE_FONT_SIMULATIONS_NONE, ref.GetAddressOf()))) continue;
            if (SUCCEEDED(builder->AddFontFaceReference(ref.Get()))) ++added;
        }
        if (added == 0) { LOGW("Bundled fonts unavailable; using system fonts"); return; }
        ComPtr<IDWriteFontSet> set;
        if (FAILED(builder->CreateFontSet(set.GetAddressOf()))) return;
        if (FAILED(f3->CreateFontCollectionFromFontSet(set.Get(), g_collection.GetAddressOf()))) return;
        UINT32 idx; BOOL exists = FALSE;
        g_collection->FindFamilyName(L"Inter", &idx, &exists);
        g_hasInter = exists;
        exists = FALSE;
        g_collection->FindFamilyName(L"Inter Display", &idx, &exists);
        g_hasDisplay = exists;
        LOGI("Bundled fonts registered (%d faces, Inter=%d, Inter Display=%d)", added, (int)g_hasInter, (int)g_hasDisplay);
    });
    return g_factory != nullptr;
}

IDWriteFactory* Fonts::factory() { return g_factory.Get(); }
IDWriteFontCollection* Fonts::collection() { return g_hasInter ? g_collection.Get() : nullptr; }
const wchar_t* Fonts::family() { return g_hasInter ? L"Inter" : L"Segoe UI"; }
const wchar_t* Fonts::displayFamily() { return g_hasDisplay ? L"Inter Display" : (g_hasInter ? L"Inter" : L"Segoe UI"); }

} // namespace aether
