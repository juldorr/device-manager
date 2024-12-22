#include "ObjectMonitor.h"
#include <windows.h>
#include <winternl.h>
#include <ntstatus.h>
#include <iostream>
#include <set>

// Required for NtQueryDirectoryObject
#pragma comment(lib, "ntdll.lib")

#define DIRECTORY_QUERY                 0x0001
#define SYMBOLIC_LINK_QUERY            0x0001

typedef struct _OBJECT_DIRECTORY_INFORMATION {
    UNICODE_STRING Name;
    UNICODE_STRING TypeName;
} OBJECT_DIRECTORY_INFORMATION, * POBJECT_DIRECTORY_INFORMATION;

extern "C" NTSTATUS NTAPI NtQueryDirectoryObject(
    HANDLE DirectoryHandle,
    PVOID Buffer,
    ULONG Length,
    BOOLEAN ReturnSingleEntry,
    BOOLEAN RestartScan,
    PULONG Context,
    PULONG ReturnLength
);

extern "C" NTSTATUS NTAPI NtOpenDirectoryObject(
    PHANDLE DirectoryHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes
);

ObjectMonitor::ObjectMonitor() : isMonitoring(false) {
}

ObjectMonitor::~ObjectMonitor() {
    stopMonitoring();
}

void ObjectMonitor::startMonitoring(const std::wstring& path) {
    if (isMonitoring) {
        return;
    }

    monitoringPath = path;
    isMonitoring = true;
    monitorThread = std::thread(&ObjectMonitor::monitoringThread, this);
}

void ObjectMonitor::stopMonitoring() {
    if (!isMonitoring) {
        return;
    }

    isMonitoring = false;
    if (monitorThread.joinable()) {
        monitorThread.join();
    }
}

void ObjectMonitor::setChangeCallback(std::function<void(const ObjectChangeInfo&)> callback) {
    changeCallback = callback;
}

std::map<std::wstring, ObjectStatistics> ObjectMonitor::getObjectsStatistics() {
    return statistics;
}

void ObjectMonitor::updateStatistics() {
    HANDLE hDirectory = nullptr;
    OBJECT_ATTRIBUTES objAttributes = { 0 };
    UNICODE_STRING uniPath = { 0 };

    RtlInitUnicodeString(&uniPath, monitoringPath.c_str());

    InitializeObjectAttributes(&objAttributes,
        &uniPath,
        OBJ_CASE_INSENSITIVE,
        NULL,
        NULL);

    NTSTATUS status = NtOpenDirectoryObject(&hDirectory,
        DIRECTORY_QUERY,
        &objAttributes);

    if (!NT_SUCCESS(status)) {
        return;
    }

    std::vector<std::pair<std::wstring, std::wstring>> currentObjects;
    const ULONG bufferSize = 8192;
    ULONG context = 0;
    ULONG returnLength;
    BYTE buffer[bufferSize] = { 0 };
    BOOLEAN restart = TRUE;

    while (TRUE) {
        status = NtQueryDirectoryObject(hDirectory,
            buffer,
            bufferSize,
            FALSE,
            restart,
            &context,
            &returnLength);

        if (!NT_SUCCESS(status)) {
            break;
        }

        POBJECT_DIRECTORY_INFORMATION info = (POBJECT_DIRECTORY_INFORMATION)buffer;
        while (info->Name.Length != 0) {
            std::wstring name(info->Name.Buffer, info->Name.Length / sizeof(WCHAR));
            std::wstring type(info->TypeName.Buffer, info->TypeName.Length / sizeof(WCHAR));

            currentObjects.push_back({ name, type });

            ObjectStatistics& stats = statistics[name];
            stats.handleCount = 0; 
            stats.referenceCount = 0; 
            stats.memoryUsage = 0; 
            GetSystemTime(&stats.lastAccessTime);

            info++;
        }

        restart = FALSE;
    }

    CloseHandle(hDirectory);
}

void ObjectMonitor::monitoringThread() {
    std::vector<std::pair<std::wstring, std::wstring>> prevObjects;

    while (isMonitoring) {
        std::vector<std::pair<std::wstring, std::wstring>> currentObjects;

        HANDLE hDirectory = nullptr;
        OBJECT_ATTRIBUTES objAttributes = { 0 };
        UNICODE_STRING uniPath = { 0 };

        RtlInitUnicodeString(&uniPath, monitoringPath.c_str());
        InitializeObjectAttributes(&objAttributes, &uniPath, OBJ_CASE_INSENSITIVE, NULL, NULL);

        if (NT_SUCCESS(NtOpenDirectoryObject(&hDirectory, DIRECTORY_QUERY, &objAttributes))) {
            const ULONG bufferSize = 8192;
            ULONG context = 0;
            ULONG returnLength;
            BYTE buffer[bufferSize] = { 0 };
            BOOLEAN restart = TRUE;

            while (TRUE) {
                NTSTATUS status = NtQueryDirectoryObject(hDirectory,
                    buffer,
                    bufferSize,
                    FALSE,
                    restart,
                    &context,
                    &returnLength);

                if (!NT_SUCCESS(status)) {
                    break;
                }

                POBJECT_DIRECTORY_INFORMATION info = (POBJECT_DIRECTORY_INFORMATION)buffer;
                while (info->Name.Length != 0) {
                    std::wstring name(info->Name.Buffer, info->Name.Length / sizeof(WCHAR));
                    std::wstring type(info->TypeName.Buffer, info->TypeName.Length / sizeof(WCHAR));
                    currentObjects.push_back({ name, type });
                    info++;
                }

                restart = FALSE;
            }

            CloseHandle(hDirectory);
        }

        if (!prevObjects.empty()) {
            std::set<std::pair<std::wstring, std::wstring>> prevSet(prevObjects.begin(), prevObjects.end());
            std::set<std::pair<std::wstring, std::wstring>> currSet(currentObjects.begin(), currentObjects.end());

            for (const auto& obj : currSet) {
                if (prevSet.find(obj) == prevSet.end()) {
                    ObjectChangeInfo changeInfo;
                    changeInfo.objectName = obj.first;
                    changeInfo.objectType = obj.second;
                    changeInfo.changeType = L"Created";
                    GetSystemTime(&changeInfo.timestamp);

                    if (changeCallback) {
                        changeCallback(changeInfo);
                    }
                }
            }

            for (const auto& obj : prevSet) {
                if (currSet.find(obj) == currSet.end()) {
                    ObjectChangeInfo changeInfo;
                    changeInfo.objectName = obj.first;
                    changeInfo.objectType = obj.second;
                    changeInfo.changeType = L"Deleted";
                    GetSystemTime(&changeInfo.timestamp);

                    if (changeCallback) {
                        changeCallback(changeInfo);
                    }
                }
            }
        }

        prevObjects = currentObjects;
        updateStatistics();

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

bool ObjectMonitor::compareObjectLists(
    const std::vector<std::pair<std::wstring, std::wstring>>& oldList,
    const std::vector<std::pair<std::wstring, std::wstring>>& newList) {

    return oldList == newList;
}
