#include "Database.h"
#include "../core/Log.h"
#include "../core/Strings.h"
#include <sqlite3.h>

namespace aether {

static const int kSchemaVersion = 1;

ResultDatabase::~ResultDatabase() { close(); }

bool ResultDatabase::exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        m_error = err ? err : "sqlite error";
        sqlite3_free(err);
        LOGE("SQL error: %s", m_error.c_str());
        return false;
    }
    return true;
}

bool ResultDatabase::open(const std::wstring& path, std::string& error) {
    std::lock_guard<std::mutex> l(m_mutex);
    if (m_db) return true;
    if (sqlite3_open_v2(narrow(path).c_str(), &m_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        error = m_db ? sqlite3_errmsg(m_db) : "sqlite3_open failed";
        if (m_db) { sqlite3_close(m_db); m_db = nullptr; }
        return false;
    }
    sqlite3_busy_timeout(m_db, 2000);
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA synchronous=NORMAL;");
    int version = 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, "PRAGMA user_version;", -1, &st, nullptr) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
        version = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    if (version > kSchemaVersion) {
        error = "Veritabanı daha yeni bir sürümle oluşturulmuş";
        return false;
    }
    bool ok = exec("CREATE TABLE IF NOT EXISTS results ("
                   " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                   " kind TEXT NOT NULL, timestamp TEXT NOT NULL, gpu TEXT, config_key TEXT,"
                   " score REAL, avg_fps REAL, status TEXT, doc TEXT NOT NULL);"
                   "CREATE INDEX IF NOT EXISTS idx_results_kind ON results(kind, timestamp);"
                   "PRAGMA user_version=1;");
    if (!ok) { error = m_error; return false; }
    return true;
}

void ResultDatabase::close() {
    std::lock_guard<std::mutex> l(m_mutex);
    if (m_db) { sqlite3_close(m_db); m_db = nullptr; }
}

int64_t ResultDatabase::insert(const BenchmarkResult& r) {
    std::lock_guard<std::mutex> l(m_mutex);
    if (!m_db) return 0;
    sqlite3_stmt* st = nullptr;
    const char* sql = "INSERT INTO results(kind,timestamp,gpu,config_key,score,avg_fps,status,doc) VALUES(?,?,?,?,?,?,?,?);";
    if (sqlite3_prepare_v2(m_db, sql, -1, &st, nullptr) != SQLITE_OK) { m_error = sqlite3_errmsg(m_db); return 0; }
    std::string doc = resultToJson(r, -1);
    sqlite3_bind_text(st, 1, r.kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, r.timestamp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, r.gpuName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, r.configKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(st, 5, r.score);
    sqlite3_bind_double(st, 6, r.avgFps);
    sqlite3_bind_text(st, 7, r.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 8, doc.c_str(), -1, SQLITE_TRANSIENT);
    int64_t id = 0;
    if (sqlite3_step(st) == SQLITE_DONE) id = sqlite3_last_insert_rowid(m_db);
    else m_error = sqlite3_errmsg(m_db);
    sqlite3_finalize(st);
    if (id) {   // persist the assigned id inside the document as well
        BenchmarkResult copy = r;
        copy.id = id;
        std::string d2 = resultToJson(copy, -1);
        if (sqlite3_prepare_v2(m_db, "UPDATE results SET doc=? WHERE id=?;", -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, d2.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 2, id);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }
    return id;
}

bool ResultDatabase::update(const BenchmarkResult& r) {
    std::lock_guard<std::mutex> l(m_mutex);
    if (!m_db || r.id == 0) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, "UPDATE results SET score=?, avg_fps=?, status=?, doc=? WHERE id=?;", -1, &st, nullptr) != SQLITE_OK)
        return false;
    std::string doc = resultToJson(r, -1);
    sqlite3_bind_double(st, 1, r.score);
    sqlite3_bind_double(st, 2, r.avgFps);
    sqlite3_bind_text(st, 3, r.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, doc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 5, r.id);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool ResultDatabase::get(int64_t id, BenchmarkResult& out) {
    std::lock_guard<std::mutex> l(m_mutex);
    if (!m_db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT doc FROM results WHERE id=?;", -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(st, 1, id);
    bool ok = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        ok = resultFromJson((const char*)sqlite3_column_text(st, 0), out);
        out.id = id;
    }
    sqlite3_finalize(st);
    return ok;
}

std::vector<BenchmarkResult> ResultDatabase::list(const std::string& kind, int limit) {
    std::vector<BenchmarkResult> v;
    std::lock_guard<std::mutex> l(m_mutex);
    if (!m_db) return v;
    sqlite3_stmt* st = nullptr;
    std::string sql = "SELECT id, doc FROM results";
    if (!kind.empty()) sql += " WHERE kind=?";
    sql += " ORDER BY id DESC LIMIT ?;";
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return v;
    int idx = 1;
    if (!kind.empty()) sqlite3_bind_text(st, idx++, kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, idx, limit);
    while (sqlite3_step(st) == SQLITE_ROW) {
        BenchmarkResult r;
        if (resultFromJson((const char*)sqlite3_column_text(st, 1), r)) {
            r.id = sqlite3_column_int64(st, 0);
            v.push_back(std::move(r));
        }
    }
    sqlite3_finalize(st);
    return v;
}

bool ResultDatabase::remove(int64_t id) {
    std::lock_guard<std::mutex> l(m_mutex);
    if (!m_db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM results WHERE id=?;", -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(st, 1, id);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool ResultDatabase::clear() { std::lock_guard<std::mutex> l(m_mutex); return m_db && exec("DELETE FROM results;"); }

int ResultDatabase::count() {
    std::lock_guard<std::mutex> l(m_mutex);
    if (!m_db) return 0;
    sqlite3_stmt* st = nullptr;
    int n = 0;
    if (sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM results;", -1, &st, nullptr) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
        n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n;
}

} // namespace aether
