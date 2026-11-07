#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace aether {

// In-app updater backed by GitHub Releases.
//  1. check():   GET  https://api.github.com/repos/<repo>/releases/latest
//  2. install(): download the matching asset (installed copy -> "*-Setup.exe", standalone/portable -> "*-win64.exe"),
//                verify its SHA-256 (GitHub asset digest or the release's SHA256SUMS.txt), then
//                  - installed copy:  run the installer silently with /UPDATE (UAC prompt); it relaunches the app,
//                  - standalone exe:  swap the running exe (old one is renamed to *.old) and relaunch.
//     The caller exits the app when the state becomes RestartPending.
struct ReleaseAsset {
    std::string name, url, sha256;   // sha256: lowercase hex, empty if GitHub did not report a digest
    int64_t size = 0;
};

struct ReleaseInfo {
    std::string tag, version, name, notes, htmlUrl;
    std::vector<ReleaseAsset> assets;
    const ReleaseAsset* find(const std::string& suffix) const;   // case-insensitive "ends with"
};

enum class UpdateState { Idle, Checking, UpToDate, Available, Downloading, Verifying, Installing, RestartPending, Error };

struct UpdateStatus {
    UpdateState state = UpdateState::Idle;
    ReleaseInfo release;
    std::string error;
    int64_t received = 0, total = 0;
    bool manualCheck = false;        // last check was requested by the user (show "up to date" feedback)
};

// Helpers (exposed for tests)
bool parseVersion(const std::string& s, int out[3]);                  // "v1.2.3" / "1.2" -> {1,2,3}/{1,2,0}
bool isNewerVersion(const std::string& candidate, const std::string& current);
bool parseRelease(const std::string& json, ReleaseInfo& out);
std::map<std::string, std::string> parseSha256Sums(const std::string& text);   // filename -> lowercase hex
std::string sha256File(const std::wstring& path);                    // lowercase hex, empty on error
std::string releaseNotesForDisplay(const std::string& markdown, const std::string& language, size_t maxChars);
bool isAllowedDownloadUrl(const std::string& url);

class Updater {
public:
    static Updater& instance();
    static constexpr const char* kRepo = "eren-agalar/FurUltimate";

    void check(bool manual);         // async; ignored while busy
    void install();                  // async; requires state Available
    void cancel();
    void backToAvailable();        // after an error: offer the same release again
    UpdateStatus status() const;
    bool active() const;             // checking / downloading / installing (UI should keep redrawing)
    void shutdown();

    static bool isInstalledCopy();   // Uninstall.exe next to the exe -> installed by the NSIS setup
    static void cleanupAfterUpdate();// removes "<exe>.old" left by a self-replace
    static std::wstring exePath();

private:
    Updater() = default;
    void set(UpdateState s, const std::string& err = {});
    bool httpGet(const std::string& url, std::string* body, const std::wstring* file, std::string& err);
    void runInstall(ReleaseInfo rel);

    mutable std::mutex m_mutex;
    UpdateStatus m_status;
    std::thread m_thread;
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_busy{false};
};

} // namespace aether
