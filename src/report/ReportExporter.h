#pragma once
#include "../data/Result.h"
#include <string>

namespace aether {

struct ReportContext {
    std::string osName, cpuName, ramText, appVersion;
};

class ReportExporter {
public:
    static bool exportJson(const BenchmarkResult& r, const std::wstring& path, std::string& error);
    static bool exportCsv(const BenchmarkResult& r, const std::wstring& path, std::string& error);
    // `compare` may be null. Comparison is only rendered when the configuration keys match.
    static bool exportPdf(const BenchmarkResult& r, const BenchmarkResult* compare, const ReportContext& ctx,
                          const std::wstring& path, std::string& error);
    static std::string csv(const BenchmarkResult& r);
    static std::string csvEscape(const std::string& s);
};

} // namespace aether
