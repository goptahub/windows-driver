// ruleloader.h
#pragma once

#include "regfltr.h"

#define RULES_FILE L"\\??\\C:\\Users\\vldm\\Desktop\\rules.txt"
#define CTRL_UTIL L"regctrl.exe"

#define MAX_PROCESSES     64
#define MAX_REGISTRY_KEYS 64
#define MAX_STRING_LEN    512

typedef struct _PROCESS_INFO {
    WCHAR Name[MAX_STRING_LEN];
    INT   Privilege; // 1..4
} PROCESS_INFO, * PPROCESS_INFO;

typedef struct _REGISTRY_KEY_INFO {
    WCHAR Path[MAX_STRING_LEN];
    INT   Privilege; // 1..4
} REGISTRY_KEY_INFO, * PREGISTRY_KEY_INFO;

typedef struct _RULES_DATA {
    PROCESS_INFO       Processes[MAX_PROCESSES];
    ULONG              ProcessCount;
    REGISTRY_KEY_INFO  RegistryKeys[MAX_REGISTRY_KEYS];
    ULONG              RegistryKeyCount;
} RULES_DATA, * PRULES_DATA;

extern FAST_MUTEX g_RulesMutex;

VOID InitRules(
    PRULES_DATA pRules
);

NTSTATUS LoadRules(
    PRULES_DATA pData
);

NTSTATUS SetDefaultRules(
    PRULES_DATA pRules
);

BOOLEAN IsAccessed(
    PUNICODE_STRING ProcessName,
    PUNICODE_STRING RegistryPath,
    PRULES_DATA     Data
);

INT GetProcessPrivilege(
    PUNICODE_STRING ProcessName,
    PRULES_DATA     Data
);

INT GetRegistryKeyPrivilege(
    PUNICODE_STRING RegistryPath,
    PRULES_DATA     Data
);

NTSTATUS
PrintRulesData(
    PRULES_DATA Rules
);

NTSTATUS CreateRules(
    PRULES_DATA pRules
);
