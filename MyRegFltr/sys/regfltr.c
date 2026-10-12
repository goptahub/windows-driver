/*++
Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    regfltr.c

Abstract: 

    Sample driver used to run the kernel mode registry callback samples.

Environment:

    Kernel mode only

--*/

#include "regfltr.h"
#include "threadlog.h"
#include "log.h"

LPCWSTR
GetNotifyClassString(
    _In_ REG_NOTIFY_CLASS NotifyClass
);


NTSTATUS 
Callback (
    _In_     PVOID CallbackContext,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2
)
/*++

Routine Description:

    This is the registry callback we'll register to intercept all registry
    operations. 

Arguments:

    CallbackContext - The value that the driver passed to the Context parameter
        of CmRegisterCallbackEx when it registers this callback routine.

    Argument1 - A REG_NOTIFY_CLASS typed value that identifies the type of 
        registry operation that is being performed and whether the callback
        is being called in the pre or post phase of processing.

    Argument2 - A pointer to a structure that contains information specific
        to the type of the registry operation. The structure type depends
        on the REG_NOTIFY_CLASS value of Argument1. Refer to MSDN for the 
        mapping from REG_NOTIFY_CLASS to REG_XXX_KEY_INFORMATION.

Return Value:

    Status returned from the helper callback routine or STATUS_SUCCESS if
    the registry operation did not originate from this process.

--*/
{
    //Log(L"[*] Callback called\n");

    KIRQL irql = KeGetCurrentIrql();

    if (irql != PASSIVE_LEVEL) {
        /*do not log whatever using ZwCreateFile as ZwCreateFile is forbidden IRQL > PASSIVE_LEVEL and causes SYSTEM_SERVICE_EXCEPTION*/
        return STATUS_SUCCESS;
    }

    NTSTATUS Status = STATUS_SUCCESS;
    REG_NOTIFY_CLASS NotifyClass;
    PCALLBACK_CONTEXT CallbackCtx;

    CallbackCtx = (PCALLBACK_CONTEXT)CallbackContext;
    if (CallbackCtx == NULL) {
        Log(L"[-] Callback: CallbackCtx == NULL failed\n");
        return STATUS_SUCCESS;
    }

    NotifyClass = (REG_NOTIFY_CLASS)(ULONG_PTR)Argument1;

    if (CallbackCtx->ProcessId == PsGetCurrentProcessId()) {
        /*Log(L"[*] Callback: improper ProcessId\n");*/
        return STATUS_SUCCESS;
    }

    /*Log(L"[*] Callback: Altitude-%s, NotifyClass-%s.\n",
        CallbackCtx->AltitudeBuffer,
        GetNotifyClassString(NotifyClass));*/


    if (Argument2 == NULL) {

        //
        // This should never happen but the SAL annotation on the callback 
        // function marks Argument 2 as opt and is looser than what 
        // it actually is.
        //
        Log(L"[-] Callback: Argument2 == NULL\n");
        return STATUS_SUCCESS;
    }
    
    switch (CallbackCtx->CallbackMode) {
    case CALLBACK_MODE_PRE_NOTIFICATION_BLOCK:
        Status = CallbackPreNotificationBlock(CallbackCtx, NotifyClass, Argument2);
        break;
    default:
        Status = STATUS_INVALID_PARAMETER;
        Log(L"[-] Callback: unknown CallbackMode\n");
        break;
    }


    //Log(L"[*] Callback finished\n");

    return Status;
    
}



NTSTATUS 
Update(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )
/*++

Routine Description:

    This routine creates the root test key and then invokes the sample.
    It records the results of each sample in an array that it returns to
    the usermode program.

Arguments:

    DeviceObject - The device object receiving the request.

    Irp - The request packet.
    
Return Value:

    NTSTATUS

--*/
{
    Log(L"[*] Update called\n");

    NTSTATUS Status = STATUS_SUCCESS;
    PIO_STACK_LOCATION IrpStack;
    ULONG OutputBufferLength;

    typedef INT output_t;
    output_t* Output;

    UNREFERENCED_PARAMETER(DeviceObject);

    //
    // Get the output buffer from the irp and check it is as large as expected.
    //
    
    IrpStack = IoGetCurrentIrpStackLocation(Irp);

    OutputBufferLength = IrpStack->Parameters.DeviceIoControl.OutputBufferLength;

    if (OutputBufferLength < sizeof (output_t)) {
        Status = STATUS_INVALID_PARAMETER;
        Log(L"[-] Update: IoGetCurrentIrpStackLocation failed\n");
        return NT_SUCCESS(Status);
    }

    Output = (output_t*) Irp->AssociatedIrp.SystemBuffer;

    //
    // Clean up test keys in case the sample terminated uncleanly.
    //


    Status = CreateRules(&Rules);

    if (!NT_SUCCESS(Status)) {
        Log(L"[-] DriverEntry: CreateRules failed\n");
    }

    Status = LoadRules(&Rules);

    if (NT_SUCCESS(Status)) {
        *Output = 1;
        Status = STATUS_SUCCESS;
        Log(L"[*] Update succeeded\n");
    }
    else {
        *Output = 0;
        Log(L"[-] Update: LoadRules failed\n");
    }

    //ExReleaseFastMutex(&g_RulesMutex);

    PrintRulesData(&Rules);

    Irp->IoStatus.Information = sizeof(output_t);
    
    return Status;
}

LPCWSTR
GetNotifyClassString(
    _In_ REG_NOTIFY_CLASS NotifyClass
)
/*++

Routine Description:

    Converts from NotifyClass to a string

Arguments:

    NotifyClass - value that identifies the type of registry operation that
        is being performed

Return Value:

    Returns a string of the name of NotifyClass.

--*/
{
    switch (NotifyClass) {
    case RegNtPreDeleteKey:                 return L"RegNtPreDeleteKey";
    case RegNtPreSetValueKey:               return L"RegNtPreSetValueKey";
    case RegNtPreDeleteValueKey:            return L"RegNtPreDeleteValueKey";
    case RegNtPreSetInformationKey:         return L"RegNtPreSetInformationKey";
    case RegNtPreRenameKey:                 return L"RegNtPreRenameKey";
    case RegNtPreEnumerateKey:              return L"RegNtPreEnumerateKey";
    case RegNtPreEnumerateValueKey:         return L"RegNtPreEnumerateValueKey";
    case RegNtPreQueryKey:                  return L"RegNtPreQueryKey";
    case RegNtPreQueryValueKey:             return L"RegNtPreQueryValueKey";
    case RegNtPreQueryMultipleValueKey:     return L"RegNtPreQueryMultipleValueKey";
    case RegNtPreKeyHandleClose:            return L"RegNtPreKeyHandleClose";
    case RegNtPreCreateKeyEx:               return L"RegNtPreCreateKeyEx";
    case RegNtPreOpenKeyEx:                 return L"RegNtPreOpenKeyEx";
    case RegNtPreFlushKey:                  return L"RegNtPreFlushKey";
    case RegNtPreLoadKey:                   return L"RegNtPreLoadKey";
    case RegNtPreUnLoadKey:                 return L"RegNtPreUnLoadKey";
    case RegNtPreQueryKeySecurity:          return L"RegNtPreQueryKeySecurity";
    case RegNtPreSetKeySecurity:            return L"RegNtPreSetKeySecurity";
    case RegNtPreRestoreKey:                return L"RegNtPreRestoreKey";
    case RegNtPreSaveKey:                   return L"RegNtPreSaveKey";
    case RegNtPreReplaceKey:                return L"RegNtPreReplaceKey";

    case RegNtPostDeleteKey:                return L"RegNtPostDeleteKey";
    case RegNtPostSetValueKey:              return L"RegNtPostSetValueKey";
    case RegNtPostDeleteValueKey:           return L"RegNtPostDeleteValueKey";
    case RegNtPostSetInformationKey:        return L"RegNtPostSetInformationKey";
    case RegNtPostRenameKey:                return L"RegNtPostRenameKey";
    case RegNtPostEnumerateKey:             return L"RegNtPostEnumerateKey";
    case RegNtPostEnumerateValueKey:        return L"RegNtPostEnumerateValueKey";
    case RegNtPostQueryKey:                 return L"RegNtPostQueryKey";
    case RegNtPostQueryValueKey:            return L"RegNtPostQueryValueKey";
    case RegNtPostQueryMultipleValueKey:    return L"RegNtPostQueryMultipleValueKey";
    case RegNtPostKeyHandleClose:           return L"RegNtPostKeyHandleClose";
    case RegNtPostCreateKeyEx:              return L"RegNtPostCreateKeyEx";
    case RegNtPostOpenKeyEx:                return L"RegNtPostOpenKeyEx";
    case RegNtPostFlushKey:                 return L"RegNtPostFlushKey";
    case RegNtPostLoadKey:                  return L"RegNtPostLoadKey";
    case RegNtPostUnLoadKey:                return L"RegNtPostUnLoadKey";
    case RegNtPostQueryKeySecurity:         return L"RegNtPostQueryKeySecurity";
    case RegNtPostSetKeySecurity:           return L"RegNtPostSetKeySecurity";
    case RegNtPostRestoreKey:               return L"RegNtPostRestoreKey";
    case RegNtPostSaveKey:                  return L"RegNtPostSaveKey";
    case RegNtPostReplaceKey:               return L"RegNtPostReplaceKey";

    case RegNtCallbackObjectContextCleanup: return L"RegNtCallbackObjectContextCleanup";

    default:
        return L"Unsupported REG_NOTIFY_CLASS";
    }
}


NTSTATUS RegisterCallbackOnThreadCreating(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PIO_STACK_LOCATION IrpStack;
    ULONG OutputBufferLength;

    typedef INT output_t;
    output_t* Output;

    UNREFERENCED_PARAMETER(DeviceObject);

    IrpStack = IoGetCurrentIrpStackLocation(Irp);

    OutputBufferLength = IrpStack->Parameters.DeviceIoControl.OutputBufferLength;

    if (OutputBufferLength < sizeof(output_t)) {
        Status = STATUS_INVALID_PARAMETER;
        Log(L"[-] RegisterCallbackOnThreadCreating: IoGetCurrentIrpStackLocation failed\n");
        return Status;
    }

    Output = (output_t*)Irp->AssociatedIrp.SystemBuffer;

    if (!ThreadCallbackRegistered) {
        Status = PsSetCreateThreadNotifyRoutine(CreateThreadNotifyRoutine);
        if (NT_SUCCESS(Status)) {
            ThreadCallbackRegistered = TRUE;
            *Output = 1;
            Status = STATUS_SUCCESS;
            Log(L"[+] RegisterCallbackOnThreadCreating succeeded\n");
        }
        else {
            ThreadCallbackRegistered = FALSE;
            *Output = 0;
            Log(L"[-] RegisterCallbackOnThreadCreating: PsSetCreateThreadNotifyRoutine failed: 0x%X\n", Status);
        }
    }
    else {
        ThreadCallbackRegistered = FALSE;
        *Output = 0;
        Log(L"[-] RegisterCallbackOnThreadCreating: CreateThreadNotifyRoutine is already registered\n");
    }

    return Status;
}

NTSTATUS UnRegisterCallbackOnThreadCreating(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PIO_STACK_LOCATION IrpStack;
    ULONG OutputBufferLength;

    typedef INT output_t;
    output_t* Output;

    UNREFERENCED_PARAMETER(DeviceObject);

    IrpStack = IoGetCurrentIrpStackLocation(Irp);

    OutputBufferLength = IrpStack->Parameters.DeviceIoControl.OutputBufferLength;

    if (OutputBufferLength < sizeof(output_t)) {
        Status = STATUS_INVALID_PARAMETER;
        Log(L"[-] UnRegisterCallbackOnThreadCreating: IoGetCurrentIrpStackLocation failed\n");
        return Status;
    }

    Output = (output_t*)Irp->AssociatedIrp.SystemBuffer;

    if (ThreadCallbackRegistered) {
        Status = PsRemoveCreateThreadNotifyRoutine(CreateThreadNotifyRoutine);
        if (NT_SUCCESS(Status)) {
            ThreadCallbackRegistered = FALSE;
            *Output = 1;
            Status = STATUS_SUCCESS;
            Log(L"[+] UnRegisterCallbackOnThreadCreating succeeded\n");
        }
        else {
            ThreadCallbackRegistered = TRUE;
            *Output = 0;
            Log(L"[-] UnRegisterCallbackOnThreadCreating: PsRemoveCreateThreadNotifyRoutine failed: 0x%X\n", Status);
        }
    }
    else {
        ThreadCallbackRegistered = TRUE;
        *Output = 0;
        Log(L"[-] UnRegisterCallbackOnThreadCreating: CreateThreadNotifyRoutine is not registered yet\n");
    }

    return Status;
}
