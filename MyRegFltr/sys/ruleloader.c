#include "regfltr.h"
#include "ruleloader.h"
#include <ntstrsafe.h>
#include "cxmlparser.h"
#include "log.h"

//#define STUB 1

FAST_MUTEX g_RulesMutex;

VOID InitRules(
    PRULES_DATA pRules
)
{
    RtlZeroMemory(pRules, sizeof(RULES_DATA));
    pRules->ProcessCount = 0;
    pRules->RegistryKeyCount = 0;
}

NTSTATUS LoadRules(
    PRULES_DATA pRules
)
{
    Log(L"[*] LoadRules ! called\n");

    if (!pRules) {
        Log(L"[-] LoadRules: !pRules: failed\n");
        return STATUS_INVALID_HANDLE;
    }

    NTSTATUS Status;
    HANDLE hKey = NULL;
    UNICODE_STRING keyPath;
    OBJECT_ATTRIBUTES objAttr;

    Log(L"[*] LoadRules: RtlInitUnicodeString called\n");

    RtlInitUnicodeString(&keyPath, CONF_KEY_NAME);

    Log(L"[*] LoadRules: InitializeObjectAttributes called\n");

    InitializeObjectAttributes(
        &objAttr,
        &keyPath,
        OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
        NULL,
        NULL
    );

    Log(L"[*] LoadRules: ZwOpenKey called\n");

    Status = ZwOpenKey(&hKey, KEY_READ, &objAttr);
    if (!NT_SUCCESS(Status)) {
        Log(L"[-] LoadRules: Failed to open registry key (Status: 0x%08X)\n", Status);
        return STATUS_UNSUCCESSFUL;
    }

    Status = ParseConfigXmlFromRegistry(hKey, pRules);

    Log(L"[*] LoadRules: ZwClose called\n");
    ZwClose(hKey);

    if (!NT_SUCCESS(Status)) {
        Log(L"[-] LoadRules: Failed to parse XML from registry (Status: 0x%08X)\n", Status);
        return STATUS_UNSUCCESSFUL;
    }

    Log(L"[+] LoadRules: Successfully loaded %lu processes, %lu registry keys\n",
        pRules->ProcessCount, pRules->RegistryKeyCount);

    return STATUS_SUCCESS;
}

BOOLEAN IsAccessed(
   PUNICODE_STRING ProcessName,
   PUNICODE_STRING RegistryPath,
   PRULES_DATA     pRules
) {
    ExAcquireFastMutex(&g_RulesMutex);
    INT procPriv = GetProcessPrivilege(ProcessName, pRules);
    INT keyPriv = GetRegistryKeyPrivilege(RegistryPath, pRules);
    ExReleaseFastMutex(&g_RulesMutex);
    return (procPriv >= keyPriv);
}

INT GetProcessPrivilege(
   PUNICODE_STRING ProcessName,
   PRULES_DATA     pRules
) {
    if (!ProcessName || !pRules) return 4;

    UNICODE_STRING storedName;
    for (ULONG i = 0; i < pRules->ProcessCount; i++) {
        RtlInitUnicodeString(&storedName, pRules->Processes[i].Name);
        if (RtlEqualUnicodeString(ProcessName, &storedName, TRUE)) {
            return pRules->Processes[i].Privilege;
        }
    }
    //
    // process absent in config is permitted to any regkey
    //
    return 4;
}

INT GetRegistryKeyPrivilege(
   PUNICODE_STRING RegistryPath,
   PRULES_DATA     pRules
) {
    if (!RegistryPath || !pRules) return 4;

    UNICODE_STRING storedPath;
    for (ULONG i = 0; i < pRules->RegistryKeyCount; i++) {
        RtlInitUnicodeString(&storedPath, pRules->RegistryKeys[i].Path);
        if (RtlEqualUnicodeString(RegistryPath, &storedPath, TRUE)) {
            return pRules->RegistryKeys[i].Privilege;
        }
    }
    //
    // regkey absent in config is available for any process
    //
    return 1;
}

NTSTATUS SetDefaultRules(
   PRULES_DATA pRules
)
/*
    sets the highest pivilege level to the value that contains rules conf
*/
{
    PREGISTRY_KEY_INFO key;
    PPROCESS_INFO proc;

    if (pRules->RegistryKeyCount >= MAX_REGISTRY_KEYS) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    key = &pRules->RegistryKeys[pRules->RegistryKeyCount];
    RtlStringCchCopyW(key->Path, MAX_STRING_LEN, CONF_VALUE_PATH);
    key->Privilege = 5;
    pRules->RegistryKeyCount++;

    if (pRules->ProcessCount >= MAX_REGISTRY_KEYS) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    proc = &pRules->Processes[pRules->ProcessCount];
    RtlStringCchCopyW(proc->Name, MAX_STRING_LEN, CTRL_UTIL);
    proc->Privilege = 5;
    pRules->ProcessCount++;

    return STATUS_SUCCESS;
}

NTSTATUS
PrintRulesData(
   PRULES_DATA pRules
)
{
    NTSTATUS status;
    HANDLE hFile = NULL;
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK ioStatus;
    UNICODE_STRING filePath;
    WCHAR buffer[4096];
    SIZE_T bufferOffset = 0;
    ULONG i;
    //SIZE_T charsWritten;

    // Открываем или создаем файл
    RtlInitUnicodeString(&filePath, RULES_FILE);

    InitializeObjectAttributes(
        &objAttr,
        &filePath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL
    );

    status = ZwCreateFile(
        &hFile,
        GENERIC_WRITE,
        &objAttr,
        &ioStatus,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ,
        FILE_OVERWRITE_IF,      // Перезаписываем файл
        FILE_SYNCHRONOUS_IO_NONALERT,
        NULL,
        0
    );

    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Заголовок
    bufferOffset += (SIZE_T)_snwprintf_s(
        buffer + bufferOffset,
        (sizeof(buffer) / sizeof(WCHAR)) - bufferOffset,
        _TRUNCATE,
        L"========================================\r\n"
        L"RULES DATA DUMP\r\n"
        L"========================================\r\n"
        L"Process Count: %lu\r\n"
        L"Registry Key Count: %lu\r\n"
        L"========================================\r\n\r\n",
        pRules->ProcessCount,
        pRules->RegistryKeyCount
    );

    // Процессы
    if (pRules->ProcessCount > 0) {
        bufferOffset += (SIZE_T)_snwprintf_s(
            buffer + bufferOffset,
            (sizeof(buffer) / sizeof(WCHAR)) - bufferOffset,
            _TRUNCATE,
            L"=== PROCESSES ===\r\n"
        );

        for (i = 0; i < pRules->ProcessCount && i < MAX_PROCESSES; i++) {
            // Проверяем, что имя не пустое
            if (pRules->Processes[i].Name[0] != L'\0') {
                bufferOffset += (SIZE_T)_snwprintf_s(
                    buffer + bufferOffset,
                    (sizeof(buffer) / sizeof(WCHAR)) - bufferOffset,
                    _TRUNCATE,
                    L"  [%lu] Name: \"%ws\", Privilege: %d\r\n",
                    i,
                    pRules->Processes[i].Name,
                    pRules->Processes[i].Privilege
                );
            }
        }
        bufferOffset += (SIZE_T)_snwprintf_s(
            buffer + bufferOffset,
            (sizeof(buffer) / sizeof(WCHAR)) - bufferOffset,
            _TRUNCATE,
            L"\r\n"
        );
    }
    else {
        bufferOffset += (SIZE_T)_snwprintf_s(
            buffer + bufferOffset,
            (sizeof(buffer) / sizeof(WCHAR)) - bufferOffset,
            _TRUNCATE,
            L"No processes configured.\r\n\r\n"
        );
    }

    // Ключи реестра
    if (pRules->RegistryKeyCount > 0) {
        bufferOffset += (SIZE_T)_snwprintf_s(
            buffer + bufferOffset,
            (sizeof(buffer) / sizeof(WCHAR)) - bufferOffset,
            _TRUNCATE,
            L"=== REGISTRY KEYS ===\r\n"
        );

        for (i = 0; i < pRules->RegistryKeyCount && i < MAX_REGISTRY_KEYS; i++) {
            if (pRules->RegistryKeys[i].Path[0] != L'\0') {
                bufferOffset += (SIZE_T)_snwprintf_s(
                    buffer + bufferOffset,
                    (sizeof(buffer) / sizeof(WCHAR)) - bufferOffset,
                    _TRUNCATE,
                    L"  [%lu] Path: \"%ws\", Privilege: %d\r\n",
                    i,
                    pRules->RegistryKeys[i].Path,
                    pRules->RegistryKeys[i].Privilege
                );
            }
        }
        bufferOffset += (SIZE_T)_snwprintf_s(
            buffer + bufferOffset,
            (sizeof(buffer) / sizeof(WCHAR)) - bufferOffset,
            _TRUNCATE,
            L"\r\n"
        );
    }
    else {
        bufferOffset += (SIZE_T)_snwprintf_s(
            buffer + bufferOffset,
            (sizeof(buffer) / sizeof(WCHAR)) - bufferOffset,
            _TRUNCATE,
            L"No registry keys configured.\r\n\r\n"
        );
    }

    // Подвал
    bufferOffset += (SIZE_T)_snwprintf_s(
        buffer + bufferOffset,
        (sizeof(buffer) / sizeof(WCHAR)) - bufferOffset,
        _TRUNCATE,
        L"========================================\r\n"
        L"END OF DUMP\r\n"
        L"========================================\r\n"
    );

    // Записываем в файл
    status = ZwWriteFile(
        hFile,
        NULL,
        NULL,
        NULL,
        &ioStatus,
        buffer,
        (ULONG)(bufferOffset * sizeof(WCHAR)),
        NULL,
        NULL
    );

    if (!NT_SUCCESS(status)) {
        Log(L"[-] PrintRulesData: ZwWriteFile failed (0x%x)\n", status);
    }

    // Закрываем файл
    ZwClose(hFile);

    return status;
}

NTSTATUS CreateRules(
    PRULES_DATA pRules
)
{
    NTSTATUS Status;
    ExAcquireFastMutex(&g_RulesMutex);
    InitRules(pRules);
    Status = SetDefaultRules(pRules);
    ExReleaseFastMutex(&g_RulesMutex);
    return Status;
}