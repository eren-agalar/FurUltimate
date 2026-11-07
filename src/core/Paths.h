#pragma once
#include <string>

namespace aether {

// All application data lives under %LOCALAPPDATA%\FurUltimate (overridable for tests).
struct Paths {
    static void init(const std::wstring& overrideRoot = L"");
    static const std::wstring& root();
    static std::wstring logs();
    static std::wstring database();
    static std::wstring settingsFile();
    static std::wstring screenshots();
    static std::wstring fonts();
    static std::wstring exeDir();
};

} // namespace aether
