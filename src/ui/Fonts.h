#pragma once
#include <dwrite_3.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <cstdint>

namespace aether {

using Microsoft::WRL::ComPtr;

// Bundled Inter typeface (SIL Open Font License 1.1). Fonts are embedded as RCDATA resources,
// extracted once to %LOCALAPPDATA%\FurUltimate\fonts and registered as a private DirectWrite collection.
class Fonts {
public:
    static bool init();                       // call once on the UI thread before any text rendering
    static IDWriteFactory* factory();
    static IDWriteFontCollection* collection(); // nullptr => system collection fallback
    static const wchar_t* family();           // "Inter" or "Segoe UI" fallback
    static const wchar_t* displayFamily();    // "Inter Display" or fallback
    // Raw TTF bytes for PDF embedding (resource ids defined in resource.h).
    static bool fontBytes(int resourceId, std::vector<uint8_t>& out);
};

} // namespace aether
