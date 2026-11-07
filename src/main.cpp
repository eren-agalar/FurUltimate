// FurUltimate — application entry point.
#include "net/Updater.h"
#include "ui/App.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "core/Strings.h"
#include "bench/AiBench.h"
#include <shellapi.h>
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <string>

using namespace aether;

static void pruneLogs(int keepDays) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((Paths::logs() + L"\\aether_*.log").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    FILETIME now; GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER n{{now.dwLowDateTime, now.dwHighDateTime}};
    do {
        ULARGE_INTEGER w{{fd.ftLastWriteTime.dwLowDateTime, fd.ftLastWriteTime.dwHighDateTime}};
        double days = double(n.QuadPart - w.QuadPart) / 864000000000.0;
        if (days > keepDays) DeleteFileW((Paths::logs() + L"\\" + fd.cFileName).c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmd, int show) {
    // Isolated DirectML measurement worker (spawned by the AI benchmark test). Must run before the
    // single-instance check, because the main window owns the instance mutex while it waits for us.
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv && argc >= 7 && isAiDmlWorkerCommand(argc - 1, argv + 1)) {
            Paths::init(argv[6]);
            Log::init(Paths::logs());
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            int rc = aiDmlWorkerMain(argc - 1, argv + 1);
            Log::shutdown();
            LocalFree(argv);
            return rc;
        }
        if (argv) LocalFree(argv);
    }
    // Single instance: a second launch focuses the running window.
    // If the mutex exists but no window does, the previous instance is still shutting down (typically right after
    // an in-app update relaunch): wait for it instead of exiting silently.
    // "--after-update": relaunched by the in-app updater; the old instance may still be closing its window.
    const bool afterUpdate = wcsstr(GetCommandLineW(), L"--after-update") != nullptr;
    HANDLE mutex = nullptr;
    for (int attempt = 0;; ++attempt) {
        mutex = CreateMutexW(nullptr, TRUE, L"Local\\FurUltimate.SingleInstance");
        if (GetLastError() != ERROR_ALREADY_EXISTS) break;
        HWND w = FindWindowW(L"FurUltimateMain", nullptr);
        if ((w && !afterUpdate) || attempt >= (afterUpdate ? 160 : 80)) {
            if (w) { ShowWindow(w, SW_RESTORE); SetForegroundWindow(w); }
            if (mutex) CloseHandle(mutex);
            return 0;
        }
        if (mutex) CloseHandle(mutex);
        Sleep(250);
    }
    std::wstring dataRoot;
    std::wstring args = cmd ? cmd : L"";
    size_t p = args.find(L"--data-dir=");
    if (p != std::wstring::npos) {
        dataRoot = args.substr(p + 11);
        size_t sp = dataRoot.find(L' ');
        if (sp != std::wstring::npos) dataRoot = dataRoot.substr(0, sp);
        dataRoot.erase(std::remove(dataRoot.begin(), dataRoot.end(), L'"'), dataRoot.end());
    }
    Paths::init(dataRoot);
    Log::init(Paths::logs());
    LOGI("FurUltimate %s starting", AETHER_VERSION);
    Updater::cleanupAfterUpdate();
    SettingsStore::instance().load(Paths::settingsFile());
    pruneLogs(SettingsStore::instance().get().keepLogDays);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int rc = 0;
    {
        App app;
        rc = app.run(inst, show);
    }
    CoUninitialize();
    LOGI("Shutdown complete (code %d)", rc);
    Log::shutdown();
    if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
    return rc;
}
