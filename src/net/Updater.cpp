#include "Updater.h"
#include "../core/FileIO.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../core/Strings.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

using nlohmann::json;

namespace aether {

static std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
static bool endsWithI(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && lower(s.substr(s.size() - suf.size())) == lower(suf);
}
static bool startsWithI(const std::string& s, const std::string& pre) {
    return s.size() >= pre.size() && lower(s.substr(0, pre.size())) == lower(pre);
}

// ---------------------------------------------------------------- helpers

bool parseVersion(const std::string& s, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    size_t i = 0;
    while (i < s.size() && !std::isdigit((unsigned char)s[i])) ++i;   // skip "v" / "FurUltimate "
    if (i == s.size()) return false;
    for (int k = 0; k < 3 && i < s.size(); ++k) {
        if (!std::isdigit((unsigned char)s[i])) break;
        int v = 0;
        while (i < s.size() && std::isdigit((unsigned char)s[i])) { v = v * 10 + (s[i] - '0'); if (v > 100000) return false; ++i; }
        out[k] = v;
        if (i < s.size() && s[i] == '.') ++i; else break;
    }
    return true;
}

bool isNewerVersion(const std::string& candidate, const std::string& current) {
    int a[3], b[3];
    if (!parseVersion(candidate, a) || !parseVersion(current, b)) return false;
    for (int k = 0; k < 3; ++k) if (a[k] != b[k]) return a[k] > b[k];
    return false;
}

const ReleaseAsset* ReleaseInfo::find(const std::string& suffix) const {
    for (auto& a : assets) if (endsWithI(a.name, suffix)) return &a;
    return nullptr;
}

bool parseRelease(const std::string& text, ReleaseInfo& out) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("tag_name")) return false;
    if (j.value("draft", false) || j.value("prerelease", false)) return false;
    out = {};
    out.tag = j.value("tag_name", "");
    int v[3];
    if (!parseVersion(out.tag, v)) return false;
    out.version = format("%d.%d.%d", v[0], v[1], v[2]);
    out.name = j.contains("name") && j["name"].is_string() ? j["name"].get<std::string>() : out.tag;
    out.notes = j.contains("body") && j["body"].is_string() ? j["body"].get<std::string>() : "";
    out.htmlUrl = j.value("html_url", "");
    if (j.contains("assets") && j["assets"].is_array()) {
        for (auto& a : j["assets"]) {
            if (!a.is_object()) continue;
            ReleaseAsset ra;
            ra.name = a.value("name", "");
            ra.url = a.value("browser_download_url", "");
            ra.size = a.value("size", (int64_t)0);
            if (a.contains("digest") && a["digest"].is_string()) {
                std::string d = a["digest"].get<std::string>();
                if (startsWithI(d, "sha256:") && d.size() == 7 + 64) ra.sha256 = lower(d.substr(7));
            }
            if (!ra.name.empty() && !ra.url.empty()) out.assets.push_back(ra);
        }
    }
    return true;
}

std::map<std::string, std::string> parseSha256Sums(const std::string& text) {
    std::map<std::string, std::string> m;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 66) continue;
        std::string hash = lower(line.substr(0, 64));
        if (!std::all_of(hash.begin(), hash.end(), [](char c) { return std::isxdigit((unsigned char)c); })) continue;
        size_t p = 64;
        while (p < line.size() && (line[p] == ' ' || line[p] == '*' || line[p] == '\t')) ++p;
        if (p < line.size()) m[line.substr(p)] = hash;
    }
    return m;
}

std::string sha256File(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 && BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<unsigned char> buf(1 << 16);
        DWORD n = 0;
        bool ok = true;
        while (ReadFile(f, buf.data(), (DWORD)buf.size(), &n, nullptr) && n > 0)
            if (BCryptHashData(h, buf.data(), n, 0) != 0) { ok = false; break; }
        unsigned char dig[32];
        if (ok && BCryptFinishHash(h, dig, 32, 0) == 0)
            for (unsigned char b : dig) hex += format("%02x", b);
    }
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return hex;
}

std::string releaseNotesForDisplay(const std::string& md, const std::string& language, size_t maxChars) {
    // The release body is bilingual: English first, then a "## Türkçe" section.
    std::string src = md;
    size_t tr = md.find("## Türkçe");
    if (tr != std::string::npos) src = language == "tr" ? md.substr(tr + std::string("## Türkçe").size()) : md.substr(0, tr);
    std::istringstream in(src);
    std::string line, out;
    bool inCode = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = trim(line);
        if (t.rfind("```", 0) == 0) { inCode = !inCode; continue; }
        if (inCode || t.empty() || t[0] == '|' || t == "---" || t.rfind("SHA256", 0) == 0) continue;
        while (!t.empty() && t[0] == '#') t.erase(0, 1);
        t = trim(t);
        std::string clean;
        for (size_t i = 0; i < t.size(); ++i) {
            if (t[i] == '*' || t[i] == '`') continue;
            clean += t[i];
        }
        if (clean.rfind("- ", 0) == 0) clean = "•  " + clean.substr(2);
        if (clean.empty()) continue;
        out += clean + "\n";
        if (out.size() > maxChars) break;
    }
    if (out.size() > maxChars) {
        size_t cut = maxChars;
        while (cut > 0 && ((unsigned char)out[cut] & 0xC0) == 0x80) --cut;   // keep UTF-8 intact
        out = out.substr(0, cut) + "…";
    }
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

static std::string envStr(const char* name) {
    char buf[1024] = {};
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    return n > 0 && n < sizeof(buf) ? std::string(buf, n) : std::string();
}

// FUL_UPDATE_URL overrides the release endpoint (automated tests against a local server).
static std::string latestReleaseUrl() {
    std::string o = envStr("FUL_UPDATE_URL");
    return o.empty() ? std::string("https://api.github.com/repos/") + Updater::kRepo + "/releases/latest" : o;
}

bool isAllowedDownloadUrl(const std::string& url) {
    std::string o = envStr("FUL_UPDATE_URL");
    if (!o.empty()) {
        size_t p = o.find("://");
        size_t e = p == std::string::npos ? std::string::npos : o.find('/', p + 3);
        if (e != std::string::npos && startsWithI(url, o.substr(0, e + 1))) return true;
    }
    return startsWithI(url, std::string("https://github.com/") + Updater::kRepo + "/releases/download/");
}

// ---------------------------------------------------------------- Updater

Updater& Updater::instance() {
    static Updater u;
    return u;
}

std::wstring Updater::exePath() {
    wchar_t buf[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    return buf;
}

bool Updater::isInstalledCopy() {
    return GetFileAttributesW((Paths::exeDir() + L"\\Uninstall.exe").c_str()) != INVALID_FILE_ATTRIBUTES;
}

void Updater::cleanupAfterUpdate() {
    std::wstring old = exePath() + L".old";
    if (GetFileAttributesW(old.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    for (int i = 0; i < 20; ++i) {   // the previous process may still be exiting
        if (DeleteFileW(old.c_str())) { LOGI("Removed previous version %s", narrow(old).c_str()); return; }
        Sleep(250);
    }
    LOGW("Could not remove %s", narrow(old).c_str());
}

void Updater::set(UpdateState s, const std::string& err) {
    std::lock_guard<std::mutex> l(m_mutex);
    m_status.state = s;
    m_status.error = err;
}

UpdateStatus Updater::status() const {
    std::lock_guard<std::mutex> l(m_mutex);
    return m_status;
}

bool Updater::active() const {
    auto s = status().state;
    return s == UpdateState::Checking || s == UpdateState::Downloading || s == UpdateState::Verifying || s == UpdateState::Installing;
}

void Updater::cancel() { m_cancel = true; }

void Updater::backToAvailable() {
    std::lock_guard<std::mutex> l(m_mutex);
    if (!m_busy && !m_status.release.version.empty() && isNewerVersion(m_status.release.version, AETHER_VERSION)) {
        m_status.state = UpdateState::Available;
        m_status.error.clear();
    }
}

void Updater::shutdown() {
    m_cancel = true;
    if (m_thread.joinable()) m_thread.join();
}

bool Updater::httpGet(const std::string& url, std::string* body, const std::wstring* file, std::string& err) {
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{sizeof(uc)};
    wchar_t host[256] = {}, path[4096] = {}, extra[2048] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 4095;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { err = "Geçersiz adres"; return false; }
    std::wstring ua = widen(std::string("FurUltimate/") + AETHER_VERSION);
    HINTERNET s = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) { err = "WinHTTP başlatılamadı"; return false; }
    WinHttpSetTimeouts(s, 10000, 10000, 15000, 30000);
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(s, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    std::wstring full = std::wstring(path) + extra;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", full.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                    : nullptr;
    bool ok = false;
    HANDLE out = INVALID_HANDLE_VALUE;
    do {
        if (!r) { err = format("Sunucuya bağlanılamadı (hata %lu)", GetLastError()); break; }
        const bool api = url == latestReleaseUrl();
        const wchar_t* hdr = api ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n"
                                  : L"Accept: application/octet-stream\r\n";
        if (!WinHttpSendRequest(r, hdr, (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(r, nullptr)) {
            err = format("Sunucuya bağlanılamadı (hata %lu)", GetLastError());
            break;
        }
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        if (status == 404 && api) { err = "Yayımlanmış sürüm bulunamadı"; break; }
        if (status == 403 || status == 429) { err = "GitHub istek sınırına ulaşıldı; daha sonra tekrar deneyin"; break; }
        if (status != 200) { err = format("Sunucu yanıtı HTTP %lu", status); break; }
        if (file) {
            wchar_t lenBuf[32] = {}; DWORD lsz = sizeof(lenBuf);
            if (WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenBuf, &lsz, WINHTTP_NO_HEADER_INDEX)) {
                std::lock_guard<std::mutex> l(m_mutex);
                m_status.total = _wtoi64(lenBuf);
            }
            out = CreateFileW(file->c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (out == INVALID_HANDLE_VALUE) { err = format("Dosya yazılamadı (hata %lu)", GetLastError()); break; }
        }
        std::vector<char> buf(1 << 16);
        bool readOk = true;
        for (;;) {
            if (m_cancel) { err = "İptal edildi"; readOk = false; break; }
            DWORD n = 0;
            if (!WinHttpReadData(r, buf.data(), (DWORD)buf.size(), &n)) { err = format("İndirme kesildi (hata %lu)", GetLastError()); readOk = false; break; }
            if (n == 0) break;
            if (body) {
                body->append(buf.data(), n);
                if (body->size() > (8u << 20)) { err = "Yanıt çok büyük"; readOk = false; break; }
            } else {
                DWORD w = 0;
                if (!WriteFile(out, buf.data(), n, &w, nullptr) || w != n) { err = "Dosya yazılamadı"; readOk = false; break; }
                std::lock_guard<std::mutex> l(m_mutex);
                m_status.received += n;
            }
        }
        ok = readOk;
    } while (false);
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!ok && file) DeleteFileW(file->c_str());
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

void Updater::check(bool manual) {
    if (m_busy.exchange(true)) return;
    if (m_thread.joinable()) m_thread.join();
    m_cancel = false;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        m_status = {};
        m_status.state = UpdateState::Checking;
        m_status.manualCheck = manual;
    }
    m_thread = std::thread([this] {
        std::string body, err;
        ReleaseInfo rel;
        if (!httpGet(latestReleaseUrl(), &body, nullptr, err)) {
            LOGW("Update check failed: %s", err.c_str());
            set(UpdateState::Error, err);
        } else if (!parseRelease(body, rel)) {
            LOGW("Update check: unreadable release response");
            set(UpdateState::Error, "Sürüm bilgisi okunamadı");
        } else {
            bool newer = isNewerVersion(rel.version, AETHER_VERSION);
            LOGI("Update check: latest %s, running %s%s", rel.version.c_str(), AETHER_VERSION, newer ? " -> update available" : "");
            std::lock_guard<std::mutex> l(m_mutex);
            m_status.release = rel;
            m_status.state = newer ? UpdateState::Available : UpdateState::UpToDate;
        }
        m_busy = false;
    });
}

void Updater::install() {
    UpdateStatus st = status();
    if (st.state != UpdateState::Available) return;
    if (m_busy.exchange(true)) return;
    if (m_thread.joinable()) m_thread.join();
    m_cancel = false;
    m_thread = std::thread([this, rel = st.release] {
        runInstall(rel);
        m_busy = false;
    });
}

void Updater::runInstall(ReleaseInfo rel) {
    const bool installed = isInstalledCopy();
    const ReleaseAsset* asset = rel.find(installed ? "-Setup.exe" : "-win64.exe");
    auto fail = [&](const std::string& e) {
        LOGW("Update failed: %s", e.c_str());
        std::lock_guard<std::mutex> l(m_mutex);
        m_status.state = UpdateState::Error;
        m_status.error = e;
        m_status.release = rel;
    };
    if (!asset) return fail(installed ? "Sürümde kurulum dosyası (Setup.exe) yok" : "Sürümde exe dosyası yok");
    if (!isAllowedDownloadUrl(asset->url)) return fail("Güvenilmeyen indirme adresi");
    {
        std::lock_guard<std::mutex> l(m_mutex);
        m_status.release = rel;
        m_status.state = UpdateState::Downloading;
        m_status.received = 0;
        m_status.total = asset->size;
        m_status.error.clear();
    }
    // Expected hash: GitHub asset digest, else SHA256SUMS.txt from the same release.
    std::string expected = asset->sha256;
    if (expected.empty()) {
        if (const ReleaseAsset* sums = rel.find("SHA256SUMS.txt")) {
            std::string text, err;
            if (isAllowedDownloadUrl(sums->url) && httpGet(sums->url, &text, nullptr, err)) {
                auto m = parseSha256Sums(text);
                auto it = m.find(asset->name);
                if (it != m.end()) expected = it->second;
            }
        }
    }
    if (expected.empty()) return fail("Doğrulama özeti (SHA-256) bulunamadı; güvenlik için otomatik kurulum yapılmadı");

    std::wstring dir = Paths::root() + L"\\updates";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring dl = dir + L"\\" + widen(asset->name);
    if (!installed) {
        // Download next to the exe so the final swap is a same-volume rename; also proves the folder is writable.
        dl = exePath() + L".new";
    }
    std::string err;
    if (!httpGet(asset->url, nullptr, &dl, err)) {
        if (!installed && err.rfind("Dosya yazılamadı", 0) == 0) err = "Uygulama klasörüne yazılamıyor; yeni sürümü sürüm sayfasından indirin";
        return fail(err);
    }
    set(UpdateState::Verifying);
    std::string got = sha256File(dl);
    if (got != expected) {
        DeleteFileW(dl.c_str());
        LOGW("Update hash mismatch: expected %s got %s", expected.c_str(), got.c_str());
        return fail("İndirilen dosya doğrulanamadı (SHA-256 uyuşmuyor)");
    }
    LOGI("Update %s downloaded and verified (%s)", rel.version.c_str(), asset->name.c_str());
    set(UpdateState::Installing);

    if (installed) {
        std::wstring params = L"/S /UPDATE";
        SHELLEXECUTEINFOW sei{sizeof(sei)};
        sei.fMask = SEE_MASK_NOASYNC;
        sei.lpVerb = L"runas";
        sei.lpFile = dl.c_str();
        sei.lpParameters = params.c_str();
        sei.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&sei)) {
            DWORD e = GetLastError();
            return fail(e == ERROR_CANCELLED ? "Yönetici izni verilmedi; güncelleme iptal edildi" : format("Kurulum başlatılamadı (hata %lu)", e));
        }
    } else {
        std::wstring cur = exePath(), old = cur + L".old";
        DeleteFileW(old.c_str());
        if (!MoveFileExW(cur.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(dl.c_str());
            return fail(format("Çalışan dosya değiştirilemedi (hata %lu)", GetLastError()));
        }
        if (!MoveFileExW(dl.c_str(), cur.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DWORD e = GetLastError();
            MoveFileExW(old.c_str(), cur.c_str(), MOVEFILE_REPLACE_EXISTING);   // roll back
            DeleteFileW(dl.c_str());
            return fail(format("Yeni sürüm yerleştirilemedi (hata %lu)", e));
        }
        // Relaunch with the same arguments (e.g. --data-dir for portable setups), minus automation flags.
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::wstring cmd = L"\"" + cur + L"\" --after-update";
        for (int i = 1; argv && i < argc; ++i) {
            std::wstring a = argv[i];
            if (a == L"--after-update" || a.rfind(L"--autorun", 0) == 0 || a.rfind(L"--exit-after-run", 0) == 0 || a.rfind(L"--capture-after", 0) == 0) continue;
            cmd += a.find_first_of(L" \t") == std::wstring::npos ? L" " + a : L" \"" + a + L"\"";
        }
        if (argv) LocalFree(argv);
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        std::wstring dirW = Paths::exeDir();
        if (!CreateProcessW(cur.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dirW.c_str(), &si, &pi))
            return fail(format("Yeni sürüm başlatılamadı (hata %lu); uygulamayı yeniden açın", GetLastError()));
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    LOGI("Update %s handed over; restarting", rel.version.c_str());
    set(UpdateState::RestartPending);
}

} // namespace aether
