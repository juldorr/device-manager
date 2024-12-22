#include "ReportGenerator.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>

ReportGenerator::ReportGenerator() {
    objectAnalyzer = std::make_unique<ObjectAnalyzer>();
    objectMonitor = std::make_unique<ObjectMonitor>();
}

ReportGenerator::~ReportGenerator() = default;

void ReportGenerator::generateReport(const ReportConfig& config) {
    std::wstringstream report;

    report << L"Windows Object Manager Analysis Report\n";
    report << L"Generated: " << getCurrentTimestamp() << L"\n\n";

    std::wstring targetPath = config.targetDirectory.empty() ? L"\\BaseNamedObjects" : config.targetDirectory;
    report << L"Target Directory: " << targetPath << L"\n\n";

    try {
        auto typeStats = objectAnalyzer->getTypeStatistics(targetPath);
        report << L"=== Object Type Statistics ===\n\n";

        size_t totalCount = 0;
        for (const auto& [type, count] : typeStats) {
            totalCount += count;
        }

        for (const auto& [type, count] : typeStats) {
            double percentage = (count * 100.0) / totalCount;
            report << type << L": "
                << count << L" objects ("
                << std::fixed << std::setprecision(1) << percentage << L"%)\n";
        }
        report << L"\nTotal Objects: " << totalCount << L"\n\n";

        report << L"=== Object Dependencies ===\n\n";
        auto dependencies = objectAnalyzer->buildDependencyGraph(targetPath);

        if (!dependencies.empty()) {
            std::map<std::wstring, std::vector<std::wstring>> dependencyMap;
            for (const auto& dep : dependencies) {
                if (dep.sourceObject.find(targetPath) == 0 &&
                    dep.targetObject.find(targetPath) == 0) {
                    dependencyMap[dep.sourceObject].push_back(dep.targetObject);
                }
            }

            for (const auto& [source, targets] : dependencyMap) {
                std::wstring shortSource = source.substr(targetPath.length());
                report << L"Source: " << shortSource << L"\n";
                for (size_t i = 0; i < targets.size(); ++i) {
                    std::wstring shortTarget = targets[i].substr(targetPath.length());
                    if (i == targets.size() - 1) {
                        report << L"└─── " << shortTarget << L"\n";
                    }
                    else {
                        report << L"├─── " << shortTarget << L"\n";
                    }
                }
                report << L"\n";
            }
        }
        else {
            report << L"No dependencies found in target directory\n\n";
        }

        if (config.includeStatistics) {
            report << L"=== Object Statistics ===\n\n";

            auto stats = objectAnalyzer->getTypeStatistics(targetPath);
            for (const auto& [type, count] : stats) {
                report << L"Type: " << type << L"\n"
                    << L"├─ Count: " << count << L" objects\n"
                    << L"└─ Percentage: " << std::fixed << std::setprecision(1)
                    << (count * 100.0 / totalCount) << L"%\n\n";
            }
        }
    }
    catch (const std::exception& e) {
        report << L"Error during analysis: "
            << std::wstring(e.what(), e.what() + strlen(e.what())) << L"\n";
    }

    if (!config.outputPath.empty()) {
        saveToFile(config.outputPath, config.format, report.str());
    }
}

void ReportGenerator::saveToFile(const std::wstring& filePath, ReportFormat format, const std::wstring& content) {
    std::wofstream outFile(filePath);
    if (!outFile.is_open()) {
        throw std::runtime_error("Unable to open file for writing");
    }

    switch (format) {
    case ReportFormat::HTML:
        outFile << generateHtmlReport(content);
        break;

    case ReportFormat::XML:
        outFile << generateXmlReport(content);
        break;
    }

    outFile.close();
}

std::wstring ReportGenerator::formatStatistics(const std::map<std::wstring, ObjectStatistics>& stats) {
    std::wstringstream ss;
    ss << L"\n=== Object Statistics ===\n\n";

    for (const auto& [name, stat] : stats) {
        ss << L"Object: " << name << L"\n"
            << L"  Handle Count: " << stat.handleCount << L"\n"
            << L"  Reference Count: " << stat.referenceCount << L"\n"
            << L"  Memory Usage: " << formatBytes(stat.memoryUsage) << L"\n"
            << L"  Last Access: " << formatTimestamp(stat.lastAccessTime) << L"\n\n";
    }

    return ss.str();
}

std::wstring ReportGenerator::formatAnalytics(const std::vector<ObjectDependency>& dependencies) {
    std::wstringstream ss;
    ss << L"\n=== Object Dependencies ===\n\n";

    for (const auto& dep : dependencies) {
        ss << L"Source: " << dep.sourceObject << L"\n"
            << L"Target: " << dep.targetObject << L"\n"
            << L"Type: " << dep.dependencyType << L"\n\n";
    }

    return ss.str();
}

std::wstring ReportGenerator::formatTypeStatistics(const std::map<std::wstring, size_t>& typeStats) {
    std::wstringstream ss;
    ss << L"\n=== Object Type Statistics ===\n\n";

    for (const auto& [type, count] : typeStats) {
        ss << type << L": " << count << L" objects\n";
    }

    return ss.str();
}

std::wstring ReportGenerator::generateHtmlReport(const std::wstring& content) {
    std::wstringstream ss;
    ss << L"<!DOCTYPE html>\n"
        << L"<html>\n<head>\n"
        << L"<title>Windows Object Manager Report</title>\n"
        << L"<style>\n"
        << L"body { font-family: Arial, sans-serif; margin: 40px; }\n"
        << L"h1 { color: #333; }\n"
        << L"pre { background-color: #f5f5f5; padding: 10px; }\n"
        << L"</style>\n"
        << L"</head>\n<body>\n"
        << L"<h1>Windows Object Manager Report</h1>\n"
        << L"<pre>" << content << L"</pre>\n"
        << L"</body>\n</html>";
    return ss.str();
}

std::wstring ReportGenerator::generateXmlReport(const std::wstring& content) {
    std::wstringstream ss;
    ss << L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
        << L"<report>\n"
        << L"  <timestamp>" << getCurrentTimestamp() << L"</timestamp>\n"
        << L"  <content>" << escapeXmlString(content) << L"</content>\n"
        << L"</report>";
    return ss.str();
}

std::wstring ReportGenerator::getCurrentTimestamp() {
    SYSTEMTIME st;
    GetSystemTime(&st);
    std::wstringstream ss;
    ss << std::setfill(L'0')
        << st.wYear << L"-"
        << std::setw(2) << st.wMonth << L"-"
        << std::setw(2) << st.wDay << L" "
        << std::setw(2) << st.wHour << L":"
        << std::setw(2) << st.wMinute << L":"
        << std::setw(2) << st.wSecond;
    return ss.str();
}

std::wstring ReportGenerator::formatTimestamp(const SYSTEMTIME& st) {
    std::wstringstream ss;
    ss << std::setfill(L'0')
        << st.wYear << L"-"
        << std::setw(2) << st.wMonth << L"-"
        << std::setw(2) << st.wDay << L" "
        << std::setw(2) << st.wHour << L":"
        << std::setw(2) << st.wMinute << L":"
        << std::setw(2) << st.wSecond;
    return ss.str();
}

std::wstring ReportGenerator::formatBytes(SIZE_T bytes) {
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB" };
    int unitIndex = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024 && unitIndex < 3) {
        size /= 1024;
        unitIndex++;
    }

    std::wstringstream ss;
    ss << std::fixed << std::setprecision(2) << size << L" " << units[unitIndex];
    return ss.str();
}

std::wstring ReportGenerator::escapeJsonString(const std::wstring& input) {
    std::wstringstream ss;
    for (wchar_t c : input) {
        switch (c) {
        case L'"': ss << L"\\\""; break;
        case L'\\': ss << L"\\\\"; break;
        case L'\b': ss << L"\\b"; break;
        case L'\f': ss << L"\\f"; break;
        case L'\n': ss << L"\\n"; break;
        case L'\r': ss << L"\\r"; break;
        case L'\t': ss << L"\\t"; break;
        default:
            if (c < 32) {
                ss << L"\\u" << std::hex << std::setw(4) << std::setfill(L'0') << static_cast<int>(c);
            }
            else {
                ss << c;
            }
        }
    }
    return ss.str();
}

std::wstring ReportGenerator::escapeXmlString(const std::wstring& input) {
    std::wstringstream ss;
    for (wchar_t c : input) {
        switch (c) {
        case L'<': ss << L"&lt;"; break;
        case L'>': ss << L"&gt;"; break;
        case L'&': ss << L"&amp;"; break;
        case L'"': ss << L"&quot;"; break;
        case L'\'': ss << L"&apos;"; break;
        default: ss << c;
        }
    }
    return ss.str();
}
