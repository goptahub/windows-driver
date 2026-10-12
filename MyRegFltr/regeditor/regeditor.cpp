#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <locale>
#include <codecvt>

// === DEFINE MISSING CONSTANTS FOR OLDER WINDOWS SDK ===
#ifndef KEY_WOW64_64_KEY
#define KEY_WOW64_64_KEY 0x0100
#endif

// === HELPER FUNCTIONS ===

// Convert string to HKEY
HKEY ParseRootKey(const std::wstring& rootKey) {
    if (rootKey == L"HKLM" || rootKey == L"HKEY_LOCAL_MACHINE") return HKEY_LOCAL_MACHINE;
    if (rootKey == L"HKCU" || rootKey == L"HKEY_CURRENT_USER") return HKEY_CURRENT_USER;
    if (rootKey == L"HKCR" || rootKey == L"HKEY_CLASSES_ROOT") return HKEY_CLASSES_ROOT;
    if (rootKey == L"HKU" || rootKey == L"HKEY_USERS") return HKEY_USERS;
    if (rootKey == L"HKCC" || rootKey == L"HKEY_CURRENT_CONFIG") return HKEY_CURRENT_CONFIG;
    return NULL;
}

// Get string representation of value type
std::wstring GetValueTypeName(DWORD type) {
    switch (type) {
    case REG_SZ:         return L"REG_SZ";
    case REG_EXPAND_SZ:  return L"REG_EXPAND_SZ";
    case REG_MULTI_SZ:   return L"REG_MULTI_SZ";
    case REG_DWORD:      return L"REG_DWORD";
    case REG_QWORD:      return L"REG_QWORD";
    case REG_BINARY:     return L"REG_BINARY";
    case REG_NONE:       return L"REG_NONE";
    default:             return L"REG_UNKNOWN";
    }
}

// Format data for output
std::wstring FormatValueData(const BYTE* data, DWORD dataSize, DWORD type) {
    std::wostringstream oss;

    switch (type) {
    case REG_SZ:
    case REG_EXPAND_SZ: {
        if (dataSize > 0) {
            const wchar_t* str = reinterpret_cast<const wchar_t*>(data);
            oss << L"\"" << str << L"\"";
        }
        break;
    }
    case REG_DWORD: {
        if (dataSize >= sizeof(DWORD)) {
            DWORD value = *reinterpret_cast<const DWORD*>(data);
            oss << value << L" (0x" << std::hex << value << L")";
        }
        break;
    }
    case REG_QWORD: {
        if (dataSize >= sizeof(ULONGLONG)) {
            ULONGLONG value = *reinterpret_cast<const ULONGLONG*>(data);
            oss << value << L" (0x" << std::hex << value << L")";
        }
        break;
    }
    case REG_BINARY: {
        oss << std::hex << std::setfill(L'0');
        for (DWORD i = 0; i < dataSize && i < 32; i++) {
            oss << std::setw(2) << (int)data[i] << L" ";
        }
        if (dataSize > 32) oss << L"...";
        break;
    }
    case REG_MULTI_SZ: {
        const wchar_t* str = reinterpret_cast<const wchar_t*>(data);
        oss << L"[";
        while (*str) {
            oss << L"\"" << str << L"\"";
            str += wcslen(str) + 1;
            if (*str) oss << L", ";
        }
        oss << L"]";
        break;
    }
    default: {
        oss << L"<binary data, " << dataSize << L" bytes>";
        break;
    }
    }

    return oss.str();
}

// === MAIN OPERATIONS ===

// Open registry key
HKEY OpenKey(
    HKEY hRootKey,
    const std::wstring& subKey,
    REGSAM samDesired = KEY_ALL_ACCESS,
    bool createIfNotExist = true
) {
    HKEY hKey = NULL;
    DWORD disposition = 0;

    LONG result;
    if (createIfNotExist) {
        result = RegCreateKeyExW(
            hRootKey,
            subKey.c_str(),
            0,
            NULL,
            REG_OPTION_NON_VOLATILE,
            samDesired,
            NULL,
            &hKey,
            &disposition
        );

        if (result == ERROR_SUCCESS) {
            std::wcout << L"[OK] " << (disposition == REG_CREATED_NEW_KEY ? L"Created" : L"Opened")
                << L" key: " << subKey << std::endl;
            return hKey;
        }
    }
    else {
        result = RegOpenKeyExW(hRootKey, subKey.c_str(), 0, samDesired, &hKey);
        if (result == ERROR_SUCCESS) {
            std::wcout << L"[OK] Opened key: " << subKey << std::endl;
            return hKey;
        }
    }

    std::wcout << L"[ERR] Failed to open key (code: " << result << L"): " << subKey << std::endl;
    return NULL;
}

// Close registry key
bool CloseKey(HKEY hKey) {
    if (hKey) {
        LONG result = RegCloseKey(hKey);
        if (result == ERROR_SUCCESS) {
            std::wcout << L"[OK] Key closed" << std::endl;
            return true;
        }
    }
    std::wcout << L"[ERR] Error closing key" << std::endl;
    return false;
}

// Delete registry key
bool DeleteKey(HKEY hRootKey, const std::wstring& subKey) {
    LONG result = RegDeleteKeyExW(
        hRootKey,
        subKey.c_str(),
        KEY_WOW64_64_KEY,
        0
    );

    if (result == ERROR_SUCCESS) {
        std::wcout << L"[OK] Key deleted: " << subKey << std::endl;
        return true;
    }

    std::wcout << L"[ERR] Failed to delete key (code: " << result << L"): " << subKey << std::endl;
    return false;
}

// Set registry value
bool SetValue(
    HKEY hKey,
    const std::wstring& valueName,
    DWORD type,
    const BYTE* data,
    DWORD dataSize
) {
    LONG result = RegSetValueExW(
        hKey,
        valueName.c_str(),
        0,
        type,
        data,
        dataSize
    );

    if (result == ERROR_SUCCESS) {
        std::wcout << L"[OK] Value set: " << valueName
            << L" [" << GetValueTypeName(type) << L"]" << std::endl;
        return true;
    }

    std::wcout << L"[ERR] Failed to set value (code: " << result << L")" << std::endl;
    return false;
}

// Get registry value
bool GetValue(
    HKEY hKey,
    const std::wstring& valueName,
    std::vector<BYTE>& data,
    DWORD& type
) {
    DWORD dataSize = 0;

    // First get the size
    LONG result = RegQueryValueExW(
        hKey,
        valueName.c_str(),
        NULL,
        &type,
        NULL,
        &dataSize
    );

    if (result != ERROR_SUCCESS) {
        if (result == ERROR_FILE_NOT_FOUND) {
            std::wcout << L"[ERR] Value not found: " << valueName << std::endl;
        }
        else {
            std::wcout << L"[ERR] Failed to read value (code: " << result << L")" << std::endl;
        }
        return false;
    }

    data.resize(dataSize);
    result = RegQueryValueExW(
        hKey,
        valueName.c_str(),
        NULL,
        &type,
        data.data(),
        &dataSize
    );

    if (result == ERROR_SUCCESS) {
        std::wcout << L"[OK] Value read: " << valueName
            << L" [" << GetValueTypeName(type) << L"]"
            << L" = " << FormatValueData(data.data(), dataSize, type) << std::endl;
        return true;
    }

    std::wcout << L"[ERR] Failed to read value (code: " << result << L")" << std::endl;
    return false;
}

// Delete registry value
bool DeleteValue(HKEY hKey, const std::wstring& valueName) {
    LONG result = RegDeleteValueW(hKey, valueName.c_str());

    if (result == ERROR_SUCCESS) {
        std::wcout << L"[OK] Value deleted: " << valueName << std::endl;
        return true;
    }

    if (result == ERROR_FILE_NOT_FOUND) {
        std::wcout << L"[ERR] Value not found: " << valueName << std::endl;
    }
    else {
        std::wcout << L"[ERR] Failed to delete value (code: " << result << L")" << std::endl;
    }
    return false;
}

// Enumerate all values in key
bool EnumValues(HKEY hKey) {
    DWORD index = 0;
    WCHAR valueName[4096];
    DWORD valueNameSize;
    DWORD type;
    BYTE data[4096];
    DWORD dataSize;

    std::wcout << L"\n=== VALUES IN KEY ===\n";

    while (true) {
        valueNameSize = sizeof(valueName) / sizeof(WCHAR);
        dataSize = sizeof(data);

        LONG result = RegEnumValueW(
            hKey,
            index,
            valueName,
            &valueNameSize,
            NULL,
            &type,
            data,
            &dataSize
        );

        if (result == ERROR_NO_MORE_ITEMS) break;
        if (result != ERROR_SUCCESS) {
            std::wcout << L"[ERR] Enumeration error (code: " << result << L")" << std::endl;
            return false;
        }

        std::wcout << L"  " << index << L". " << valueName
            << L" [" << GetValueTypeName(type) << L"]"
            << L" = " << FormatValueData(data, dataSize, type) << std::endl;
        index++;
    }

    if (index == 0) {
        std::wcout << L"  (no values)" << std::endl;
    }

    return true;
}

// === COMMAND LINE PARSER ===

struct Command {
    enum Type {
        CMD_OPEN,
        CMD_CLOSE,
        CMD_DELETE_KEY,
        CMD_SET_SZ,
        CMD_SET_DWORD,
        CMD_SET_QWORD,
        CMD_GET,
        CMD_DELETE_VALUE,
        CMD_ENUM,
        CMD_HELP,
        CMD_EXIT
    };

    Type type;
    std::wstring rootKey;
    std::wstring subKey;
    std::wstring valueName;
    std::wstring stringData;
    DWORD dwordData;
    ULONGLONG qwordData;
};

Command ParseCommand(const std::wstring& line) {
    Command cmd;
    cmd.type = Command::CMD_HELP;
    cmd.dwordData = 0;
    cmd.qwordData = 0;

    std::wistringstream iss(line);
    std::wstring command;
    iss >> command;

    if (command.empty()) return cmd;

    if (command == L"open" || command == L"o") {
        cmd.type = Command::CMD_OPEN;
        iss >> cmd.rootKey >> cmd.subKey;
    }
    else if (command == L"close" || command == L"c") {
        cmd.type = Command::CMD_CLOSE;
    }
    else if (command == L"deletekey" || command == L"dk") {
        cmd.type = Command::CMD_DELETE_KEY;
        iss >> cmd.rootKey >> cmd.subKey;
    }
    else if (command == L"setsz" || command == L"ss") {
        cmd.type = Command::CMD_SET_SZ;
        iss >> cmd.rootKey >> cmd.subKey >> cmd.valueName;
        std::getline(iss, cmd.stringData);
        // Remove leading space
        if (!cmd.stringData.empty() && cmd.stringData[0] == L' ') {
            cmd.stringData = cmd.stringData.substr(1);
        }
    }
    else if (command == L"setdword" || command == L"sd") {
        cmd.type = Command::CMD_SET_DWORD;
        iss >> cmd.rootKey >> cmd.subKey >> cmd.valueName >> cmd.dwordData;
    }
    else if (command == L"setqword" || command == L"sq") {
        cmd.type = Command::CMD_SET_QWORD;
        iss >> cmd.rootKey >> cmd.subKey >> cmd.valueName >> cmd.qwordData;
    }
    else if (command == L"get" || command == L"g") {
        cmd.type = Command::CMD_GET;
        iss >> cmd.rootKey >> cmd.subKey >> cmd.valueName;
    }
    else if (command == L"deletevalue" || command == L"dv") {
        cmd.type = Command::CMD_DELETE_VALUE;
        iss >> cmd.rootKey >> cmd.subKey >> cmd.valueName;
    }
    else if (command == L"enum" || command == L"e") {
        cmd.type = Command::CMD_ENUM;
        iss >> cmd.rootKey >> cmd.subKey;
    }
    else if (command == L"help" || command == L"h" || command == L"?") {
        cmd.type = Command::CMD_HELP;
    }
    else if (command == L"exit" || command == L"q") {
        cmd.type = Command::CMD_EXIT;
    }

    return cmd;
}

void PrintHelp() {
    std::wcout << L"\n=== COMMANDS ===\n"
        << L"  open HKEY KEY           - Open/create a key\n"
        << L"  close                   - Close current key\n"
        << L"  deletekey HKEY KEY      - Delete a key\n"
        << L"  setsz HKEY KEY NAME TEXT - Set a string value\n"
        << L"  setdword HKEY KEY NAME NUM - Set a DWORD value\n"
        << L"  setqword HKEY KEY NAME NUM - Set a QWORD value\n"
        << L"  get HKEY KEY NAME       - Get a value\n"
        << L"  deletevalue HKEY KEY NAME - Delete a value\n"
        << L"  enum HKEY KEY           - Enumerate all values\n"
        << L"  help                    - Show help\n"
        << L"  exit                    - Exit\n"
        << L"\nExamples:\n"
        << L"  open HKCU Software\\MyApp\n"
        << L"  setsz HKCU Software\\MyApp MyValue Hello World\n"
        << L"  get HKCU Software\\MyApp MyValue\n"
        << L"  deletevalue HKCU Software\\MyApp MyValue\n"
        << L"  enum HKCU Software\\MyApp\n"
        << L"  deletekey HKCU Software\\MyApp\n"
        << L"  (shortcuts: o, ss, sd, g, dv, e, dk, q)\n"
        << std::endl;
}

// === MAIN FUNCTION ===

int main() {
    SetConsoleOutputCP(CP_UTF8);
    std::wcout << L"=== REGISTRY MANAGER ===\n"
        << L"Type help for command list\n\n";

    HKEY hCurrentKey = NULL;
    std::wstring currentRootKey;
    std::wstring currentSubKey;

    while (true) {
        // Display current key
        if (hCurrentKey) {
            std::wcout << L"[" << currentRootKey << L"\\" << currentSubKey << L"]> ";
        }
        else {
            std::wcout << L"(no key)> ";
        }

        std::wstring input;
        std::getline(std::wcin, input);

        if (input.empty()) continue;

        Command cmd = ParseCommand(input);

        switch (cmd.type) {
        case Command::CMD_OPEN: {
            if (cmd.rootKey.empty() || cmd.subKey.empty()) {
                std::wcout << L"[ERR] Usage: open HKEY KEY" << std::endl;
                break;
            }

            HKEY hRoot = ParseRootKey(cmd.rootKey);
            if (!hRoot) {
                std::wcout << L"[ERR] Unknown root key: " << cmd.rootKey << std::endl;
                break;
            }

            // Close previous key
            if (hCurrentKey) {
                RegCloseKey(hCurrentKey);
                hCurrentKey = NULL;
            }

            hCurrentKey = OpenKey(hRoot, cmd.subKey);
            if (hCurrentKey) {
                currentRootKey = cmd.rootKey;
                currentSubKey = cmd.subKey;
            }
            break;
        }

        case Command::CMD_CLOSE: {
            if (hCurrentKey) {
                CloseKey(hCurrentKey);
                hCurrentKey = NULL;
                currentRootKey.clear();
                currentSubKey.clear();
            }
            else {
                std::wcout << L"[ERR] No open key" << std::endl;
            }
            break;
        }

        case Command::CMD_DELETE_KEY: {
            if (cmd.rootKey.empty() || cmd.subKey.empty()) {
                std::wcout << L"[ERR] Usage: deletekey HKEY KEY" << std::endl;
                break;
            }

            HKEY hRoot = ParseRootKey(cmd.rootKey);
            if (!hRoot) {
                std::wcout << L"[ERR] Unknown root key: " << cmd.rootKey << std::endl;
                break;
            }

            // If deleting current key, close it
            if (hCurrentKey && currentRootKey == cmd.rootKey && currentSubKey == cmd.subKey) {
                RegCloseKey(hCurrentKey);
                hCurrentKey = NULL;
                currentRootKey.clear();
                currentSubKey.clear();
            }

            DeleteKey(hRoot, cmd.subKey);
            break;
        }

        case Command::CMD_SET_SZ: {
            if (!hCurrentKey) {
                std::wcout << L"[ERR] No open key" << std::endl;
                break;
            }
            if (cmd.valueName.empty()) {
                std::wcout << L"[ERR] Specify value name" << std::endl;
                break;
            }
            if (cmd.stringData.empty()) {
                std::wcout << L"[ERR] Specify data" << std::endl;
                break;
            }

            DWORD dataSize = (DWORD)((cmd.stringData.length() + 1) * sizeof(wchar_t));
            SetValue(hCurrentKey, cmd.valueName, REG_SZ,
                reinterpret_cast<const BYTE*>(cmd.stringData.c_str()), dataSize);
            break;
        }

        case Command::CMD_SET_DWORD: {
            if (!hCurrentKey) {
                std::wcout << L"[ERR] No open key" << std::endl;
                break;
            }
            if (cmd.valueName.empty()) {
                std::wcout << L"[ERR] Specify value name" << std::endl;
                break;
            }

            SetValue(hCurrentKey, cmd.valueName, REG_DWORD,
                reinterpret_cast<const BYTE*>(&cmd.dwordData), sizeof(DWORD));
            break;
        }

        case Command::CMD_SET_QWORD: {
            if (!hCurrentKey) {
                std::wcout << L"[ERR] No open key" << std::endl;
                break;
            }
            if (cmd.valueName.empty()) {
                std::wcout << L"[ERR] Specify value name" << std::endl;
                break;
            }

            SetValue(hCurrentKey, cmd.valueName, REG_QWORD,
                reinterpret_cast<const BYTE*>(&cmd.qwordData), sizeof(ULONGLONG));
            break;
        }

        case Command::CMD_GET: {
            if (!hCurrentKey) {
                std::wcout << L"[ERR] No open key" << std::endl;
                break;
            }
            if (cmd.valueName.empty()) {
                std::wcout << L"[ERR] Specify value name" << std::endl;
                break;
            }

            std::vector<BYTE> data;
            DWORD type;
            GetValue(hCurrentKey, cmd.valueName, data, type);
            break;
        }

        case Command::CMD_DELETE_VALUE: {
            if (!hCurrentKey) {
                std::wcout << L"[ERR] No open key" << std::endl;
                break;
            }
            if (cmd.valueName.empty()) {
                std::wcout << L"[ERR] Specify value name" << std::endl;
                break;
            }

            DeleteValue(hCurrentKey, cmd.valueName);
            break;
        }

        case Command::CMD_ENUM: {
            if (!hCurrentKey) {
                std::wcout << L"[ERR] No open key" << std::endl;
                break;
            }

            EnumValues(hCurrentKey);
            break;
        }

        case Command::CMD_HELP: {
            PrintHelp();
            break;
        }

        case Command::CMD_EXIT: {
            if (hCurrentKey) {
                RegCloseKey(hCurrentKey);
            }
            std::wcout << L"Exiting..." << std::endl;
            return 0;
        }
        }
    }

    return 0;
}