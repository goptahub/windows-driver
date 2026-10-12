// user_registry_test.c
// User-mode registry test for TestDriver key with DWORD values

#include <stdbool.h>
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

#ifndef KEY_WOW64_64_KEY
#define KEY_WOW64_64_KEY 0x0100
#endif

// Registry path (single key)
#define TEST_REG_KEY L"SOFTWARE\\TestDriver"

// DWORD value names stored IN the key (not subkeys)
#define VALUE_PARAM1 L"param1"
#define VALUE_PARAM2 L"param2"
#define VALUE_PARAM3 L"param3"

#define MAX_VALUE_NAME 16383

// Helper function: print error message
void PrintError(const char* msg, DWORD error) {
    printf("[ERR] %s (error code: %lu)\n", msg, error);
}

// Helper function: print separator line
void PrintSeparator() {
    printf("============================================================\n");
}

//
// Function: OpenRegistryKey
// Description: Opens or creates a registry key
// Parameters:
//   - rootKey: Root registry key (HKEY_LOCAL_MACHINE, etc.)
//   - path: Subkey path
//   - createIfNotExist: If TRUE, create key if it doesn't exist
// Returns: Handle to the key, or NULL on failure
//
HKEY OpenRegistryKey(
    HKEY rootKey,
    const wchar_t* path,
    bool createIfNotExist
) {
    HKEY hKey = NULL;
    DWORD disposition = 0;

    if (createIfNotExist) {
        LONG result = RegCreateKeyExW(
            rootKey,
            path,
            0,
            NULL,
            REG_OPTION_NON_VOLATILE,
            KEY_ALL_ACCESS,
            NULL,
            &hKey,
            &disposition
        );

        if (result == ERROR_SUCCESS) {
            if (disposition == REG_CREATED_NEW_KEY) {
                printf("[OK] Created key: %S\n", path);
            }
            else {
                printf("[OK] Opened key: %S\n", path);
            }
            return hKey;
        }
        else {
            PrintError("Failed to create/open key", result);
            return NULL;
        }
    }
    else {
        LONG result = RegOpenKeyExW(
            rootKey,
            path,
            0,
            KEY_ALL_ACCESS,
            &hKey
        );

        if (result == ERROR_SUCCESS) {
            printf("[OK] Opened key: %S\n", path);
            return hKey;
        }
        else {
            PrintError("Failed to open key", result);
            return NULL;
        }
    }
}

//
// Function: CloseRegistryKey
// Description: Closes a registry key handle
// Parameters:
//   - hKey: Handle to the key to close
// Returns: TRUE on success, FALSE on failure
//
bool CloseRegistryKey(HKEY hKey) {
    if (hKey) {
        LONG result = RegCloseKey(hKey);
        if (result == ERROR_SUCCESS) {
            printf("[OK] Key closed\n");
            return true;
        }
        else {
            PrintError("Failed to close key", result);
            return false;
        }
    }
    return false;
}

//
// Function: SetDWORDValue
// Description: Sets a DWORD value in the registry key
// Parameters:
//   - hKey: Handle to the open registry key
//   - valueName: Name of the value to set (e.g., "param1")
//   - data: DWORD data to store
// Returns: TRUE on success, FALSE on failure
//
bool SetDWORDValue(HKEY hKey, const wchar_t* valueName, DWORD data) {
    LONG result = RegSetValueExW(
        hKey,
        valueName,
        0,
        REG_DWORD,
        (const BYTE*)&data,
        sizeof(DWORD)
    );

    if (result == ERROR_SUCCESS) {
        printf("[OK] Set DWORD value: %S = %lu (0x%X)\n", valueName, data, data);
        return true;
    }

    PrintError("Failed to set DWORD value", result);
    return false;
}

//
// Function: GetDWORDValue
// Description: Retrieves a DWORD value from the registry key
// Parameters:
//   - hKey: Handle to the open registry key
//   - valueName: Name of the value to read (e.g., "param1")
//   - pData: Pointer to receive the DWORD data
// Returns: TRUE on success, FALSE on failure
//
bool GetDWORDValue(HKEY hKey, const wchar_t* valueName, DWORD* pData) {
    DWORD type = 0;
    DWORD dataSize = sizeof(DWORD);
    DWORD data = 0;

    LONG result = RegQueryValueExW(
        hKey,
        valueName,
        NULL,
        &type,
        (BYTE*)&data,
        &dataSize
    );

    if (result == ERROR_SUCCESS) {
        if (type == REG_DWORD) {
            *pData = data;
            printf("[OK] Read DWORD value: %S = %lu (0x%X)\n", valueName, data, data);
            return true;
        }
        else {
            printf("[ERR] Value %S exists but is not DWORD (type: %lu)\n", valueName, type);
            return false;
        }
    }

    if (result == ERROR_FILE_NOT_FOUND) {
        printf("[WARN] Value not found: %S\n", valueName);
    }
    else {
        PrintError("Failed to read DWORD value", result);
    }
    return false;
}

//
// Function: DeleteRegistryValue
// Description: Deletes a value from the registry key
// Parameters:
//   - hKey: Handle to the open registry key
//   - valueName: Name of the value to delete (e.g., "param1")
// Returns: TRUE on success, FALSE on failure
//
bool DeleteRegistryValue(HKEY hKey, const wchar_t* valueName) {
    LONG result = RegDeleteValueW(hKey, valueName);

    if (result == ERROR_SUCCESS) {
        printf("[OK] Deleted value: %S\n", valueName);
        return true;
    }

    if (result == ERROR_FILE_NOT_FOUND) {
        printf("[WARN] Value not found: %S\n", valueName);
        return false;
    }

    PrintError("Failed to delete value", result);
    return false;
}

//
// Function: DeleteRegistryKey
// Description: Deletes a registry key
// Parameters:
//   - rootKey: Root registry key
//   - subKey: Subkey path to delete
// Returns: TRUE on success, FALSE on failure
//
bool DeleteRegistryKey(HKEY rootKey, const wchar_t* subKey) {
    LONG result = RegDeleteKeyExW(
        rootKey,
        subKey,
        KEY_WOW64_64_KEY,
        0
    );

    if (result == ERROR_SUCCESS) {
        printf("[OK] Deleted key: %S\n", subKey);
        return true;
    }

    if (result == ERROR_FILE_NOT_FOUND) {
        printf("[WARN] Key not found: %S\n", subKey);
        return false;
    }

    PrintError("Failed to delete key (make sure it's empty)", result);
    return false;
}

//
// Function: EnumerateRegistryValues
// Description: Enumerates and displays all values in a registry key
// Parameters:
//   - hKey: Handle to the open registry key
// Returns: TRUE on success, FALSE on failure
//
bool EnumerateRegistryValues(HKEY hKey) {
    DWORD index = 0;
    wchar_t valueName[MAX_VALUE_NAME];
    DWORD valueNameSize;
    DWORD type;
    BYTE data[1024];
    DWORD dataSize;

    printf("\n[INFO] Enumerating values in the key...\n");

    while (true) {
        valueNameSize = MAX_VALUE_NAME;
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

        if (result == ERROR_NO_MORE_ITEMS) {
            break;
        }

        if (result != ERROR_SUCCESS) {
            PrintError("Enumeration failed", result);
            return false;
        }

        // Display value based on its type
        switch (type) {
        case REG_DWORD: {
            if (dataSize >= sizeof(DWORD)) {
                DWORD val = *(DWORD*)data;
                printf("  [%lu] %S (REG_DWORD) = %lu (0x%X)\n",
                    index, valueName, val, val);
            }
            break;
        }
        case REG_QWORD: {
            if (dataSize >= sizeof(ULONGLONG)) {
                ULONGLONG val = *(ULONGLONG*)data;
                printf("  [%lu] %S (REG_QWORD) = %llu\n", index, valueName, val);
            }
            break;
        }
        case REG_SZ: {
            wchar_t* str = (wchar_t*)data;
            printf("  [%lu] %S (REG_SZ) = \"%S\"\n", index, valueName, str);
            break;
        }
        case REG_EXPAND_SZ: {
            wchar_t* str = (wchar_t*)data;
            printf("  [%lu] %S (REG_EXPAND_SZ) = \"%S\"\n", index, valueName, str);
            break;
        }
        case REG_MULTI_SZ: {
            wchar_t* str = (wchar_t*)data;
            printf("  [%lu] %S (REG_MULTI_SZ) = ", index, valueName);
            while (*str) {
                printf("\"%S\" ", str);
                str += wcslen(str) + 1;
            }
            printf("\n");
            break;
        }
        case REG_BINARY: {
            printf("  [%lu] %S (REG_BINARY) = ", index, valueName);
            for (DWORD i = 0; i < dataSize && i < 32; i++) {
                printf("%02X ", data[i]);
            }
            if (dataSize > 32) printf("...");
            printf("\n");
            break;
        }
        default: {
            printf("  [%lu] %S (Type: %lu, Size: %lu bytes)\n",
                index, valueName, type, dataSize);
            break;
        }
        }
        index++;
    }

    printf("[INFO] Total values found: %lu\n", index);
    return true;
}

//
// Function: TestRegistryOperations
// Description: Main test function demonstrating operations on the TestDriver key
// Parameters:
//   - rootKey: Root registry key (HKEY_LOCAL_MACHINE, etc.)
// Returns: TRUE on success, FALSE on failure
//
bool TestRegistryOperations(HKEY rootKey) {
    bool success = true;
    HKEY hKey = NULL;
    DWORD value = 0;

    PrintSeparator();
    printf("[INFO] Starting registry test on: %S\n", TEST_REG_KEY);
    PrintSeparator();
    printf("\n");

    // ============================================
    // Step 1: Create/Open the key
    // ============================================
    printf("--- Step 1: Creating/opening key ---\n");

    hKey = OpenRegistryKey(rootKey, TEST_REG_KEY, true);
    if (!hKey) {
        success = false;
        goto cleanup;
    }
    printf("\n");

    // ============================================
    // Step 2: Set DWORD values (param1, param2, param3)
    // ============================================
    printf("--- Step 2: Setting DWORD values ---\n");

    SetDWORDValue(hKey, VALUE_PARAM1, 100);
    SetDWORDValue(hKey, VALUE_PARAM2, 200);
    SetDWORDValue(hKey, VALUE_PARAM3, 300);
    printf("\n");

    // ============================================
    // Step 3: Read DWORD values
    // ============================================
    printf("--- Step 3: Reading DWORD values ---\n");

    if (GetDWORDValue(hKey, VALUE_PARAM1, &value)) {
        printf("  %S = %lu\n", VALUE_PARAM1, value);
    }
    if (GetDWORDValue(hKey, VALUE_PARAM2, &value)) {
        printf("  %S = %lu\n", VALUE_PARAM2, value);
    }
    if (GetDWORDValue(hKey, VALUE_PARAM3, &value)) {
        printf("  %S = %lu\n", VALUE_PARAM3, value);
    }
    printf("\n");

    // ============================================
    // Step 4: Enumerate all values
    // ============================================
    printf("--- Step 4: Enumerating all values ---\n");
    EnumerateRegistryValues(hKey);
    printf("\n");

    // ============================================
    // Step 5: Delete a value (demonstration)
    // ============================================
    printf("--- Step 5: Deleting a value (param3) ---\n");
    DeleteRegistryValue(hKey, VALUE_PARAM3);
    printf("\n");

    // ============================================
    // Step 6: Verify the value was deleted
    // ============================================
    printf("--- Step 6: Verifying deletion (param3) ---\n");
    if (!GetDWORDValue(hKey, VALUE_PARAM3, &value)) {
        printf("  param2 was successfully deleted (not found)\n");
    }
    printf("\n");

    // ============================================
    // Step 7: Re-create the deleted value
    // ============================================
    printf("--- Step 7: Re-creating param3 ---\n");
    SetDWORDValue(hKey, VALUE_PARAM3, 999);
    printf("\n");

    // ============================================
    // Step 8: Final enumeration
    // ============================================
    printf("--- Step 8: Final enumeration ---\n");
    EnumerateRegistryValues(hKey);
    printf("\n");

cleanup:
    // ============================================
    // Cleanup: Close the key
    // ============================================
    printf("--- Cleanup: Closing key ---\n");
    if (hKey) CloseRegistryKey(hKey);
    printf("\n");

    PrintSeparator();
    printf("[INFO] Test completed.\n");
    PrintSeparator();

    return success;
}

//
// Function: main
// Description: Program entry point
//
int main() {
    // Set console to UTF-8 for proper Unicode display
    SetConsoleOutputCP(CP_UTF8);

    printf("\n");
    PrintSeparator();
    printf("[INFO] USER-MODE REGISTRY TEST\n");
    printf("[INFO] Testing DWORD values in: %S\n", TEST_REG_KEY);
    printf("[INFO] Values: param1, param2, param3\n");
    PrintSeparator();
    printf("\n");

    // Check for administrator privileges
    BOOL isAdmin = FALSE;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD size = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, size, &size)) {
            isAdmin = elevation.TokenIsElevated;
        }
        CloseHandle(hToken);
    }

    if (!isAdmin) {
        printf("[WARN] Running without administrator privileges.\n");
        printf("[WARN] You may not be able to write to HKLM.\n");
        printf("[WARN] Consider running as administrator.\n\n");
    }

    // Ask user for cleanup option
    printf("Do you want to delete the test key after the test? (y/n): ");
    char choice;
    scanf_s("%c", &choice, 1);

    // Run the test
    bool result = TestRegistryOperations(HKEY_LOCAL_MACHINE);

    if (choice == 'y' || choice == 'Y') {
        printf("\n--- Cleanup: Deleting test key ---\n");
        DeleteRegistryKey(HKEY_LOCAL_MACHINE, TEST_REG_KEY);
    }

    printf("\n[INFO] Press Enter to exit...");
    getchar();
    getchar();

    return result ? 0 : 1;
}