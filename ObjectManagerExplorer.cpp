#include "ObjectManagerExplorer.h"
#include <iostream>
#include <vector>
#include <shlobj.h> 
#include <thread>
#pragma comment(lib, "shell32.lib")

#pragma comment(lib, "ntdll.lib")

extern "C" NTSTATUS NTAPI RtlGetLastNtStatus(void);

// Structure definitions
typedef struct _OBJECT_BASIC_INFORMATION {
    ULONG Attributes;
    ACCESS_MASK DesiredAccess;
    ULONG HandleCount;
    ULONG PointerCount;
    ULONG PagedPoolUsage;
    ULONG NonPagedPoolUsage;
    ULONG Reserved[3];
    ULONG NameInformationLength;
    ULONG TypeInformationLength;
    ULONG SecurityDescriptorLength;
    LARGE_INTEGER CreationTime;
} OBJECT_BASIC_INFORMATION, * POBJECT_BASIC_INFORMATION;

typedef struct _OBJECT_DIRECTORY_INFORMATION {
    UNICODE_STRING Name;
    UNICODE_STRING TypeName;
} OBJECT_DIRECTORY_INFORMATION, * POBJECT_DIRECTORY_INFORMATION;

// NT API function declarations
extern "C" {
    NTSTATUS NTAPI NtOpenDirectoryObject(
        OUT PHANDLE DirectoryHandle,
        IN ACCESS_MASK DesiredAccess,
        IN POBJECT_ATTRIBUTES ObjectAttributes
    );

    NTSTATUS NTAPI NtQueryDirectoryObject(
        IN HANDLE DirectoryHandle,
        OUT PVOID Buffer,
        IN ULONG Length,
        IN BOOLEAN ReturnSingleEntry,
        IN BOOLEAN RestartScan,
        IN OUT PULONG Context,
        OUT PULONG ReturnLength OPTIONAL
    );

    NTSTATUS NTAPI NtOpenEvent(
        OUT PHANDLE EventHandle,
        IN ACCESS_MASK DesiredAccess,
        IN POBJECT_ATTRIBUTES ObjectAttributes
    );

    NTSTATUS NTAPI NtQueryObject(
        IN HANDLE Handle OPTIONAL,
        IN OBJECT_INFORMATION_CLASS ObjectInformationClass,
        OUT PVOID ObjectInformation OPTIONAL,
        IN ULONG ObjectInformationLength,
        OUT PULONG ReturnLength OPTIONAL
    );
}

#define DIRECTORY_QUERY 0x0001
#define STATUS_NO_MORE_ENTRIES ((NTSTATUS)0x8000001A)
#define EVENT_QUERY_STATE 0x0001

ObjectManagerExplorer::ObjectManagerExplorer() {}
ObjectManagerExplorer::~ObjectManagerExplorer() {}

std::wstring ObjectManagerExplorer::getErrorMessage(DWORD errorCode) {
    LPWSTR errorText = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&errorText,
        0,
        nullptr
    );

    std::wstring errorMsg = errorText ? errorText : L"Unknown error";
    LocalFree(errorText);
    return errorMsg;
}

typedef BOOL(WINAPI* LPFN_ISUSERANADMIN)(void);
BOOL IsUserAnAdminWrapper() {
    LPFN_ISUSERANADMIN lpfnIsUserAnAdmin = (LPFN_ISUSERANADMIN)GetProcAddress(
        GetModuleHandle(L"shell32.dll"),
        "IsUserAnAdmin");

    if (lpfnIsUserAnAdmin == NULL)
        return FALSE;

    return lpfnIsUserAnAdmin();
}


void ObjectManagerExplorer::logDetailedError(const std::wstring& operation, const std::wstring& path) {
    DWORD error = GetLastError();
    LPWSTR messageBuffer = nullptr;

    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&messageBuffer,
        0, NULL
    );

    std::wcerr << L"Operation: " << operation
        << L"\nPath: " << path
        << L"\nError Code: " << error
        << L"\nNtStatus: 0x" << std::hex << RtlGetLastNtStatus()
        << L"\nDetailed Error: " << (messageBuffer ? messageBuffer : L"Unknown error")
        << L"\nProcess Privileges: " << (IsUserAnAdmin() ? L"Admin" : L"Non-Admin")
        << std::endl;

    if (messageBuffer) {
        LocalFree(messageBuffer);
    }
}



HANDLE ObjectManagerExplorer::safeOpenDirectory(const std::wstring& path) {
    HANDLE hDirectory = nullptr;
    OBJECT_ATTRIBUTES objAttributes = { sizeof(OBJECT_ATTRIBUTES) };
    UNICODE_STRING uniPath;

    // Normalize path
    std::wstring normalizedPath = path;
    if (!normalizedPath.empty() && normalizedPath.back() == L'\\') {
        normalizedPath.pop_back();
    }

    RtlInitUnicodeString(&uniPath, normalizedPath.c_str());
    InitializeObjectAttributes(&objAttributes, &uniPath, OBJ_CASE_INSENSITIVE, NULL, NULL);

    NTSTATUS status = NtOpenDirectoryObject(&hDirectory, DIRECTORY_QUERY, &objAttributes);

    if (!NT_SUCCESS(status)) {
        logDetailedError(L"NtOpenDirectoryObject", normalizedPath);
        SetLastError(RtlNtStatusToDosError(status));
        return nullptr;
    }

    return hDirectory;
}


void ObjectManagerExplorer::exploreNamespace(const std::wstring& path, bool recursive) {
    std::wcout << L"Exploring namespace at: " << path.c_str() << std::endl;
    listObjects(path, L"", recursive);
}

void ObjectManagerExplorer::listObjects(const std::wstring& path, const std::wstring& filterType, bool recursive) {
    HANDLE hDirectory = nullptr;
    OBJECT_ATTRIBUTES objAttributes = { sizeof(OBJECT_ATTRIBUTES) };
    UNICODE_STRING uniPath;

    try {
        RtlInitUnicodeString(&uniPath, path.c_str());
        InitializeObjectAttributes(&objAttributes, &uniPath, OBJ_CASE_INSENSITIVE, NULL, NULL);

        NTSTATUS status = NtOpenDirectoryObject(&hDirectory, DIRECTORY_QUERY, &objAttributes);
        if (!NT_SUCCESS(status)) {
            std::wcerr << L"Failed to open directory: " << path << std::endl;
            return;
        }

        std::vector<BYTE> buffer(8192);
        ULONG context = 0;
        ULONG returnLength;
        BOOLEAN restart = TRUE;

        while (true) {
            status = NtQueryDirectoryObject(
                hDirectory,
                buffer.data(),
                static_cast<ULONG>(buffer.size()),
                FALSE,
                restart,
                &context,
                &returnLength
            );

            if (status == STATUS_NO_MORE_ENTRIES) {
                break;
            }

            POBJECT_DIRECTORY_INFORMATION dirInfo = reinterpret_cast<POBJECT_DIRECTORY_INFORMATION>(buffer.data());

            while (dirInfo->Name.Length > 0) {
                std::wstring objName(dirInfo->Name.Buffer, dirInfo->Name.Length / sizeof(WCHAR));
                std::wstring objType(dirInfo->TypeName.Buffer, dirInfo->TypeName.Length / sizeof(WCHAR));

                if (filterType.empty() || objType == filterType) {
                    std::wstring fullPath = path;
                    if (fullPath.back() != L'\\') fullPath += L'\\';
                    fullPath += objName;

                    // Skip problematic objects containing certain characters
                    if (objName.find(L":") == std::wstring::npos &&
                        objName.find(L"/") == std::wstring::npos &&
                        objName.find(L"\\") == std::wstring::npos &&
                        objName.length() < 260) { // MAX_PATH

                        std::wcout << L"Object: " << fullPath << L", Type: " << objType << std::endl;
                    }
                }

                dirInfo++;
            }

            restart = FALSE;
        }

    }
    catch (...) {
        std::wcerr << L"Error processing objects" << std::endl;
    }

    if (hDirectory) {
        NtClose(hDirectory);
    }
}

void ObjectManagerExplorer::displayObjectInfo(const std::wstring& objectName) {
    HANDLE objectHandle;
    UNICODE_STRING unicodeObjectName;
    OBJECT_ATTRIBUTES objAttributes;

    RtlInitUnicodeString(&unicodeObjectName, objectName.c_str());
    InitializeObjectAttributes(&objAttributes, &unicodeObjectName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    NTSTATUS status = NtOpenEvent(&objectHandle, EVENT_QUERY_STATE, &objAttributes);
    if (!NT_SUCCESS(status)) {
        std::wcerr << L"Failed to open object: " << objectName.c_str() << std::endl;
        return;
    }

    OBJECT_BASIC_INFORMATION objBasicInfo;
    ULONG returnLength;
    status = NtQueryObject(
        objectHandle,
        ObjectBasicInformation,
        &objBasicInfo,
        sizeof(objBasicInfo),
        &returnLength
    );

    if (!NT_SUCCESS(status)) {
        std::wcerr << L"Failed to query object information." << std::endl;
        NtClose(objectHandle);
        return;
    }

    std::wcout << L"Object Information for: " << objectName.c_str() << std::endl
        << L"  Handle Count: " << objBasicInfo.HandleCount << std::endl
        << L"  Pointer Count: " << objBasicInfo.PointerCount << std::endl
        << L"  Paged Pool Usage: " << objBasicInfo.PagedPoolUsage << std::endl
        << L"  Non-Paged Pool Usage: " << objBasicInfo.NonPagedPoolUsage << std::endl;

    NtClose(objectHandle);
}

std::vector<std::pair<std::wstring, std::wstring>> ObjectManagerExplorer::getObjectNames(
    const std::wstring& path,
    const std::wstring& filterType
) {
    std::vector<std::pair<std::wstring, std::wstring>> objectNames;

    HANDLE dirHandle;
    UNICODE_STRING unicodePath;
    OBJECT_ATTRIBUTES objAttributes;

    RtlInitUnicodeString(&unicodePath, path.c_str());
    InitializeObjectAttributes(&objAttributes, &unicodePath, OBJ_CASE_INSENSITIVE, NULL, NULL);

    NTSTATUS status = NtOpenDirectoryObject(&dirHandle, DIRECTORY_QUERY, &objAttributes);
    if (!NT_SUCCESS(status)) {
        std::wcerr << L"Failed to open directory: " << path.c_str() << std::endl;
        return objectNames;
    }

    BYTE buffer[1024];
    ULONG context = 0;
    ULONG returnLength = 0;
    BOOLEAN restart = TRUE;

    while (true) {
        status = NtQueryDirectoryObject(
            dirHandle,
            buffer,
            sizeof(buffer),
            FALSE,
            restart,
            &context,
            &returnLength
        );

        restart = FALSE;

        if (status == STATUS_NO_MORE_ENTRIES) {
            break;
        }

        if (!NT_SUCCESS(status)) {
            std::wcerr << L"Failed to query directory object." << std::endl;
            break;
        }

        POBJECT_DIRECTORY_INFORMATION dirInfo = reinterpret_cast<POBJECT_DIRECTORY_INFORMATION>(buffer);
        while (dirInfo->Name.Length > 0) {
            std::wstring typeName(dirInfo->TypeName.Buffer, dirInfo->TypeName.Length / sizeof(wchar_t));
            std::wstring objectName(dirInfo->Name.Buffer, dirInfo->Name.Length / sizeof(wchar_t));

            if (filterType.empty() || typeName == filterType) {
                objectNames.emplace_back(objectName, typeName);
            }

            dirInfo++;
        }
    }

    NtClose(dirHandle);
    return objectNames;
}