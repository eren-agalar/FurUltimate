#include "Leaderboard.h"
#include "LeaderboardEmbedded.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../core/FileIO.h"
#include "../core/Strings.h"
#include <windows.h>
#include <winhttp.h>
#include <objbase.h>
#include <nlohmann/json.hpp>
#include <cmath>

namespace aether {

using nlohmann::json;

std::string overallBoard(const std::string& presetId) { return "overall:" + presetId; }
std::string aiBoard(const std::string& aiPath) { return "ai:" + aiPath; }

std::string newPlayerId() {
    GUID g{};
    CoCreateGuid(&g);
    return format("%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", (unsigned long)g.Data1, g.Data2, (g.Data3 & 0x0FFF) | 0x4000,
                  (g.Data4[0] & 0x3F) | 0x80, g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
}

static std::string urlEncode(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else o += format("%%%02X", c);
    }
    return o;
}

LeaderboardClient& LeaderboardClient::instance() { static LeaderboardClient c; return c; }

void LeaderboardClient::loadConfig() {
    for (const std::wstring& path : {Paths::root() + L"\\leaderboard.json", Paths::exeDir() + L"\\leaderboard.json"}) {
        std::string text;
        if (!readFile(path, text)) continue;
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) { LOGW("leaderboard.json is invalid: %s", narrow(path).c_str()); continue; }
        m_cfg.url = j.value("url", "");
        m_cfg.anonKey = j.value("anonKey", "");
        while (!m_cfg.url.empty() && m_cfg.url.back() == '/') m_cfg.url.pop_back();
        if (m_cfg.valid()) { LOGI("Leaderboard server: %s", m_cfg.url.c_str()); return; }
    }
    m_cfg.url = kEmbeddedLeaderboardUrl;
    m_cfg.anonKey = kEmbeddedLeaderboardKey;
    while (!m_cfg.url.empty() && m_cfg.url.back() == '/') m_cfg.url.pop_back();
    if (m_cfg.valid()) { LOGI("Leaderboard server (built-in): %s", m_cfg.url.c_str()); return; }
    m_cfg = {};
    LOGI("Leaderboard server not configured (leaderboard.json missing)");
}

bool LeaderboardClient::http(const std::string& method, const std::string& path, const std::string& body, int& status,
                             std::string& response, std::string& error) {
    status = 0;
    std::wstring url = widen(m_cfg.url + path);
    URL_COMPONENTS uc{sizeof(uc)};
    wchar_t host[256] = {}, upath[2048] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 255;
    uc.lpszUrlPath = upath; uc.dwUrlPathLength = 2047;
    wchar_t extra[2048] = {};
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2047;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { error = "Geçersiz sunucu adresi"; return false; }
    HINTERNET s = WinHttpOpen(L"FurUltimate/1.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"FurUltimate/1.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) { error = "WinHTTP başlatılamadı"; return false; }
    WinHttpSetTimeouts(s, 8000, 8000, 10000, 15000);
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    std::wstring full = std::wstring(upath) + extra;
    HINTERNET r = c ? WinHttpOpenRequest(c, widen(method).c_str(), full.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                    : nullptr;
    bool ok = false;
    if (r) {
        std::wstring headers = L"apikey: " + widen(m_cfg.anonKey) + L"\r\nAuthorization: Bearer " + widen(m_cfg.anonKey) +
                               L"\r\nContent-Type: application/json\r\nAccept: application/json\r\n";
        if (WinHttpSendRequest(r, headers.c_str(), (DWORD)-1L, body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                               (DWORD)body.size(), (DWORD)body.size(), 0) &&
            WinHttpReceiveResponse(r, nullptr)) {
            DWORD code = 0, len = sizeof(code);
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len,
                                WINHTTP_NO_HEADER_INDEX);
            status = (int)code;
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(r, &avail) && avail > 0) {
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(r, chunk.data(), avail, &read)) break;
                response.append(chunk.data(), read);
                if (response.size() > (4u << 20)) break;
            }
            ok = true;
        } else {
            error = format("Sunucuya bağlanılamadı (hata %lu)", GetLastError());
        }
    } else if (error.empty()) {
        error = "Sunucuya bağlanılamadı";
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    if (ok && (status < 200 || status >= 300)) {
        json j = json::parse(response, nullptr, false);
        std::string msg = (!j.is_discarded() && j.is_object()) ? j.value("message", "") : "";
        error = format("Sunucu hatası %d%s", status, msg.empty() ? "" : (": " + msg).c_str());
        return false;
    }
    return ok;
}

void LeaderboardClient::runDetached(std::function<void()> fn) {
    std::lock_guard<std::mutex> l(m_mutex);
    // Join workers that already finished so handles do not accumulate during long sessions.
    for (auto it = m_threads.begin(); it != m_threads.end();) {
        if (it->done->load()) { it->thread.join(); it = m_threads.erase(it); } else ++it;
    }
    auto done = std::make_shared<std::atomic<bool>>(false);
    m_threads.push_back({std::thread([fn = std::move(fn), done] { fn(); done->store(true); }), done});
}

void LeaderboardClient::refresh(const std::string& board, const std::string& playerId) {
    if (!configured()) return;
    int gen = ++m_generation;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        m_snap.loading = true;
        m_snap.error.clear();
        m_snap.board = board;
    }
    runDetached([this, board, playerId, gen] {
        Snapshot s;
        s.board = board;
        int status;
        std::string resp, err;
        std::string b = urlEncode(board);
        bool ok = http("GET", "/rest/v1/leaderboard_public?select=entry_id,nickname,gpu,driver,score,tops,updated_at&board=eq." + b +
                                  "&order=score.desc&limit=100", "", status, resp, err);
        if (ok) {
            json j = json::parse(resp, nullptr, false);
            int rank = 0;
            if (j.is_array()) for (auto& row : j) {
                LeaderboardEntry e;
                e.rank = ++rank;
                e.id = row.value("entry_id", 0LL);
                e.nickname = row.value("nickname", ""); e.gpu = row.value("gpu", ""); e.driver = row.value("driver", "");
                e.score = row.value("score", 0.0);
                if (row.contains("tops") && row["tops"].is_number()) e.tops = row["tops"].get<double>();
                e.date = row.value("updated_at", "").substr(0, 10);
                s.players.push_back(e);
            }
            resp.clear();
            ok = http("GET", "/rest/v1/gpu_ranking?select=gpu,players,avg_score,best_score&board=eq." + b + "&order=avg_score.desc&limit=200",
                      "", status, resp, err);
            if (ok) {
                json g = json::parse(resp, nullptr, false);
                int r = 0;
                if (g.is_array()) for (auto& row : g) {
                    GpuRankingEntry e;
                    e.rank = ++r;
                    e.gpu = row.value("gpu", "");
                    e.players = row.value("players", 0);
                    e.avgScore = row.value("avg_score", 0.0);
                    e.bestScore = row.value("best_score", 0.0);
                    s.gpus.push_back(e);
                }
            }
            if (ok && !playerId.empty()) {
                resp.clear();
                json body = {{"p_player", playerId}};
                if (http("POST", "/rest/v1/rpc/my_entries", body.dump(), status, resp, err)) {
                    json m = json::parse(resp, nullptr, false);
                    if (m.is_array()) for (auto& row : m) {
                        MyBoardEntry e;
                        e.id = row.value("entry_id", 0LL);
                        e.board = row.value("board", ""); e.gpu = row.value("gpu", "");
                        e.score = row.value("score", 0.0); e.rank = row.value("rank", 0); e.total = row.value("total", 0);
                        s.mine.push_back(e);
                    }
                } else {
                    err.clear();   // personal entries are optional
                }
            }
        }
        if (gen != m_generation) return;   // a newer refresh superseded this one
        for (auto& e : s.players)
            for (auto& m : s.mine)
                if (m.id != 0 && m.id == e.id) e.mine = true;   // ties make ranks ambiguous; match by row id
        std::lock_guard<std::mutex> l(m_mutex);
        s.loading = false;
        s.loaded = ok;
        s.error = ok ? "" : err;
        s.fetchedAt = (double)GetTickCount64() / 1000.0;
        m_snap = std::move(s);
    });
}

void LeaderboardClient::submit(const SubmitRequest& req) {
    if (!configured()) return;
    runDetached([this, req] {
        json details = json::parse(req.detailsJson.empty() ? "{}" : req.detailsJson, nullptr, false);
        json body = {{"p_player", req.playerId}, {"p_nick", req.nickname}, {"p_board", req.board}, {"p_gpu", req.gpu},
                     {"p_vendor", req.vendor}, {"p_driver", req.driver}, {"p_score", (int)std::lround(req.score)},
                     {"p_tops", req.tops}, {"p_details", details.is_discarded() ? json::object() : details},
                     {"p_bench_version", req.benchVersion}, {"p_app_version", req.appVersion}};
        int status;
        std::string resp, err;
        SubmitResult r;
        if (http("POST", "/rest/v1/rpc/submit_score", body.dump(), status, resp, err)) {
            json j = json::parse(resp, nullptr, false);
            r.ok = !j.is_discarded() && j.is_object();
            if (r.ok) { r.status = j.value("status", ""); r.rank = j.value("rank", 0); r.error = j.value("error", ""); }
            if (!r.error.empty()) r.ok = false;
        } else {
            r.error = err;
        }
        LOGI("Leaderboard submit %s: %s %s", req.board.c_str(), r.ok ? r.status.c_str() : "failed", r.error.c_str());
        std::lock_guard<std::mutex> l(m_mutex);
        m_submits.push_back(r);
    });
}

LeaderboardClient::Snapshot LeaderboardClient::snapshot() const { std::lock_guard<std::mutex> l(m_mutex); return m_snap; }

std::vector<SubmitResult> LeaderboardClient::takeSubmitResults() {
    std::lock_guard<std::mutex> l(m_mutex);
    std::vector<SubmitResult> v;
    v.swap(m_submits);
    return v;
}

void LeaderboardClient::shutdown() {
    std::vector<Worker> t;
    { std::lock_guard<std::mutex> l(m_mutex); t.swap(m_threads); }
    for (auto& w : t) if (w.thread.joinable()) w.thread.join();   // requests are bounded by WinHTTP timeouts
}

} // namespace aether
