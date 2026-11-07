#pragma once
#include <atomic>
#include <memory>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace aether {

// Online leaderboard (Supabase / PostgREST backend, see server/leaderboard/README.md).
// All network traffic runs on background threads; the UI polls snapshots.
struct LeaderboardEntry {
    long long id = 0;    // public row id (not the player id)
    int rank = 0;
    std::string nickname, gpu, driver, date;
    double score = 0, tops = 0;
    bool mine = false;
};
struct GpuRankingEntry {
    int rank = 0;
    std::string gpu;
    int players = 0;
    double avgScore = 0, bestScore = 0;
};
struct MyBoardEntry { long long id = 0; std::string board, gpu; double score = 0; int rank = 0, total = 0; };

struct LeaderboardServerConfig {
    std::string url;       // https://<project>.supabase.co
    std::string anonKey;   // public anon key (read + RPC only)
    bool valid() const {
        return !anonKey.empty() && (url.rfind("https://", 0) == 0 || url.rfind("http://localhost", 0) == 0 || url.rfind("http://127.0.0.1", 0) == 0);
    }
};

struct SubmitRequest {
    std::string playerId, nickname, board, gpu, vendor, driver, benchVersion, appVersion;
    double score = 0, tops = 0;
    std::string detailsJson;   // sub-scores for server-side consistency checks
};
struct SubmitResult { bool ok = false; std::string status; int rank = 0; std::string error; };

class LeaderboardClient {
public:
    static LeaderboardClient& instance();
    // Loads leaderboard.json from the data folder or next to the executable.
    void loadConfig();
    const LeaderboardServerConfig& config() const { return m_cfg; }
    bool configured() const { return m_cfg.valid(); }

    // Asynchronous queries; results are read with snapshot().
    void refresh(const std::string& board, const std::string& playerId);
    void submit(const SubmitRequest& req);

    struct Snapshot {
        bool loading = false, loaded = false;
        std::string error, board;
        std::vector<LeaderboardEntry> players;
        std::vector<GpuRankingEntry> gpus;
        std::vector<MyBoardEntry> mine;
        double fetchedAt = 0;
    };
    Snapshot snapshot() const;
    // Completed submissions since the last call (UI shows a toast for each).
    std::vector<SubmitResult> takeSubmitResults();
    void shutdown();

private:
    LeaderboardClient() = default;
    bool http(const std::string& method, const std::string& path, const std::string& body, int& status, std::string& response,
              std::string& error);
    void runDetached(std::function<void()> fn);
    LeaderboardServerConfig m_cfg;
    mutable std::mutex m_mutex;
    Snapshot m_snap;
    std::vector<SubmitResult> m_submits;
    std::atomic<int> m_generation{0};
    struct Worker { std::thread thread; std::shared_ptr<std::atomic<bool>> done; };
    std::vector<Worker> m_threads;
};

// Board identifiers.
std::string overallBoard(const std::string& presetId);   // "overall:STANDARD-1080P"
std::string aiBoard(const std::string& aiPath);          // "ai:DirectML FP16"
std::string newPlayerId();                                // random UUID v4

} // namespace aether
