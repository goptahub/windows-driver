#include "log.h"
#include <stdarg.h>
#include <ntstrsafe.h>  // Для RtlStringCbCopyW и т.д.

static HANDLE g_LogFileHandle = NULL;

NTSTATUS LogOpenFile(VOID)
{
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK ioStatus;
    UNICODE_STRING filePath;

    if (g_LogFileHandle != NULL) {
        return STATUS_SUCCESS;
    }

    RtlInitUnicodeString(&filePath, LOG_FILE);

    InitializeObjectAttributes(&objAttr,
        &filePath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL);

    return ZwCreateFile(&g_LogFileHandle,
        GENERIC_WRITE | FILE_APPEND_DATA,
        &objAttr,
        &ioStatus,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ,
        FILE_OPEN_IF,
        FILE_SYNCHRONOUS_IO_NONALERT,
        NULL,
        0);
}

VOID LogClose(VOID)
{
    if (g_LogFileHandle != NULL) {
        ZwClose(g_LogFileHandle);
        g_LogFileHandle = NULL;
    }
}

NTSTATUS LogWriteToFile(PWCHAR buffer, SIZE_T length)
{
    IO_STATUS_BLOCK ioStatus;
    LARGE_INTEGER byteOffset;
    NTSTATUS status;

    if (g_LogFileHandle == NULL) {
        status = LogOpenFile();
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

    byteOffset.QuadPart = -1; // Писать в конец файла

    status = ZwWriteFile(g_LogFileHandle,
        NULL,
        NULL,
        NULL,
        &ioStatus,
        buffer,
        (ULONG)(length * sizeof(WCHAR)),
        &byteOffset,
        NULL);

    //LogClose();

    return status;
}

int LogPrintf(const wchar_t* format, ...)
{
    NTSTATUS status;
    WCHAR buffer[2048];
    va_list args;
    int len;

    va_start(args, format);
    len = _vsnwprintf_s(buffer,
        sizeof(buffer) / sizeof(WCHAR),
        _TRUNCATE,
        format,
        args);
    va_end(args);

    if (len < 0) {
        // Ошибка форматирования
        return 1;
    }

    status = LogWriteToFile(buffer, len);
    if (!NT_SUCCESS(status)) {
        return 2;
    }

    return 0;
}