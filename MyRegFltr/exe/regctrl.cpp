#include <windows.h>
#include <iostream>
#include <string>
#include "common.h"
#include "regctrl.h"
#include "xml_mgr.h"

#define ROOT        HKEY_LOCAL_MACHINE
#define SUBKEY      L"SOFTWARE\\TestDriver"
#define VALUENAME   L"DriverRules"

void PrintUsage() {
    std::cout
        << "Usage:\n"
        << "  regctrl.exe add <process|key> <name> <privilege>\n"
        << "  regctrl.exe update\n"
        << "  regctrl.exe del\n"
        << "  regctrl.exe show\n"
        << "  regctrl.exe reg\n"
        << "  regctrl.exe unreg\n\n"
        << "Commands:\n"
        << "  add    - add a rule to the XML stored in the registry\n"
        << "           privilege: 1..4\n"
        << "  update - notify the driver to reload rules\n"
        << "  del    - delete the registry value containing configuration\n"
        << "  show   - show configuration from registry\n"
        << "  reg    - register callback on thread creating\n"
        << "  unreg  - unregister callback on thread creating\n\n";
        
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    std::wstring command = argv[1];

    if (command == L"add") {

        if (argc != 5) {
            std::cerr << "[-] Error: 'add' requires 3 arguments.\n";
            PrintUsage();
            return 1;
        }

        std::wstring typeW = argv[2];
        std::wstring nameW = argv[3];
        std::wstring privilegeW = argv[4];


        int len = WideCharToMultiByte(CP_UTF8, 0, typeW.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) {
            std::cerr << "[-] Error converting type string.\n";
            return 1;
        }
        std::string typeStr(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, typeW.c_str(), -1, &typeStr[0], len, nullptr, nullptr);
        typeStr.pop_back();

        len = WideCharToMultiByte(CP_UTF8, 0, nameW.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) {
            std::cerr << "[-] Error converting name string.\n";
            return 1;
        }
        std::string nameStr(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, nameW.c_str(), -1, &nameStr[0], len, nullptr, nullptr);
        nameStr.pop_back();

        int privilege = _wtoi(privilegeW.c_str());
        if (privilege < 1 || privilege > 5) {
            std::cerr << "[-] Error: privilege must be between 1 and 5.\n";
            return 1;
        }

        EntryType type;
        if (typeStr == "process") {
            type = EntryType::Process;
        }
        else if (typeStr == "key") {
            type = EntryType::RegistryKey;
        }
        else {
            std::cerr << "[-] Error: type must be 'process' or 'key'.\n";
            return 1;
        }

        //HKEY root = ROOT;
        //std::wstring subKey = SUBKEY;
        //std::wstring valueName = VALUENAME;

        XmlRegistryManager manager(ROOT, SUBKEY, VALUENAME);
        if (manager.AddEntry(type, nameStr, privilege)) {
            std::cout << "[+] Entry successfully added to XML and saved to registry.\n";
            return 0;
        }
        else {
            std::cerr << "[-] Failed to add entry.\n";
            return 1;
        }
    }
    else if (command == L"update") {

        HANDLE hDevice = CreateFileW(WIN32_DEVICE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hDevice == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to open driver device. Error: " << GetLastError()
                << "\nMake sure the driver is loaded and accessible.\n";
            return 1;
        }

        DWORD bytesReturned = 0;
        INT output = -1;

        BOOL result = DeviceIoControl(hDevice,
            IOCTL_RULES_UPDATE,
            nullptr, 0,
            &output, sizeof(output),
            &bytesReturned,
            nullptr);
        CloseHandle(hDevice);

        if (!result) {
            std::cerr << "DeviceIoControl failed. Error: " << GetLastError() << "\n";
            return 1;
        }

        switch (output) {
        case 0:
            std::cout << "Update rules in driver failed.\n";
            return 1;
        case 1:
            std::cout << "Update rules in driver succeeded.\n";
            return 0;
        default:
            std::cout << "Driver sent no answer (returned value: " << output << " ).\n";
            return 1;
        }
    }
    else if (command == L"reg") {

        HANDLE hDevice = CreateFileW(WIN32_DEVICE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hDevice == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to open driver device. Error: " << GetLastError()
                << "\nMake sure the driver is loaded and accessible.\n";
            return 1;
        }

        DWORD bytesReturned = 0;
        INT output = -1;

        BOOL result = DeviceIoControl(hDevice,
            IOCTL_REGISTER_CALLBACK_ON_THREAD_CREATING,
            nullptr, 0,
            &output, sizeof(output),
            &bytesReturned,
            nullptr);
        CloseHandle(hDevice);

        if (!result) {
            std::cerr << "DeviceIoControl failed. Error: " << GetLastError() << "\n";
            return 1;
        }

        switch (output) {
        case 0:
            std::cout << "reg failed.\n";
            return 1;
        case 1:
            std::cout << "reg succeeded.\n";
            return 0;
        default:
            std::cout << "Driver sent no answer (returned value: " << output << " ).\n";
            return 1;
        }
    }
    else if (command == L"unreg") {

        HANDLE hDevice = CreateFileW(WIN32_DEVICE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hDevice == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to open driver device. Error: " << GetLastError()
                << "\nMake sure the driver is loaded and accessible.\n";
            return 1;
        }

        DWORD bytesReturned = 0;
        INT output = -1;

        BOOL result = DeviceIoControl(hDevice,
            IOCTL_UNREGISTER_CALLBACK_ON_THREAD_CREATING,
            nullptr, 0,
            &output, sizeof(output),
            &bytesReturned,
            nullptr);
        CloseHandle(hDevice);

        if (!result) {
            std::cerr << "DeviceIoControl failed. Error: " << GetLastError() << "\n";
            return 1;
        }

        switch (output) {
        case 0:
            std::cout << "unreg failed.\n";
            return 1;
        case 1:
            std::cout << "unreg succeeded.\n";
            return 0;
        default:
            std::cout << "Driver sent no answer (returned value: " << output << " ).\n";
            return 1;
        }
    }
    else if (command == L"show") {
        XmlRegistryManager manager(ROOT, SUBKEY, VALUENAME);
        if (manager.PrintRawXml())
            return 0;
        else
            return 1;
    }
    else if (command == L"del") {
        // Delete the registry value
        HKEY hKey;
        LONG openResult = RegOpenKeyExW(ROOT, SUBKEY, 0, KEY_SET_VALUE, &hKey);

        if (openResult != ERROR_SUCCESS) {
            std::cerr << "[-] Failed to open registry key. Error: " << openResult << "\n";
            return 1;
        }

        LONG deleteResult = RegDeleteValueW(hKey, VALUENAME);
        RegCloseKey(hKey);

        if (deleteResult == ERROR_SUCCESS) {
            std::cout << "[+] Registry value successfully deleted.\n";
            return 0;
        }
        else if (deleteResult == ERROR_FILE_NOT_FOUND) {
            std::cout << "[+] Registry value does not exist (nothing to delete).\n";
            return 0;
        }
        else {
            std::cerr << "[-] Failed to delete registry value. Error: " << deleteResult << "\n";
            return 1;
        }
    }
    else {
        std::cerr << "[-] Unknown command: " << command.c_str() << "\n";
        PrintUsage();
        return 1;
    }
}