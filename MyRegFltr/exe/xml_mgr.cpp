#include "xml_mgr.h"
#include <iostream>
#include <vector>

XmlRegistryManager::XmlRegistryManager(HKEY hKeyRoot, const std::wstring& subKey, const std::wstring& valueName)
    : m_hKeyRoot(hKeyRoot), m_subKey(subKey), m_valueName(valueName) {
}

bool XmlRegistryManager::AddEntry(EntryType type, const std::string& nameOrPath, int privilege) {
    std::string xmlData;
    if (!ReadFromRegistry(xmlData)) {
        xmlData = "<config>\n  <processes/>\n  <registry_keys/>\n</config>";
    }

    tinyxml2::XMLDocument doc;
    if (doc.Parse(xmlData.c_str()) != tinyxml2::XML_SUCCESS) {
        std::cerr << "[-] Error parsing XML from registry.\n";
        return false;
    }

    tinyxml2::XMLElement* root = doc.RootElement();
    if (!root) {
        std::cerr << "[-] Root element not found.\n";
        return false;
    }

    const char* parentName = (type == EntryType::Process) ? "processes" : "registry_keys";
    tinyxml2::XMLElement* parent = root->FirstChildElement(parentName);
    if (!parent) {
        parent = doc.NewElement(parentName);
        root->InsertEndChild(parent);
    }

    const char* childName = (type == EntryType::Process) ? "process" : "key";
    tinyxml2::XMLElement* newElem = doc.NewElement(childName);
    newElem->SetAttribute((type == EntryType::Process) ? "name" : "path", nameOrPath.c_str());
    newElem->SetAttribute("privilege", privilege);

    parent->InsertEndChild(newElem);

    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    std::string newXml = printer.CStr();

    return WriteToRegistry(newXml);
}

bool XmlRegistryManager::ReadFromRegistry(std::string& outData) {
    HKEY hKey;
    LONG result = RegOpenKeyExW(m_hKeyRoot, m_subKey.c_str(), 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD size = 0;
    result = RegQueryValueExW(hKey, m_valueName.c_str(), nullptr, &type, 
        nullptr, &size);

    if (result != ERROR_SUCCESS || type != REG_SZ) {
        RegCloseKey(hKey);
        return false;
    }

    std::vector<wchar_t> buffer(size / sizeof(wchar_t));
    result = RegQueryValueExW(hKey, m_valueName.c_str(), nullptr, &type,
        reinterpret_cast<LPBYTE>(buffer.data()), &size);

    if (result == ERROR_SUCCESS) {
        int len = WideCharToMultiByte(CP_UTF8, 0, buffer.data(), -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::vector<char> utf8(len);
            WideCharToMultiByte(CP_UTF8, 0, buffer.data(), -1, utf8.data(), len, nullptr, nullptr);
            outData = utf8.data();
        }
    }
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

bool XmlRegistryManager::WriteToRegistry(const std::string& xmlData) {
    HKEY hKey;
    LONG result = RegCreateKeyExW(m_hKeyRoot, m_subKey.c_str(), 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    
    if (result != ERROR_SUCCESS) {
        std::cerr << "Failed to create/open registry key. Error: " << result << "\n";
        return false;
    }

    int len = MultiByteToWideChar(CP_UTF8, 0, xmlData.c_str(), -1, nullptr, 0);
    if (len <= 0) {
        RegCloseKey(hKey);
        return false;
    }
    std::vector<wchar_t> wide(len);
    MultiByteToWideChar(CP_UTF8, 0, xmlData.c_str(), -1, wide.data(), len);

    //std::cout << xmlData << std::endl;
    //std::cout << len << std::endl;
    //std::wcout << wide.data() << std::endl;
    //std::cout << wide.size() << std::endl;


    result = RegSetValueExW(hKey, m_valueName.c_str(), 0, REG_SZ,
        reinterpret_cast<const BYTE*>(wide.data()),
        static_cast<DWORD>(len * sizeof(wchar_t))
    );
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}


bool XmlRegistryManager::PrintRawXml() {
    std::string xmlData;
    if (!ReadFromRegistry(xmlData)) {
        std::cerr << "[-] No XML data found in registry.\n";
        return false;
    }
    std::cout << xmlData << std::endl;
    return true;
}