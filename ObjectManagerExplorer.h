// ObjectManagerExplorer.h
#pragma once
#include <string>
#include <vector>
#include <windows.h>
#include <winternl.h>

class ObjectManagerExplorer {
public:
    ObjectManagerExplorer();
    ~ObjectManagerExplorer();

    void exploreNamespace(const std::wstring& path, bool recursive = false);
    void listObjects(const std::wstring& path, const std::wstring& filterType = L"", bool recursive = false);
    void displayObjectInfo(const std::wstring& objectName);

private:
    std::vector<std::pair<std::wstring, std::wstring>> getObjectNames(const std::wstring& path, const std::wstring& filterType);
    std::wstring getErrorMessage(DWORD errorCode);
    HANDLE safeOpenDirectory(const std::wstring& path);
    void logDetailedError(const std::wstring& operation, const std::wstring& path);

};