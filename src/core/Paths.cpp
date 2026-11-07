#include "Paths.h"
#include <windows.h>
#include <shlobj.h>

namespace aether {

static std::wstring g_root;

void Paths::init(const std::wstring& overrideRoot) {
    if (!overrideRoot.empty()) {
        g_root = overrideRoot;
    } else {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) {
            g_root = std::wstring(p) + L"\\FurUltimate";
            // One-time migration from previous product names (results, settings, leaderboard identity, logs).
            for (const wchar_t* old : {L"\\FurNovaMark", L"\\AetherGPULab"}) {
                std::wstring legacy = std::wstring(p) + old;
                if (GetFileAttributesW(g_root.c_str()) == INVALID_FILE_ATTRIBUTES &&
                    GetFileAttributesW(legacy.c_str()) != INVALID_FILE_ATTRIBUTES)
                    MoveFileExW(legacy.c_str(), g_root.c_str(), MOVEFILE_COPY_ALLOWED);
            }
            CoTaskMemFree(p);
        } else {
            g_root = exeDir() + L"\\data";
        }
    }
    CreateDirectoryW(g_root.c_str(), nullptr);
    CreateDirectoryW(logs().c_str(), nullptr);
    CreateDirectoryW(screenshots().c_str(), nullptr);
    CreateDirectoryW(fonts().c_str(), nullptr);
}

const std::wstring& Paths::root() { return g_root; }
std::wstring Paths::logs() { return g_root + L"\\logs"; }
std::wstring Paths::database() { return g_root + L"\\results.db"; }
std::wstring Paths::settingsFile() { return g_root + L"\\settings.json"; }
std::wstring Paths::screenshots() { return g_root + L"\\screenshots"; }
std::wstring Paths::fonts() { return g_root + L"\\fonts"; }

std::wstring Paths::exeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s(buf, n);
    size_t pos = s.find_last_of(L"\\/");
    return pos == std::wstring::npos ? s : s.substr(0, pos);
}

} // namespace aether
