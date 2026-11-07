#pragma once
#include "Canvas.h"
#include "UI.h"
#include "Charts.h"
#include "../core/Settings.h"
#include "../hw/HardwareInfo.h"
#include "../telemetry/Telemetry.h"
#include "../stress/StressEngine.h"
#include "../bench/BenchmarkEngine.h"
#include "../data/Database.h"
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace aether {

enum class Page { Stress = 0, Benchmark, Leaderboard, Results, Hardware, Settings, Count };
enum class Modal { None, ConfirmStress, ConfirmBenchmark, ConfirmClearHistory, ConfirmDelete, About, LeaderboardJoin };

class App {
public:
    int run(HINSTANCE inst, int show);

    // ---- shared state (accessed by pages on the UI thread)
    Canvas canvas;
    UI ui{canvas};
    Settings settings;
    bool settingsDirty = false;
    float settingsSaveTimer = 0;

    std::atomic<bool> hwReady{false};
    SystemHardware hw;
    const GpuHardware* gpu() const { return hw.gpus.empty() ? nullptr : &hw.gpus[hw.primaryGpu]; }

    TelemetrySample tel;
    std::vector<TelemetrySample> history;     // last 30 minutes (refreshed when a new sample arrives)
    double lastSampleT = -1;

    StressEngine stress;
    BenchmarkEngine bench;
    StressConfig stressCfg;
    int stressDurationOverride = -1;          // automation only
    int stressResIndex = 1, stressScaleIndex = 2, stressDurIndex = 2, stressModeIndex = 0;
    BenchConfig benchCfg;
    BenchmarkResult lastBenchResult;         // result shown on the benchmark page after a run
    bool haveLastBench = false;

    ResultDatabase db;
    std::vector<BenchmarkResult> results;
    bool resultsDirty = true;
    int64_t selectedResult = 0, compareResult = 0;
    int resultFilter = 0;
    int64_t pendingDelete = 0;

    Page page = Page::Stress, prevPage = Page::Stress;
    float pageT = 1.0f;                       // page transition progress
    Modal modal = Modal::None;
    float modalT = 0;

    void navigate(Page p);
    void openModal(Modal m) { modal = m; }
    void startStress();
    void startBenchmark();
    uint32_t pendingBenchMask = 0x3F;         // tests of the run waiting for confirmation
    OverlayPalette overlayPalette() const;
    // Leaderboard
    int lbBoard = 0, lbView = 0;              // board: 0 1080p, 1 1440p, 2 2160p, 3 AI ; view: 0 players, 1 GPUs
    double lbLastRefresh = -1000;
    std::string lbNickDraft;
    BenchmarkResult lbPending;                // result waiting for the consent dialog
    bool lbHavePending = false;
    std::string lbBoardId() const;
    void refreshLeaderboard(bool force);
    void maybeSubmitLeaderboard(const BenchmarkResult& r);
    void submitLeaderboard(const BenchmarkResult& r);
    void refreshResults();
    void markSettingsChanged() { settingsDirty = true; settingsSaveTimer = 0.6f; }
    void applySettings();
    void captureWindow();
    std::wstring saveDialog(const wchar_t* defName, const wchar_t* filterName, const wchar_t* ext);
    void exportResult(const BenchmarkResult& r, int format /*0 pdf,1 json,2 csv*/);
    void openFolder(const std::wstring& path);
    std::string systemSummaryText() const;
    HWND hwnd() const { return m_hwnd; }

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);
    void frame();
    void drawSidebar(const Rect& r);
    void drawModal();
    void drawBackground();
    void pollEngines();
    void detectHardwareAsync();

    HWND m_hwnd = nullptr;
    InputState m_input;
    bool m_running = true;
    bool m_needFrame = true;
    LARGE_INTEGER m_freq{}, m_last{};
    std::thread m_hwThread;
    std::mutex m_pendingMutex;
    std::deque<BenchmarkResult> m_pending;
    float m_navIndicator = 0;
    bool m_wasBusy = false;
    float m_idleTimer = 0;
    // Command-line automation (QA / hardware test procedure): --page=N --autorun=JOB --duration=S
    // --capture-after=S --exit-after-run. See docs/HARDWARE_TEST_PROCEDURE.md.
    void parseCommandLine();
    void automationTick(float dt);
    std::string m_autorun;
    int m_autoDuration = 30;
    uint32_t m_autoMask = 0x3F;
    float m_captureAfter = -1, m_autoClock = 0;
    bool m_exitAfterRun = false, m_autoStarted = false, m_autoSawBusy = false;
};

// Pages
void drawStressPage(App& app, Rect area);
void drawBenchmarkPage(App& app, Rect area);
void drawLeaderboardPage(App& app, Rect area);
void drawResultsPage(App& app, Rect area);
void drawHardwarePage(App& app, Rect area);
void drawSettingsPage(App& app, Rect area);

// Shared page helpers
float pageHeader(App& app, Rect& area, const std::string& title, const std::string& subtitle);
void statTile(App& app, const Rect& r, Icon icon, const std::string& label, const std::string& value, const std::string& unit,
              const std::string& sub, const Color& accent, bool available = true);
void keyValueRow(App& app, const Rect& r, const std::string& key, const std::string& value, bool detected = true);
std::string naStr(float v, const char* fmt);
std::string naStr(double v, const char* fmt);
const char* pageTitle(Page p);
Icon pageIcon(Page p);
int columnsFor(float width, int maxCols);

} // namespace aether
