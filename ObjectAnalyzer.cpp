#include "ObjectAnalyzer.h"
#include <memory>
#include <queue>
#include <set>
#include <stdexcept>    
#include <winternl.h>
#include <psapi.h>

// Required for NtQueryDirectoryObject
#pragma comment(lib, "ntdll.lib")

#define DIRECTORY_QUERY                 0x0001
#define SYMBOLIC_LINK_QUERY            0x0001
#define STATUS_NO_MORE_ENTRIES         ((NTSTATUS)0x8000001AL)
#define STATUS_INFO_LENGTH_MISMATCH    ((NTSTATUS)0xC0000004L)
#define SystemExtendedHandleInformation 64


extern "C" NTSTATUS NTAPI NtOpenDirectoryObject(
    PHANDLE DirectoryHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes
);

extern "C" NTSTATUS NTAPI NtQueryDirectoryObject(
    HANDLE DirectoryHandle,
    PVOID Buffer,
    ULONG Length,
    BOOLEAN ReturnSingleEntry,
    BOOLEAN RestartScan,
    PULONG Context,
    PULONG ReturnLength
);

typedef struct _OBJECT_DIRECTORY_INFORMATION {
    UNICODE_STRING Name;
    UNICODE_STRING TypeName;
} OBJECT_DIRECTORY_INFORMATION, * POBJECT_DIRECTORY_INFORMATION;

typedef struct _SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX {
    PVOID Object;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR HandleValue;
    ULONG GrantedAccess;
    USHORT CreatorBackTraceIndex;
    USHORT ObjectTypeIndex;
    ULONG HandleAttributes;
    ULONG Reserved;
} SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX, * PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX;

typedef struct _SYSTEM_HANDLE_INFORMATION_EX {
    ULONG_PTR NumberOfHandles;
    ULONG_PTR Reserved;
    SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX Handles[1];
} SYSTEM_HANDLE_INFORMATION_EX, * PSYSTEM_HANDLE_INFORMATION_EX;

typedef struct _OBJECT_TYPE_INFORMATION {
    UNICODE_STRING TypeName;
    ULONG TotalNumberOfHandles;
    ULONG TotalNumberOfObjects;
} OBJECT_TYPE_INFORMATION, * POBJECT_TYPE_INFORMATION;

extern "C" {
    NTSTATUS NTAPI NtDuplicateObject(
        HANDLE SourceProcessHandle,
        HANDLE SourceHandle,
        HANDLE TargetProcessHandle,
        PHANDLE TargetHandle,
        ACCESS_MASK DesiredAccess,
        ULONG HandleAttributes,
        ULONG Options
    );
}

extern "C" {
    NTSTATUS NTAPI NtQuerySymbolicLinkObject(
        HANDLE LinkHandle,
        PUNICODE_STRING LinkTarget,
        PULONG ReturnedLength
    );
}

extern "C" NTSTATUS NTAPI NtOpenSymbolicLinkObject(
    PHANDLE LinkHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes
);

#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#define STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS)0xC0000004L)
#define SystemExtendedHandleInformation 64

extern "C" NTSTATUS NTAPI NtOpenSection(
    PHANDLE SectionHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes
);

typedef struct _OBJECT_NAME_INFORMATION {
    UNICODE_STRING Name;
} OBJECT_NAME_INFORMATION, * POBJECT_NAME_INFORMATION;

ObjectAnalyzer::ObjectAnalyzer() {}
ObjectAnalyzer::~ObjectAnalyzer() {}

std::vector<ObjectDependency> ObjectAnalyzer::buildDependencyGraph(const std::wstring& rootObject) {
    std::vector<ObjectDependency> dependencies;
    std::queue<std::wstring> objectQueue;
    std::set<std::wstring> visitedObjects;
    HANDLE hRootDir = nullptr;

    bool isDirectory = (rootObject.find(L"\\BaseNamedObjects") != std::wstring::npos) ||
        (rootObject == L"\\");

    if (isDirectory) {
        UNICODE_STRING uniRootPath;
        OBJECT_ATTRIBUTES objAttributes = { sizeof(OBJECT_ATTRIBUTES) };
        RtlInitUnicodeString(&uniRootPath, rootObject.c_str());
        InitializeObjectAttributes(&objAttributes, &uniRootPath, OBJ_CASE_INSENSITIVE, NULL, NULL);

        NTSTATUS status = NtOpenDirectoryObject(&hRootDir, DIRECTORY_QUERY, &objAttributes);
        if (!NT_SUCCESS(status)) {
            return dependencies;
        }

        BYTE buffer[8192];
        ULONG context = 0;
        ULONG returnLength;
        BOOLEAN restart = TRUE;

        while (TRUE) {
            status = NtQueryDirectoryObject(
                hRootDir,
                buffer,
                sizeof(buffer),
                FALSE,
                restart,
                &context,
                &returnLength
            );

            if (!NT_SUCCESS(status) || status == STATUS_NO_MORE_ENTRIES) {
                break;
            }

            POBJECT_DIRECTORY_INFORMATION dirInfo =
                reinterpret_cast<POBJECT_DIRECTORY_INFORMATION>(buffer);

            while (dirInfo->Name.Length != 0) {
                std::wstring objName(dirInfo->Name.Buffer,
                    dirInfo->Name.Length / sizeof(WCHAR));
                std::wstring objType(dirInfo->TypeName.Buffer,
                    dirInfo->TypeName.Length / sizeof(WCHAR));

                std::wstring fullPath = rootObject;
                if (fullPath.back() != L'\\') fullPath += L"\\";
                fullPath += objName;

                if (objType == L"SymbolicLink") {
                    ObjectDependency dep;
                    dep.sourceObject = fullPath;

                    UNICODE_STRING targetPath;
                    OBJECT_ATTRIBUTES linkAttr = { sizeof(OBJECT_ATTRIBUTES) };
                    RtlInitUnicodeString(&targetPath, fullPath.c_str());
                    InitializeObjectAttributes(&linkAttr, &targetPath,
                        OBJ_CASE_INSENSITIVE, NULL, NULL);

                    HANDLE hLink;
                    if (NT_SUCCESS(NtOpenSymbolicLinkObject(&hLink, SYMBOLIC_LINK_QUERY,
                        &linkAttr))) {
                        UNICODE_STRING target;
                        WCHAR targetBuffer[MAX_PATH];
                        target.Buffer = targetBuffer;
                        target.Length = 0;
                        target.MaximumLength = MAX_PATH * sizeof(WCHAR);

                        if (NT_SUCCESS(NtQuerySymbolicLinkObject(hLink, &target, NULL))) {
                            dep.targetObject = std::wstring(target.Buffer,
                                target.Length / sizeof(WCHAR));
                            dep.dependencyType = L"SymbolicLink";
                            dependencies.push_back(dep);
                        }
                        NtClose(hLink);
                    }
                }
                else if (objType == L"Section") {
                    HANDLE hProcesses[1024];
                    ULONG cbNeeded;
                    if (EnumProcesses((DWORD*)hProcesses, sizeof(hProcesses), &cbNeeded)) {
                        DWORD numProcesses = cbNeeded / sizeof(DWORD);
                        for (DWORD i = 0; i < numProcesses; i++) {
                            if (HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION,
                                FALSE, (DWORD)hProcesses[i])) {
                                HANDLE hSection;
                                UNICODE_STRING sectionName;
                                RtlInitUnicodeString(&sectionName, fullPath.c_str());
                                OBJECT_ATTRIBUTES sectionAttr = { sizeof(OBJECT_ATTRIBUTES) };
                                InitializeObjectAttributes(&sectionAttr, &sectionName,
                                    OBJ_CASE_INSENSITIVE, NULL, NULL);

                                
                                if (NT_SUCCESS(NtOpenSection(&hSection, SECTION_QUERY,
                                    &sectionAttr))) {
                                    ObjectDependency dep;
                                    dep.sourceObject = fullPath;
                                    dep.targetObject = L"Process:" +
                                        std::to_wstring((DWORD)hProcesses[i]);
                                    dep.dependencyType = L"SharedMemory";
                                    dependencies.push_back(dep);
                                    NtClose(hSection);
                                }
                                CloseHandle(hProcess);
                            }
                        }
                    }
                }

                dirInfo++;
            }
            restart = FALSE;
        }

        if (hRootDir) {
            NtClose(hRootDir);
        }
    }
    else {
        HANDLE hObject;
        UNICODE_STRING objName;
        OBJECT_ATTRIBUTES objAttr = { sizeof(OBJECT_ATTRIBUTES) };
        RtlInitUnicodeString(&objName, rootObject.c_str());
        InitializeObjectAttributes(&objAttr, &objName, OBJ_CASE_INSENSITIVE, NULL, NULL);

        IO_STATUS_BLOCK ioStatusBlock;
        if (NT_SUCCESS(NtOpenFile(&hObject, FILE_READ_ATTRIBUTES | FILE_READ_DATA,
            &objAttr, &ioStatusBlock, FILE_SHARE_READ, FILE_OPEN_FOR_BACKUP_INTENT))) {
            std::vector<BYTE> buffer(1024 * 1024);
            ULONG returnLength = 0;

            NTSTATUS status = NtQuerySystemInformation(
                (SYSTEM_INFORMATION_CLASS)SystemExtendedHandleInformation,
                buffer.data(),
                static_cast<ULONG>(buffer.size()),
                &returnLength
            );

            if (NT_SUCCESS(status)) {
                PSYSTEM_HANDLE_INFORMATION_EX handleInfo =
                    reinterpret_cast<PSYSTEM_HANDLE_INFORMATION_EX>(buffer.data());

                for (ULONG_PTR i = 0; i < handleInfo->NumberOfHandles; i++) {
                    const auto& handle = handleInfo->Handles[i];
                    ObjectDependency dep;
                    dep.sourceObject = rootObject;
                    dep.targetObject = L"Process:" + std::to_wstring(handle.UniqueProcessId);
                    dep.dependencyType = L"Handle";
                    dependencies.push_back(dep);
                }
            }
            NtClose(hObject);
        }
    }

    return dependencies;
}

std::map<std::wstring, size_t> ObjectAnalyzer::getTypeStatistics(const std::wstring& targetDirectory) {
    std::map<std::wstring, size_t> statistics;
    HANDLE hDirectory = nullptr;
    UNICODE_STRING uniPath;
    OBJECT_ATTRIBUTES objAttributes = { sizeof(OBJECT_ATTRIBUTES) };

    // Use passed directory path
    RtlInitUnicodeString(&uniPath, targetDirectory.c_str());
    InitializeObjectAttributes(&objAttributes, &uniPath, 0, NULL, NULL);

    NTSTATUS status = NtOpenDirectoryObject(&hDirectory, DIRECTORY_QUERY, &objAttributes);
    if (NT_SUCCESS(status)) {
        const ULONG bufferSize = 8192;
        BYTE buffer[bufferSize];
        ULONG context = 0;
        ULONG returnLength;
        BOOLEAN restart = TRUE;

        while (true) {
            status = NtQueryDirectoryObject(
                hDirectory,
                buffer,
                bufferSize,
                FALSE,
                restart,
                &context,
                &returnLength
            );

            if (!NT_SUCCESS(status) || status == STATUS_NO_MORE_ENTRIES) {
                break;
            }

            POBJECT_DIRECTORY_INFORMATION dirInfo =
                reinterpret_cast<POBJECT_DIRECTORY_INFORMATION>(buffer);

            while (dirInfo->Name.Length != 0) {
                std::wstring typeName(
                    dirInfo->TypeName.Buffer,
                    dirInfo->TypeName.Length / sizeof(WCHAR)
                );
                statistics[typeName]++;
                dirInfo++;
            }

            restart = FALSE;
        }

        CloseHandle(hDirectory);
    }

    return statistics;
}
