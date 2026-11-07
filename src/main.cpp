// FurUltimate — application entry point.
#include "ui/App.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "core/Strings.h"
#include "bench/AiBench.h"
#include <shellapi.h>
#include <windows.h>
#include <objbase.h>
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
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\FurUltimate.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND w = FindWindowW(L"FurUltimateMain", nullptr)) { ShowWindow(w, SW_RESTORE); SetForegroundWindow(w); }
        return 0;
    }
    std::wstring dataRoot;
    std::wstring args = cmd ? cmd : L"";
    size_t p = args.find(L"--data-dir=");
    if (p != std::wstring::npos) {
        dataRoot = args.substr(p + 11);
        size_t sp = dataRoot.find(L' ');
        if (sp != std::wstring::npos) dataRoot = dataRoot.substr(0, sp);
    }
    Paths::init(dataRoot);
    Log::init(Paths::logs());
    LOGI("FurUltimate %s starting", AETHER_VERSION);
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
