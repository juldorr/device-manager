#include "ObjectManagerExplorer.h"
#include "ObjectMonitor.h"
#include "ReportGenerator.h" 
#include "ObjectAnalyzer.h"
#include <iostream>
#include <string>
#include <iomanip>
#include <limits>

// Existing callback for object changes 
void handleObjectChange(const ObjectChangeInfo& changeInfo) {
    SYSTEMTIME& time = const_cast<SYSTEMTIME&>(changeInfo.timestamp);
    std::wcout << std::setfill(L'0')
        << time.wYear << L"-"
        << std::setw(2) << time.wMonth << L"-"
        << std::setw(2) << time.wDay << L" "
        << std::setw(2) << time.wHour << L":"
        << std::setw(2) << time.wMinute << L":"
        << std::setw(2) << time.wSecond << L" - ";

    std::wcout << L"Object " << changeInfo.objectName
        << L" (" << changeInfo.objectType << L") "
        << changeInfo.changeType << L"\n";
}

// Callback for object analysis results
void handleAnalysisResults(
    const std::wstring& objectName,
    const std::wstring& objectType,
    ULONG handleCount,
    ULONG referenceCount,
    const std::vector<std::wstring>& linkedObjects,
    ACCESS_MASK accessMask) {

    std::wcout << L"\nAnalysis Results for: " << objectName << L"\n"
        << L"Type: " << objectType << L"\n"
        << L"Handle Count: " << handleCount << L"\n"
        << L"Reference Count: " << referenceCount << L"\n"
        << L"Access Mask: 0x" << std::hex << accessMask << std::dec << L"\n";

    if (!linkedObjects.empty()) {
        std::wcout << L"Linked Objects:\n";
        for (const auto& obj : linkedObjects) {
            std::wcout << L"  - " << obj << L"\n";
        }
    }
}

void printMenu() {
    std::wcout << L"\n--- Object Manager Explorer ---\n"
        << L"0. Exit\n"
        << L"1. List all objects in a directory\n"
        << L"2. List objects by type\n"
        << L"3. Display information about an object\n"
        << L"4. Explore namespace recursively\n"
        << L"5. Start real-time monitoring\n"
        << L"6. Stop monitoring\n"
        << L"7. Show current statistics\n"
        << L"8. Generate Report\n"
        << L"9. Build dependency graph\n"
        << L"10. Show type statistics\n"
        << L"Select an option: ";
}

void printReportFormatMenu() {
    std::wcout << L"\nSelect report format:\n"
        << L"1. HTML\n"
        << L"2. XML\n"
        << L"Select format: ";
}

// Helper function to safely get integer input with validation
int getValidatedIntegerInput(int minValue, int maxValue) {
    int input;
    while (true) {
        if (std::wcin >> input) {
            // Clear any remaining characters in the input buffer
            std::wcin.ignore(1000, L'\n');

            // Check if the input is within valid range
            if (input >= minValue && input <= maxValue) {
                return input;
            }
            std::wcout << L"Invalid input. Please enter a number between "
                << minValue << L" and " << maxValue << L": ";
        }
        else {
            // Clear error flags
            std::wcin.clear();
            // Clear any remaining characters in the input buffer
            std::wcin.ignore(1000, L'\n');
            std::wcout << L"Invalid input. Please enter a number between "
                << minValue << L" and " << maxValue << L": ";
        }
    }
}

// Helper function to safely get boolean input
bool getValidatedBooleanInput(const std::wstring& prompt) {
    int input;
    std::wcout << prompt;
    input = getValidatedIntegerInput(0, 1);
    return input == 1;
}

int main() {
    ObjectManagerExplorer explorer;
    ObjectMonitor monitor;
    ReportGenerator reporter;
    ObjectAnalyzer analyzer;

    int choice;
    std::wstring path;
    std::wstring objectName;
    std::wstring filterType;
    bool isMonitoring = false;

    monitor.setChangeCallback(handleObjectChange);
    analyzer.setAnalysisCallback(handleAnalysisResults);

    while (true) {
        try {
            printMenu();
            choice = getValidatedIntegerInput(0, 10);

            switch (choice) {
            case 0:
                if (isMonitoring) {
                    monitor.stopMonitoring();
                }
                std::wcout << L"Exiting program.\n";
                return 0;

            case 1:
                std::wcout << L"Enter directory path (e.g., \\BaseNamedObjects): ";
                std::getline(std::wcin, path);
                explorer.listObjects(path);
                break;

            case 2:
                std::wcout << L"Enter directory path (e.g., \\BaseNamedObjects): ";
                std::getline(std::wcin, path);
                std::wcout << L"Enter object type to filter (e.g., Event, Mutex, Semaphore): ";
                std::getline(std::wcin, filterType);
                explorer.listObjects(path, filterType);
                break;
                
            case 3:
                std::wcout << L"Enter object name (e.g., \\BaseNamedObjects\\CPFATE_12280_v4.0.30319): ";
                std::getline(std::wcin, objectName);
                explorer.displayObjectInfo(objectName);
                break;

            case 4:
                std::wcout << L"Enter directory path to explore recursively (e.g., \\BaseNamedObjects): ";
                std::getline(std::wcin, path);
                explorer.exploreNamespace(path, true);
                break;

            case 5:
                if (!isMonitoring) {
                    std::wcout << L"Enter directory path to monitor (e.g., \\BaseNamedObjects): ";
                    std::getline(std::wcin, path);
                    monitor.startMonitoring(path);
                    isMonitoring = true;
                    std::wcout << L"Monitoring started. You will see notifications about object changes.\n";
                }
                else {
                    std::wcout << L"Monitoring is already active.\n";
                }
                break;

            case 6:
                if (isMonitoring) {
                    monitor.stopMonitoring();
                    isMonitoring = false;
                    std::wcout << L"Monitoring stopped.\n";
                }
                else {
                    std::wcout << L"Monitoring is not active.\n";
                }
                break;

            case 7:
                if (isMonitoring) {
                    auto stats = monitor.getObjectsStatistics();
                    std::wcout << L"\nCurrent Object Statistics:\n";
                    std::wcout << L"=======================\n";

                    for (const auto& [name, stat] : stats) {
                        std::wcout << L"Object: " << name << L"\n"
                            << L"  Handles: " << stat.handleCount << L"\n"
                            << L"  References: " << stat.referenceCount << L"\n"
                            << L"  Memory Usage: " << stat.memoryUsage << L" bytes\n"
                            << L"  Last Access: "
                            << std::setfill(L'0')
                            << stat.lastAccessTime.wYear << L"-"
                            << std::setw(2) << stat.lastAccessTime.wMonth << L"-"
                            << std::setw(2) << stat.lastAccessTime.wDay << L" "
                            << std::setw(2) << stat.lastAccessTime.wHour << L":"
                            << std::setw(2) << stat.lastAccessTime.wMinute << L":"
                            << std::setw(2) << stat.lastAccessTime.wSecond << L"\n"
                            << L"------------------------\n";
                    }
                }
                else {
                    std::wcout << L"Start monitoring first to collect statistics.\n";
                }
                break;

            case 8:
            {
                ReportConfig config;
                std::wstring outputPath;

                std::wcout << L"Enter target directory path (e.g., \\BaseNamedObjects): ";
                std::getline(std::wcin, config.targetDirectory);

                printReportFormatMenu();
                int formatChoice = getValidatedIntegerInput(1, 2);

                switch (formatChoice) {
                case 1: config.format = ReportFormat::HTML; break;
                case 2: config.format = ReportFormat::XML; break;
                }

                std::wcout << L"Enter output file path (e.g., D:\\report.html): ";
                std::getline(std::wcin, outputPath);
                config.outputPath = outputPath;

                try {
                    reporter.generateReport(config);
                    std::wcout << L"Report generated successfully at: " << outputPath << L"\n";
                }
                catch (const std::exception& e) {
                    std::wcout << L"Error generating report: "
                        << std::wstring(e.what(), e.what() + strlen(e.what())) << L"\n";
                }
            }
            break;

            case 9:
            {
                std::wcout << L"Enter root object name or directory path (e.g., \\BaseNamedObjects): ";
                std::getline(std::wcin, objectName);

                try {
                    auto dependencies = analyzer.buildDependencyGraph(objectName);
                    std::wcout << L"\nDependency Graph:\n";
                    std::wcout << L"=================\n";

                    if (dependencies.empty()) {
                        std::wcout << L"No dependencies found.\n";
                    }
                    else {
                        for (const auto& dep : dependencies) {
                            std::wcout << dep.sourceObject << L" -> "
                                << dep.targetObject << L" ("
                                << dep.dependencyType << L")\n";
                        }
                        std::wcout << L"\nTotal dependencies found: " << dependencies.size() << L"\n";
                    }
                }
                catch (const std::exception& e) {
                    std::wcout << L"Error building dependency graph: "
                        << std::wstring(e.what(), e.what() + strlen(e.what())) << L"\n";
                }
            }
            break;

            case 10:
            {
                std::wstring dirPath;
                std::wcout << L"Enter directory path to analyze (e.g., \\BaseNamedObjects): ";
                std::getline(std::wcin, dirPath);

                try {
                    auto typeStats = analyzer.getTypeStatistics(dirPath);
                    std::wcout << L"\nObject Type Statistics:\n";
                    std::wcout << L"=====================\n";

                    for (const auto& [type, count] : typeStats) {
                        std::wcout << type << L": " << count << L" objects\n";
                    }
                }
                catch (const std::exception& e) {
                    std::wcout << L"Error getting type statistics: "
                        << std::wstring(e.what(), e.what() + strlen(e.what())) << L"\n";
                }
            }
            break;
            }
        }
        catch (const std::exception& e) {
            std::wcout << L"An unexpected error occurred: "
                << std::wstring(e.what(), e.what() + strlen(e.what())) << L"\n";
            std::wcout << L"Press Enter to continue...";
            std::wcin.ignore(1000, L'\n');
        }
    }
}