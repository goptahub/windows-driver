#pragma once

#include <windows.h>
#include <string>
#include "tinyxml2.h"

enum class EntryType {
    Process,
    RegistryKey
};

class XmlRegistryManager {
public:
    XmlRegistryManager(HKEY hKeyRoot, const std::wstring& subKey, const std::wstring& valueName);
    bool AddEntry(EntryType type, const std::string& nameOrPath, int privilege);
    bool PrintRawXml();

private:
    HKEY m_hKeyRoot;
    std::wstring m_subKey;
    std::wstring m_valueName;

    bool ReadFromRegistry(std::string& outData);
    bool WriteToRegistry(const std::string& xmlData);
};