#pragma once
#include <windows.h>
#include <winternl.h>
#include <string>
#include <vector>
#include <map>
#include <functional>

typedef enum _OBJECT_INFO_CLASS {
    ObjectNameInfo = 1,
    ObjectTypeInfo = 2
} OBJECT_INFO_CLASS;

struct HandleInfo {
    DWORD processId;
    DWORD handleValue;
    std::wstring objectType;
    std::wstring objectName;
};

struct ObjectDependency {
    std::wstring sourceObject;
    std::wstring targetObject;
    std::wstring dependencyType;
};

using AnalysisCallback = std::function<void(
    const std::wstring& objectName,
    const std::wstring& objectType,
    ULONG handleCount,
    ULONG referenceCount,
    const std::vector<std::wstring>& linkedObjects,
    ACCESS_MASK accessMask
    )>;

class ObjectAnalyzer {
public:
    ObjectAnalyzer();
    ~ObjectAnalyzer();

    std::vector<ObjectDependency> buildDependencyGraph(const std::wstring& rootObject);
    std::map<std::wstring, size_t> getTypeStatistics(const std::wstring& targetDirectory);

    void setAnalysisCallback(AnalysisCallback callback) {
        analysisCallback = callback;
    }
    void analyzeObjectRelations(const std::wstring& objectName);

private:
    AnalysisCallback analysisCallback;
};
