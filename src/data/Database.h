#pragma once
#include "Result.h"
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace aether {

// Local SQLite result store (WAL journal, schema versioned through PRAGMA user_version).
class ResultDatabase {
public:
    ~ResultDatabase();
    bool open(const std::wstring& path, std::string& error);
    void close();
    bool isOpen() const { return m_db != nullptr; }
    int64_t insert(const BenchmarkResult& r);
    bool update(const BenchmarkResult& r);
    bool get(int64_t id, BenchmarkResult& out);
    std::vector<BenchmarkResult> list(const std::string& kindFilter = "", int limit = 500);
    bool remove(int64_t id);
    bool clear();
    int count();
    std::string lastError() const { return m_error; }
private:
    bool exec(const char* sql);
    sqlite3* m_db = nullptr;
    std::mutex m_mutex;
    std::string m_error;
};

} // namespace aether
