#include "regfltr.h"
#include "log.h"

NTSTATUS GetCurrentProcessNameW(
     PWSTR NameBuffer,
     SIZE_T BufferSizeInChars
)
{
    //Log(L"[*] GetCurrentProcessNameW called\n");
    NTSTATUS status;
    PEPROCESS process;
    HANDLE pid = PsGetCurrentProcessId();
    PCHAR imageFileName;
    SIZE_T ansiLen;
    SIZE_T i;

    status = PsLookupProcessByProcessId(pid, &process);
    if (!NT_SUCCESS(status)) {
        Log(L"[-] GetCurrentProcessNameW: PsLookupProcessByProcessId failed\n");
        return status;
    }

    imageFileName = PsGetProcessImageFileName(process);
    ansiLen = strlen(imageFileName);

    if (ansiLen >= BufferSizeInChars) {
        ansiLen = BufferSizeInChars - 1;
    }

    // ANSI -> UTF-16LE 
    for (i = 0; i < ansiLen; i++) {
        NameBuffer[i] = (WCHAR)imageFileName[i];
    }
    NameBuffer[ansiLen] = L'\0';

    ObDereferenceObject(process);

    //Log(L"[+] GetCurrentProcessNameW succeeded\n");
    return STATUS_SUCCESS;
}

BOOLEAN
RegesterCallbackPreNotificationBlock(
    PCALLBACK_CONTEXT* ppCallbackCtx
    )
{
    //Log(L"[*] RegesterCallbackPreNotificationBlock called\n");

    PCALLBACK_CONTEXT CallbackCtx;
    NTSTATUS Status;
    BOOLEAN Success = FALSE;

    //InfoPrint("RegesterCallbackPreNotificationBlock started");

    //
    // Create the callback context
    //

    CallbackCtx = CreateCallbackContext(
        CALLBACK_MODE_PRE_NOTIFICATION_BLOCK,
        CALLBACK_ALTITUDE);

    if (CallbackCtx == NULL) {
        Log(L"[-] RegesterCallbackPreNotificationBlock: CreateCallbackContext failed\n");
        goto Exit;
    }

    //
    // Register callback
    //


    Status = CmRegisterCallbackEx(  Callback,
                                    &CallbackCtx->Altitude,
                                    g_DeviceObj->DriverObject,
                                    (PVOID)CallbackCtx,
                                    &CallbackCtx->Cookie,
                                    NULL);
    if (!NT_SUCCESS(Status)) {
        //ErrorPrint("CmRegisterCallback failed. Status 0x%x", Status);
        Log(L"[-] RegesterCallbackPreNotificationBlock: CmRegisterCallbackEx failed\n");
        goto Exit;
    }

    *ppCallbackCtx = CallbackCtx;
    Success = TRUE;
 
    //Log(L"[+] RegesterCallbackPreNotificationBlock succeeded\n");
  Exit:
    //InfoPrint("RegesterCallbackPreNotificationBlock finished");
    return Success;
}

NTSTATUS
UnRegesterCallbackPreNotificationBlock(
    PCALLBACK_CONTEXT CallbackCtx
    )
{
    //Log(L"[*] UnRegesterCallbackPreNotificationBlock called\n");
    //InfoPrint("UnRegesterCallbackPreNotificationBlock started");

    NTSTATUS Status;
    Status = CmUnRegisterCallback(CallbackCtx->Cookie);

    if (!NT_SUCCESS(Status)) {
        //ErrorPrint("CmUnRegisterCallback failed. Status 0x%x", Status);
        Log(L"[-] UnRegesterCallbackPreNotificationBlock: CmUnRegisterCallback failed\n");
    }

    DeleteCallbackContext(CallbackCtx);
    //InfoPrint("UnRegesterCallbackPreNotificationBlock finished");

    if (NT_SUCCESS(Status)) {
        //Log(L"[+] UnRegesterCallbackPreNotificationBlock succeeded\n");
    }
    else {
        Log(L"[-] UnRegesterCallbackPreNotificationBlock failed\n");
    }
    
    return Status;
}


NTSTATUS GetFullKeyPathForCreateOpen(
    _In_ PCALLBACK_CONTEXT CallbackCtx,
    _In_ REG_NOTIFY_CLASS NotifyClass,
    _In_ PVOID Argument2,
    _Out_ PUNICODE_STRING FullPath
)
{
    //Log(L"[*] GetFullKeyPathForCreateOpen called\n");
    NTSTATUS status = STATUS_SUCCESS;
    PVOID rootObject = NULL;
    PUNICODE_STRING completeName = NULL;
    PCUNICODE_STRING pTempPath = NULL;
    PWSTR combinedPath = NULL;
    
    RtlZeroMemory(FullPath, sizeof(UNICODE_STRING));
    
    // Извлекаем общие поля в зависимости от типа уведомления
    if (NotifyClass == RegNtPreCreateKeyEx || NotifyClass == RegNtPostCreateKeyEx) {
        PREG_CREATE_KEY_INFORMATION info = (PREG_CREATE_KEY_INFORMATION)Argument2;
        rootObject = info->RootObject;
        completeName = info->CompleteName;
        //Log(L"[*] NotifyClass == RegNtPreCreateKeyEx || NotifyClass == RegNtPostCreateKeyEx\n");
    } 
    else if (NotifyClass == RegNtPreOpenKeyEx || NotifyClass == RegNtPostOpenKeyEx) {
        PREG_OPEN_KEY_INFORMATION info = (PREG_OPEN_KEY_INFORMATION)Argument2;
        rootObject = info->RootObject;
        completeName = info->CompleteName;
        //Log(L"[*] NotifyClass == RegNtPreOpenKeyEx || NotifyClass == RegNtPostOpenKeyEx\n");
    }
    else {
        Log(L"[-] GetFullKeyPathForCreateOpen: unknown NotifyClass\n");
        return STATUS_INVALID_PARAMETER;
    }
    
    if (completeName == NULL) {
        Log(L"[-] GetFullKeyPathForCreateOpen: completeName == NULL\n");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Проверяем, не является ли CompleteName уже полным путём
    if (completeName->Buffer != NULL && completeName->Buffer[0] == L'\\') {
        // Абсолютный путь - копируем напрямую
        FullPath->Length = completeName->Length;
        FullPath->MaximumLength = completeName->MaximumLength;
        FullPath->Buffer = (PWSTR)ExAllocatePoolZero(NonPagedPoolNx,
                                                  completeName->MaximumLength,
                                                  'pATH');
        if (FullPath->Buffer) {
            RtlCopyMemory(FullPath->Buffer, completeName->Buffer, completeName->Length);
            FullPath->Buffer[(INT)FullPath->Length / sizeof(WCHAR)] = L'\0';
            //Log(L"[+] GetFullKeyPathForCreateOpen succeeded\n");
            return STATUS_SUCCESS;
        }
        Log(L"[-] GetFullKeyPathForCreateOpen failed: ExAllocatePoolZero failed 1\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Относительный путь - нужно получить путь родительского объекта
    if (rootObject != NULL) {
        status = CmCallbackGetKeyObjectIDEx(&CallbackCtx->Cookie,
                                            rootObject,
                                            NULL,
                                            &pTempPath,
                                            0);
        if (NT_SUCCESS(status)) {
            // Собираем полный путь
            SIZE_T combinedLen = pTempPath->Length + sizeof(WCHAR) + completeName->Length;
            combinedPath = (PWSTR)ExAllocatePoolZero(NonPagedPoolNx,
                                                        combinedLen + sizeof(WCHAR),
                                                        'pATH');
            if (combinedPath) {
                RtlCopyMemory(combinedPath, pTempPath->Buffer, pTempPath->Length);
                combinedPath[pTempPath->Length / sizeof(WCHAR)] = L'\\';
                RtlCopyMemory((PCHAR)combinedPath + pTempPath->Length + sizeof(WCHAR),
                             completeName->Buffer,
                             completeName->Length);
                
                FullPath->Buffer = combinedPath;
                FullPath->Length = (USHORT)combinedLen;
                FullPath->MaximumLength = (USHORT)(combinedLen + sizeof(WCHAR));
                FullPath->Buffer[FullPath->Length / sizeof(WCHAR)] = L'\0';
                status = STATUS_SUCCESS;
                //Log(L"[+] GetFullKeyPathForCreateOpen succeeded\n");
            } else {
                Log(L"[-] GetFullKeyPathForCreateOpen: ExAllocatePoolZero failed 2\n");
                status = STATUS_INSUFFICIENT_RESOURCES;
            }
            
            CmCallbackReleaseKeyObjectIDEx(pTempPath);
        }
        else {
            Log(L"[-] GetFullKeyPathForCreateOpen: CmCallbackGetKeyObjectIDEx failed\n");
        }
    }

    //Log(L"[*] GetFullKeyPathForCreateOpen finished\n");
    
    return status;
}

#define VALUE_NAME_LEN 1024

NTSTATUS GetValueNameFromArgument(
    _In_ REG_NOTIFY_CLASS NotifyClass,
    _In_ PVOID Argument2,
    _Out_ PUNICODE_STRING ValueName,
    _In_ SIZE_T MaxBufferSize
)
{
    PUNICODE_STRING pValueName = NULL;
    SIZE_T copyLength;

    RtlZeroMemory(ValueName, sizeof(UNICODE_STRING));

    if (Argument2 == NULL) {
        Log(L"[-] GetValueNameFromArgumentSafe: Argument2 is NULL\n");
        return STATUS_INVALID_PARAMETER;
    }

    // Извлекаем ValueName в зависимости от типа
    switch (NotifyClass) {
    case RegNtPreSetValueKey: {
        PREG_SET_VALUE_KEY_INFORMATION info = (PREG_SET_VALUE_KEY_INFORMATION)Argument2;
        pValueName = info->ValueName;
        break;
    }
    case RegNtPreDeleteValueKey: {
        PREG_DELETE_VALUE_KEY_INFORMATION info = (PREG_DELETE_VALUE_KEY_INFORMATION)Argument2;
        pValueName = info->ValueName;
        break;
    }
    case RegNtPreQueryValueKey: {
        PREG_QUERY_VALUE_KEY_INFORMATION info = (PREG_QUERY_VALUE_KEY_INFORMATION)Argument2;
        pValueName = info->ValueName;
        break;
    }
    default:
        return STATUS_INVALID_PARAMETER;
    }

    if (pValueName == NULL || pValueName->Buffer == NULL) {
        // Пустое имя (Default)
        return STATUS_SUCCESS;
    }

    // Ограничиваем длину
    copyLength = min(pValueName->Length, MaxBufferSize - sizeof(WCHAR));
    if (copyLength < pValueName->Length) {
        Log(L"[*] GetValueNameFromArgumentSafe: Truncating value name from %lu to %lu bytes\n",
            pValueName->Length, copyLength);
    }

    // Выделяем память
    ValueName->Length = (USHORT)copyLength;
    ValueName->MaximumLength = (USHORT)(copyLength + sizeof(WCHAR));
    ValueName->Buffer = (PWSTR)ExAllocatePoolZero(
        NonPagedPoolNx,
        ValueName->MaximumLength,
        'vNmT'
    );

    if (ValueName->Buffer == NULL) {
        Log(L"[-] GetValueNameFromArgumentSafe: ExAllocatePoolZero failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(ValueName->Buffer,
        pValueName->Buffer,
        copyLength);
    ValueName->Buffer[copyLength / sizeof(WCHAR)] = L'\0';

    return STATUS_SUCCESS;
}

// Получение объекта ключа из структуры
PVOID GetKeyObjectFromArgument(
    _In_ REG_NOTIFY_CLASS NotifyClass,
    _In_ PVOID Argument2
)
{
    if (Argument2 == NULL) {
        return NULL;
    }

    switch (NotifyClass) {
    case RegNtPreSetValueKey: {
        PREG_SET_VALUE_KEY_INFORMATION info = (PREG_SET_VALUE_KEY_INFORMATION)Argument2;
        return info->Object;
    }
    case RegNtPreDeleteValueKey: {
        PREG_DELETE_VALUE_KEY_INFORMATION info = (PREG_DELETE_VALUE_KEY_INFORMATION)Argument2;
        return info->Object;
    }
    case RegNtPreQueryValueKey: {
        PREG_QUERY_VALUE_KEY_INFORMATION info = (PREG_QUERY_VALUE_KEY_INFORMATION)Argument2;
        return info->Object;
    }
    default:
        return NULL;
    }
}

NTSTATUS CopyValueName(
    _In_ PUNICODE_STRING SourceValueName,
    _Out_ PUNICODE_STRING DestinationValueName
)
{
    RtlZeroMemory(DestinationValueName, sizeof(UNICODE_STRING));

    if (SourceValueName == NULL || SourceValueName->Buffer == NULL) {
        return STATUS_SUCCESS; // Пустое имя (Default)
    }

    DestinationValueName->Length = SourceValueName->Length;
    DestinationValueName->MaximumLength = SourceValueName->MaximumLength;
    DestinationValueName->Buffer = (PWSTR)ExAllocatePoolZero(
        NonPagedPoolNx,
        SourceValueName->MaximumLength,
        'vNmT'
    );

    if (!DestinationValueName->Buffer) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(DestinationValueName->Buffer,
        SourceValueName->Buffer,
        SourceValueName->Length);

    return STATUS_SUCCESS;
}

// Освобождение имени значения
VOID FreeValueName(
    _In_ PUNICODE_STRING ValueName
)
{
    if (ValueName->Buffer != NULL) {
        ExFreePoolWithTag(ValueName->Buffer, 'vNmT');
        RtlZeroMemory(ValueName, sizeof(UNICODE_STRING));
    }
}

VOID FreeKeyPath(_In_ PUNICODE_STRING KeyPath)
{
    //Log(L"[*] FreeKeyPath called\n");
    if (KeyPath->Buffer != NULL) {
        ExFreePoolWithTag(KeyPath->Buffer, 'pATH');
        RtlZeroMemory(KeyPath, sizeof(UNICODE_STRING));
    }
    //Log(L"[+] FreeKeyPath succeeded\n");
}

NTSTATUS GetFullKeyPathFromObject(
    _In_ PCALLBACK_CONTEXT CallbackCtx,
    _In_ PVOID KeyObject,
    _Out_ PUNICODE_STRING FullPath
)
{
    //Log(L"[*] GetFullKeyPathFromObject called\n");

    NTSTATUS status;
    PCUNICODE_STRING path = NULL;

    RtlZeroMemory(FullPath, sizeof(UNICODE_STRING));

    if (KeyObject == NULL) {
        Log(L"[-] GetFullKeyPathFromObject failed: KeyObject == NULL\n");
        return STATUS_INVALID_PARAMETER;
    }

    status = CmCallbackGetKeyObjectIDEx(&CallbackCtx->Cookie,
        KeyObject,
        NULL,
        &path,
        0);


    if (NT_SUCCESS(status)) {
        // Создаём копию пути (память, возвращаемая CmCallbackGetKeyObjectIDEx,
        // будет освобождена при вызове CmCallbackReleaseKeyObjectIDEx)
        FullPath->Length = path->Length;
        FullPath->MaximumLength = path->MaximumLength;
        FullPath->Buffer = (PWSTR)ExAllocatePoolZero(NonPagedPoolNx,
            path->MaximumLength,
            'pATH');
        if (FullPath->Buffer) {
            RtlCopyMemory(FullPath->Buffer, path->Buffer, path->MaximumLength);
        }
        else {
            status = STATUS_INSUFFICIENT_RESOURCES;
            Log(L"[-] GetFullKeyPathFromObject: ExAllocatePoolZero failed 3\n");
        }

        CmCallbackReleaseKeyObjectIDEx(path);
    }
    else {
        Log(L"[-] GetFullKeyPathFromObject: CmCallbackGetKeyObjectIDEx failed\n");
    }

    //Log(L"[*] GetFullKeyPathFromObject finished\n");

    return status;
}



NTSTATUS GetFullValuePath(
    _In_ PCALLBACK_CONTEXT CallbackCtx,
    _In_ REG_NOTIFY_CLASS NotifyClass,
    _In_ PVOID Argument2,
    _Out_ PUNICODE_STRING FullValuePath
)
{
    NTSTATUS status;
    UNICODE_STRING KeyPath = { 0 };
    UNICODE_STRING ValueName = { 0 };
    PWSTR combinedPath = NULL;
    SIZE_T combinedLength;

    RtlZeroMemory(FullValuePath, sizeof(UNICODE_STRING));

    if (CallbackCtx == NULL || Argument2 == NULL) {
        Log(L"[-] GetFullValuePath: Invalid parameters\n");
        return STATUS_INVALID_PARAMETER;
    }

    // 1. Получаем имя значения
    status = GetValueNameFromArgument(NotifyClass, Argument2, &ValueName, VALUE_NAME_LEN);
    if (!NT_SUCCESS(status)) {
        Log(L"[-] GetFullValuePath: Failed to get value name (0x%08X)\n", status);
        return status;
    }

    // 2. Получаем объект ключа
    PVOID KeyObject = GetKeyObjectFromArgument(NotifyClass, Argument2);
    if (KeyObject == NULL) {
        Log(L"[-] GetFullValuePath: Failed to get key object\n");
        FreeValueName(&ValueName);
        return STATUS_INVALID_PARAMETER;
    }

    // 3. Получаем путь к ключу
    status = GetFullKeyPathFromObject(CallbackCtx, KeyObject, &KeyPath);
    if (!NT_SUCCESS(status)) {
        Log(L"[-] GetFullValuePath: Failed to get key path (0x%08X)\n", status);
        FreeValueName(&ValueName);
        return status;
    }

    // 4. Объединяем путь к ключу и имя значения
    // Формат: \Registry\Machine\Software\Key\ValueName

    if (ValueName.Buffer == NULL || ValueName.Length == 0) {
        // Для Default значения - используем только путь ключа
        combinedLength = KeyPath.Length + sizeof(WCHAR) + 16; // + "(Default)"
        combinedPath = (PWSTR)ExAllocatePoolZero(
            NonPagedPoolNx,
            combinedLength + sizeof(WCHAR),
            'pATH'
        );

        if (combinedPath) {
            RtlCopyMemory(combinedPath, KeyPath.Buffer, KeyPath.Length);
            RtlStringCbCopyW(combinedPath + (KeyPath.Length / sizeof(WCHAR)),
                combinedLength - KeyPath.Length,
                L"\\(Default)");

            FullValuePath->Buffer = combinedPath;
            FullValuePath->Length = (USHORT)(KeyPath.Length + sizeof(WCHAR) * 9); // "\\(Default)"
            FullValuePath->MaximumLength = (USHORT)(combinedLength + sizeof(WCHAR));
        }
        else {
            status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else {
        // Обычное имя значения
        combinedLength = KeyPath.Length + sizeof(WCHAR) + ValueName.Length;
        combinedPath = (PWSTR)ExAllocatePoolZero(
            NonPagedPoolNx,
            combinedLength + sizeof(WCHAR),
            'pATH'
        );

        if (combinedPath) {
            // Копируем путь к ключу
            RtlCopyMemory(combinedPath, KeyPath.Buffer, KeyPath.Length);

            // Добавляем разделитель "\"
            combinedPath[KeyPath.Length / sizeof(WCHAR)] = L'\\';

            // Добавляем имя значения
            RtlCopyMemory(
                (PUCHAR)combinedPath + KeyPath.Length + sizeof(WCHAR),
                ValueName.Buffer,
                ValueName.Length
            );

            FullValuePath->Buffer = combinedPath;
            FullValuePath->Length = (USHORT)combinedLength;
            FullValuePath->MaximumLength = (USHORT)(combinedLength + sizeof(WCHAR));
            FullValuePath->Buffer[FullValuePath->Length / sizeof(WCHAR)] = L'\0';

            status = STATUS_SUCCESS;
        }
        else {
            status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    // Освобождаем временные ресурсы
    FreeKeyPath(&KeyPath);
    FreeValueName(&ValueName);

    if (!NT_SUCCESS(status)) {
        Log(L"[-] GetFullValuePath: Failed to allocate combined path\n");
    }

    return status;
}


VOID FreeFullValuePath(
    _In_ PUNICODE_STRING FullValuePath
)
{
    if (FullValuePath->Buffer != NULL) {
        ExFreePoolWithTag(FullValuePath->Buffer, 'pATH');
        RtlZeroMemory(FullValuePath, sizeof(UNICODE_STRING));
    }
}

NTSTATUS
CallbackPreNotificationBlock(
    _In_ PCALLBACK_CONTEXT CallbackCtx,
    _In_ REG_NOTIFY_CLASS NotifyClass,
    _Inout_ PVOID Argument2
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    UNICODE_STRING KeyPath = { 0 };
    UNICODE_STRING FullValuePath = { 0 };
    UNICODE_STRING ProcessName = { 0 };
    WCHAR procName[16] = { 0 };

    // Получаем имя текущего процесса (ANSI -> UTF-16)
    Status = GetCurrentProcessNameW(procName, RTL_NUMBER_OF(procName));
    if (!NT_SUCCESS(Status)) {
        Log(L"[-] CallbackPreNotificationBlock: GetCurrentProcessNameW failed: return STATUS_SUCCESS\n");
        return STATUS_SUCCESS; // если не удалось получить имя – не блокируем
    }
    RtlInitUnicodeString(&ProcessName, procName);

    Status = STATUS_SUCCESS;

    BOOLEAN testexe = FALSE;
    UNICODE_STRING testexeName;
    RtlInitUnicodeString(&testexeName, L"test.exe");
    if (RtlEqualUnicodeString(&ProcessName, &testexeName, TRUE)) {
        testexe = TRUE;
    }

    switch (NotifyClass) {
    case RegNtPreCreateKeyEx:
    case RegNtPreOpenKeyEx: {
        Status = GetFullKeyPathForCreateOpen(CallbackCtx, NotifyClass, Argument2, &KeyPath);
        if (NT_SUCCESS(Status)) {
            if (testexe) {
                Log(L"[*] Callback: Altitude-%s, NotifyClass-%s.\n",
                    CallbackCtx->AltitudeBuffer,
                    GetNotifyClassString(NotifyClass));
                Log(L"[*] check:\n\tReg:  %wZ\n\tProc: %wZ\n", &KeyPath, &ProcessName);
            }
            if (!IsAccessed(&ProcessName, &KeyPath, &Rules)) {
                Log(L"[+] CallbackPreNotificationBlock: operation blocked\n");
                Status = STATUS_ACCESS_DENIED;
            }
            FreeKeyPath(&KeyPath);
        }
        else {
            Log(L"[-] CallbackPreNotificationBlock: GetFullKeyPathForCreateOpen failed\n");
        }
        break;
    }
    case RegNtPreDeleteKey: {
        PREG_DELETE_KEY_INFORMATION info = (PREG_DELETE_KEY_INFORMATION)Argument2;
        Status = GetFullKeyPathFromObject(CallbackCtx, info->Object, &KeyPath);
        if (NT_SUCCESS(Status)) {
            if (testexe) {
                Log(L"[*] Callback: Altitude-%s, NotifyClass-%s.\n",
                    CallbackCtx->AltitudeBuffer,
                    GetNotifyClassString(NotifyClass));
                Log(L"[*] check:\n\tReg:  %wZ\n\tProc: %wZ\n", &KeyPath, &ProcessName);
            }
            if (!IsAccessed(&ProcessName, &KeyPath, &Rules)) {
                Log(L"[+] CallbackPreNotificationBlock: operation blocked\n");
                Status = STATUS_ACCESS_DENIED;
            }
            FreeKeyPath(&KeyPath);
        }
        else {
            Log(L"[-] CallbackPreNotificationBlock: GetFullKeyPathFromObject failed\n");
        }
        break;
    }

    case RegNtPreSetValueKey:
    case RegNtPreDeleteValueKey:
    case RegNtPreQueryValueKey: {
        Status = GetFullValuePath(CallbackCtx, NotifyClass, Argument2, &FullValuePath);
        if (!NT_SUCCESS(Status)) {
            Log(L"[-] CallbackPreNotificationBlock: GetFullValuePath failed (0x%08X)\n", Status);
            break;
        }

        if (testexe) {
            Log(L"[*] Callback: Altitude-%s, NotifyClass-%s.\n",
                CallbackCtx->AltitudeBuffer,
                GetNotifyClassString(NotifyClass));
            Log(L"[*] check:\n\tReg:  %wZ\n\tProc: %wZ\n", &FullValuePath, &ProcessName);
        }

        // Проверяем доступ по полному пути значения
        if (!IsAccessed(&ProcessName, &FullValuePath, &Rules)) {
            Log(L"[+] CallbackPreNotificationBlock: operation blocked\n");
            Status = STATUS_ACCESS_DENIED;
        }

        FreeFullValuePath(&FullValuePath);
        break;
    }

    default:
        //Log(L"[*] CallbackPreNotificationBlock: miss untracked NotifyClass: %d\n", NotifyClass);
        break;
    }

    return Status;
}

