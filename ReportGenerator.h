#pragma once
#include <string>
#include <vector>
#include <memory>
#include "ObjectMonitor.h"
#include "ObjectAnalyzer.h"

enum class ReportFormat {
    HTML,
    XML
};

struct ReportConfig {
    ReportFormat format;
    bool includeStatistics;
    bool includeAnalytics;
    std::wstring outputPath;
    std::wstring targetDirectory;
};

class ReportGenerator {
public:
    ReportGenerator();
    ~ReportGenerator();

    void generateReport(const ReportConfig& config);

private:
    std::unique_ptr<ObjectMonitor> objectMonitor;
    std::unique_ptr<ObjectAnalyzer> objectAnalyzer;

    void saveToFile(const std::wstring& filePath, ReportFormat format, const std::wstring& content);

    std::wstring formatStatistics(const std::map<std::wstring, ObjectStatistics>& stats);
    std::wstring formatAnalytics(const std::vector<ObjectDependency>& dependencies);
    std::wstring formatTypeStatistics(const std::map<std::wstring, size_t>& typeStats);

    std::wstring generateHtmlReport(const std::wstring& content);
    std::wstring generateXmlReport(const std::wstring& content);

    std::wstring getCurrentTimestamp();
    std::wstring formatTimestamp(const SYSTEMTIME& st);
    std::wstring formatBytes(SIZE_T bytes);
    std::wstring escapeJsonString(const std::wstring& input);
    std::wstring escapeXmlString(const std::wstring& input);
};