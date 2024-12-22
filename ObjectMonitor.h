#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <map>

struct ObjectChangeInfo {
    std::wstring objectName;
    std::wstring objectType;
    std::wstring changeType;
    SYSTEMTIME timestamp;
};

struct ObjectStatistics {
    ULONG handleCount;
    ULONG referenceCount;
    SIZE_T memoryUsage;
    SYSTEMTIME lastAccessTime;
};

class ObjectMonitor {
public:
    ObjectMonitor();
    ~ObjectMonitor();

    void startMonitoring(const std::wstring& path);
    void stopMonitoring();

    void setChangeCallback(std::function<void(const ObjectChangeInfo&)> callback);

    std::map<std::wstring, ObjectStatistics> getObjectsStatistics();
    void updateStatistics();

private:
    void monitoringThread();
    bool compareObjectLists(
        const std::vector<std::pair<std::wstring, std::wstring>>& oldList,
        const std::vector<std::pair<std::wstring, std::wstring>>& newList
    );

    std::thread monitorThread;
    std::atomic<bool> isMonitoring;
    std::wstring monitoringPath;
    std::function<void(const ObjectChangeInfo&)> changeCallback;
    std::map<std::wstring, ObjectStatistics> statistics;
};
