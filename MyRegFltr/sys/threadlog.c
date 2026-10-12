#include "regfltr.h"
#include "threadlog.h"
#include "log.h"

#include <ntddk.h>
#include <ntstrsafe.h>

#define THREAD_LOG_FILE L"\\??\\C:\\Users\\vldm\\Desktop\\thread_log.txt"

BOOLEAN ThreadCallbackRegistered = FALSE;

typedef struct _THREAD_LOG_INFO {
    LARGE_INTEGER SystemTime;
    HANDLE ProcessId;
    HANDLE ThreadId;
    BOOLEAN Create;
    PEPROCESS Process;
} THREAD_LOG_INFO;

NTSTATUS GetProcessName(
    _In_ HANDLE ProcessId,
    _Out_ PUNICODE_STRING ProcessName,
    _In_ SIZE_T BufferSize
)
{
    NTSTATUS status;
    PEPROCESS process = NULL;
    PCHAR imageFileName = NULL;
    SIZE_T ansiLen;
    SIZE_T i;

    RtlZeroMemory(ProcessName, sizeof(UNICODE_STRING));
    ProcessName->Buffer = (PWSTR)ExAllocatePoolZero(
        NonPagedPoolNx,
        BufferSize,
        'pNmT'
    );

    if (!ProcessName->Buffer) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = PsLookupProcessByProcessId(ProcessId, &process);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(ProcessName->Buffer, 'pNmT');
        ProcessName->Buffer = NULL;
        return status;
    }

    imageFileName = PsGetProcessImageFileName(process);
    if (imageFileName) {
        ansiLen = strlen(imageFileName);
        if (ansiLen >= BufferSize / sizeof(WCHAR)) {
            ansiLen = BufferSize / sizeof(WCHAR) - 1;
        }

        for (i = 0; i < ansiLen; i++) {
            ProcessName->Buffer[i] = (WCHAR)imageFileName[i];
        }
        ProcessName->Buffer[ansiLen] = L'\0';
        ProcessName->Length = (USHORT)(ansiLen * sizeof(WCHAR));
        ProcessName->MaximumLength = (USHORT)BufferSize;
    }

    ObDereferenceObject(process);
    return STATUS_SUCCESS;
}

NTSTATUS GetProcessModulePath(
    _In_ HANDLE ProcessId,
    _Out_ PUNICODE_STRING ModulePath,
    _In_ SIZE_T BufferSize
)
{
    NTSTATUS status;
    PEPROCESS process = NULL;

    RtlZeroMemory(ModulePath, sizeof(UNICODE_STRING));
    ModulePath->Buffer = (PWSTR)ExAllocatePoolZero(
        NonPagedPoolNx,
        BufferSize,
        'pMdT'
    );

    if (!ModulePath->Buffer) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = PsLookupProcessByProcessId(ProcessId, &process);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(ModulePath->Buffer, 'pMdT');
        ModulePath->Buffer = NULL;
        return status;
    }
/*
    KIRQL irql = KeGetCurrentIrql();
    if (irql != PASSIVE_LEVEL) {
        //do not log whatever using ZwCreateFile as ZwCreateFile is forbidden IRQL > PASSIVE_LEVEL and causes SYSTEM_SERVICE_EXCEPTION
        return STATUS_SUCCESS;
    }
    else {
        PUNICODE_STRING imageFileName = NULL;
        SIZE_T pathLength;
        __try {
            
            status = SeLocateProcessImageName(process, &imageFileName);
            if (NT_SUCCESS(status) && imageFileName && imageFileName->Buffer) {
                pathLength = min(imageFileName->Length, BufferSize - sizeof(WCHAR));
                RtlCopyMemory(ModulePath->Buffer, imageFileName->Buffer, pathLength);
                ModulePath->Buffer[pathLength / sizeof(WCHAR)] = L'\0';
                ModulePath->Length = (USHORT)pathLength;
                ModulePath->MaximumLength = (USHORT)BufferSize;

                if (imageFileName->Buffer) {
                    ExFreePool(imageFileName->Buffer);
                }
                ExFreePool(imageFileName);
                ObDereferenceObject(process);
                return STATUS_SUCCESS;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            if (imageFileName) {
                if (imageFileName->Buffer) {
                    ExFreePool(imageFileName->Buffer);
                }
                ExFreePool(imageFileName);
            }
        }
    }
*/

    PCHAR imageFileNameAnsi = PsGetProcessImageFileName(process);
    if (imageFileNameAnsi) {
        SIZE_T ansiLen = strlen(imageFileNameAnsi);
        SIZE_T copyLen = min(ansiLen, BufferSize / sizeof(WCHAR) - 1);

        for (SIZE_T i = 0; i < copyLen; i++) {
            ModulePath->Buffer[i] = (WCHAR)imageFileNameAnsi[i];
        }
        ModulePath->Buffer[copyLen] = L'\0';
        ModulePath->Length = (USHORT)(copyLen * sizeof(WCHAR));
        ModulePath->MaximumLength = (USHORT)BufferSize;
    }

    ObDereferenceObject(process);
    return STATUS_SUCCESS;
}

VOID GetSystemTimeString(
    _Out_ WCHAR* TimeBuffer,
    _In_ SIZE_T BufferSize
)
{
    LARGE_INTEGER systemTime;
    LARGE_INTEGER localTime;
    TIME_FIELDS timeFields;

    KeQuerySystemTime(&systemTime);
    ExSystemTimeToLocalTime(&systemTime, &localTime);
    RtlTimeToTimeFields(&localTime, &timeFields);

    RtlStringCbPrintfW(
        TimeBuffer,
        BufferSize,
        L"%04d-%02d-%02d %02d:%02d:%02d.%03d",
        timeFields.Year,
        timeFields.Month,
        timeFields.Day,
        timeFields.Hour,
        timeFields.Minute,
        timeFields.Second,
        timeFields.Milliseconds
    );
}

NTSTATUS WriteThreadLogToFile(
    _In_ PWCHAR LogBuffer
)
{
    NTSTATUS status;
    HANDLE hFile = NULL;
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK ioStatus;
    UNICODE_STRING filePath;
    LARGE_INTEGER byteOffset;

    RtlInitUnicodeString(&filePath, THREAD_LOG_FILE);

    InitializeObjectAttributes(
        &objAttr,
        &filePath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL
    );

    status = ZwCreateFile(
        &hFile,
        GENERIC_WRITE | FILE_APPEND_DATA,
        &objAttr,
        &ioStatus,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ,
        FILE_OPEN_IF,
        FILE_SYNCHRONOUS_IO_NONALERT,
        NULL,
        0
    );

    if (!NT_SUCCESS(status)) {
        return status;
    }

    byteOffset.QuadPart = -1; // write into the file end

    status = ZwWriteFile(
        hFile,
        NULL,
        NULL,
        NULL,
        &ioStatus,
        LogBuffer,
        (ULONG)(wcslen(LogBuffer) * sizeof(WCHAR)),
        &byteOffset,
        NULL
    );

    ZwClose(hFile);
    return status;
}

VOID CreateThreadNotifyRoutine(
    HANDLE ProcessId,
    HANDLE ThreadId,
    BOOLEAN Create
)
{
    NTSTATUS status;
    UNICODE_STRING ProcessName = { 0 };
    UNICODE_STRING ModulePath = { 0 };
    WCHAR timeBuffer[64] = { 0 };
    WCHAR logBuffer[1024] = { 0 };
    HANDLE currentPid = PsGetCurrentProcessId();
    BOOLEAN bProcessNameAllocated = FALSE;
    BOOLEAN bModulePathAllocated = FALSE;

    if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
        /*do not log whatever using ZwCreateFile as ZwCreateFile is forbidden IRQL > PASSIVE_LEVEL and causes SYSTEM_SERVICE_EXCEPTION*/
        return;
    }

    GetSystemTimeString(timeBuffer, sizeof(timeBuffer));

    status = GetProcessName(ProcessId, &ProcessName, 256 * sizeof(WCHAR));
    if (!NT_SUCCESS(status)) {
        RtlInitUnicodeString(&ProcessName, L"Unknown"); //stub
        bProcessNameAllocated = FALSE;
    }
    else {
        bProcessNameAllocated = TRUE;
    }

    status = GetProcessModulePath(ProcessId, &ModulePath, 512 * sizeof(WCHAR));
    if (!NT_SUCCESS(status)) {
        RtlInitUnicodeString(&ModulePath, L"Unknown"); //stub
        bModulePathAllocated = FALSE;
    }
    else {
        bModulePathAllocated = TRUE;
    }

    if (Create) {
        RtlStringCbPrintfW(
            logBuffer,
            sizeof(logBuffer),
            L"[THREAD START] Time: %s | Process: %ws (PID: %d) | Module: %ws | Thread: %d | Creator: PID %d\n",
            timeBuffer,
            ProcessName.Buffer ? ProcessName.Buffer : L"Unknown",
            (ULONG)(ULONG_PTR)ProcessId,
            ModulePath.Buffer ? ModulePath.Buffer : L"Unknown",
            (ULONG)(ULONG_PTR)ThreadId,
            (ULONG)(ULONG_PTR)currentPid
        );
    }
    else {
        RtlStringCbPrintfW(
            logBuffer,
            sizeof(logBuffer),
            L"[THREAD END] Time: %s | Process: %ws (PID: %d) | Module: %ws | Thread: %d\n",
            timeBuffer,
            ProcessName.Buffer ? ProcessName.Buffer : L"Unknown",
            (ULONG)(ULONG_PTR)ProcessId,
            ModulePath.Buffer ? ModulePath.Buffer : L"Unknown",
            (ULONG)(ULONG_PTR)ThreadId
        );
    }

    WriteThreadLogToFile(logBuffer);

    if (bProcessNameAllocated && ProcessName.Buffer != NULL) {
        ExFreePoolWithTag(ProcessName.Buffer, 'pNmT');
    }
    if (bModulePathAllocated && ModulePath.Buffer != NULL) {
        ExFreePoolWithTag(ModulePath.Buffer, 'pMdT');
    }
}

