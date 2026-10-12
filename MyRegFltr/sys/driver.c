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

//
// Rules
//
RULES_DATA Rules;

//
// context of CallbackPreNotificationBlock
//
PCALLBACK_CONTEXT CallbackPreNotificationCtx;


DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD     DeviceUnload;

_Dispatch_type_(IRP_MJ_CREATE)         DRIVER_DISPATCH DeviceCreate;
_Dispatch_type_(IRP_MJ_CLOSE)          DRIVER_DISPATCH DeviceClose;
_Dispatch_type_(IRP_MJ_CLEANUP)        DRIVER_DISPATCH DeviceCleanup;
_Dispatch_type_(IRP_MJ_DEVICE_CONTROL) DRIVER_DISPATCH DeviceControl;

//
// Pointer to the device object used to register registry callbacks
//
PDEVICE_OBJECT g_DeviceObj;

//
// Registry callback version
//
ULONG g_MajorVersion;
ULONG g_MinorVersion;

//
// Set to TRUE if TM and RM were successfully created and the transaction
// callback was successfully enabled. 
//
//BOOLEAN g_RMCreated;


//
// OS version globals initialized in driver entry 
//

BOOLEAN g_IsWin8OrGreater = FALSE;

VOID
DetectOSVersion()
/*++

Routine Description:

    This routine determines the OS version and initializes some globals used
    in the sample.

Arguments:

    None

Return value:

    None. On failure, global variables stay at default value

--*/
{

    RTL_OSVERSIONINFOEXW VersionInfo = { 0 };
    NTSTATUS Status;
    ULONGLONG ConditionMask = 0;

    //
    // Set VersionInfo to Win7's version number and then use
    // RtlVerifVersionInfo to see if this is win8 or greater.
    //

    VersionInfo.dwOSVersionInfoSize = sizeof(VersionInfo);
    VersionInfo.dwMajorVersion = 6;
    VersionInfo.dwMinorVersion = 1;

    VER_SET_CONDITION(ConditionMask, VER_MAJORVERSION, VER_LESS_EQUAL);
    VER_SET_CONDITION(ConditionMask, VER_MINORVERSION, VER_LESS_EQUAL);



    Status = RtlVerifyVersionInfo(&VersionInfo,
        VER_MAJORVERSION | VER_MINORVERSION,
        ConditionMask);
    if (NT_SUCCESS(Status)) {
        g_IsWin8OrGreater = FALSE;
        //InfoPrint("DetectOSVersion: This machine is running Windows 7 or an older OS.");
    }
    else if (Status == STATUS_REVISION_MISMATCH) {
        g_IsWin8OrGreater = TRUE;
        //InfoPrint("DetectOSVersion: This machine is running Windows 8 or a newer OS.");
    }
    else {
        //ErrorPrint("RtlVerifyVersionInfo returned unexpected error status 0x%x.",
            //Status);

        //
        // default action is to assume this is not win8
        //
        g_IsWin8OrGreater = FALSE;
    }

}



NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
/*++

Routine Description:

    This routine is called by the operating system to initialize the driver.
    It allocates a device object, initializes the supported Io callbacks, and
    creates a symlink to make the device accessible to Win32.

Arguments:

    DriverObject - Supplies the system control object for this test driver.

    RegistryPath - The string location of the driver's corresponding services
                   key in the registry.

Return value:

    Success or appropriate failure code.

--*/
{
    LogStart();
    Log(L"[*] DriverEntry() called\n");

    NTSTATUS Status;
    UNICODE_STRING NtDeviceName;
    UNICODE_STRING DosDevicesLinkName;
    UNICODE_STRING DeviceSDDLString;

    UNREFERENCED_PARAMETER(RegistryPath);


    //
    //  Default to NonPagedPoolNx for non paged pool allocations where supported.
    //

    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);

    //
    // Create our device object.
    //

    RtlInitUnicodeString(&NtDeviceName, NT_DEVICE_NAME);
    RtlInitUnicodeString(&DeviceSDDLString, DEVICE_SDDL);

    Status = IoCreateDeviceSecure(
        DriverObject,                 // pointer to driver object
        0,                            // device extension size
        &NtDeviceName,                // device name
        FILE_DEVICE_UNKNOWN,          // device type
        0,                            // device characteristics
        TRUE,                         // not exclusive
        &DeviceSDDLString,            // SDDL string specifying access
        NULL,                         // device class guid
        &g_DeviceObj);                // returned device object pointer

    if (!NT_SUCCESS(Status)) {
        Log(L"[-] DriverEntry: IoCreateDeviceSecure failed\n");
        return Status;
    }

    //
    // Set dispatch routines.
    //

    DriverObject->MajorFunction[IRP_MJ_CREATE] = DeviceCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DeviceClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = DeviceCleanup;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceControl;
    DriverObject->DriverUnload = DeviceUnload;

    //
    // Create a link in the Win32 namespace.
    //

    RtlInitUnicodeString(&DosDevicesLinkName, DOS_DEVICES_LINK_NAME);

    Status = IoCreateSymbolicLink(&DosDevicesLinkName, &NtDeviceName);

    if (!NT_SUCCESS(Status)) {
        IoDeleteDevice(DriverObject->DeviceObject);
        Log(L"[-] DriverEntry: IoCreateSymbolicLink failed\n");
        return Status;
    }

    ExInitializeFastMutex(&g_RulesMutex);

    Status = CreateRules(&Rules);

    if (!NT_SUCCESS(Status)) {
        Log(L"[-] DriverEntry: CreateRules failed\n");
    }

    Status = LoadRules(&Rules);

    if (!NT_SUCCESS(Status)) {
        Log(L"[-] DriverEntry: LoadRules failed\n");
    }

    PrintRulesData(&Rules);

    //
    // Get callback version.
    //

    //CmGetCallbackVersion(&g_MajorVersion, &g_MinorVersion);
    //Log(L"[*] Callback version %u.%u\n", g_MajorVersion, g_MinorVersion);

    //
    // Some variations depend on knowing if the OS is win8 or above
    //

    //DetectOSVersion();

    Status = RegesterCallbackPreNotificationBlock(&CallbackPreNotificationCtx);
    if (!NT_SUCCESS(Status)) {
        Log(L"[-] DriveEntry: RegesterCallbackPreNotificationBlock failed\n");
        return Status;
    }

    Log(L"[*] DriveEntry: irql: %u\n", (unsigned int)KeGetCurrentIrql());


    Log(L"[+] DriveEntry succeeded\n");
    return STATUS_SUCCESS;

}



NTSTATUS
DeviceCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
)
/*++

Routine Description:

    Dispatches file create requests.

Arguments:

    DeviceObject - The device object receiving the request.

    Irp - The request packet.

Return Value:

    STATUS_NOT_IMPLEMENTED

--*/
{
    Log(L"[*] DeviceCreate called\n");
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    Log(L"[+] DeviceCreate succeeded\n");

    return STATUS_SUCCESS;
}



NTSTATUS
DeviceClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
)
/*++

Routine Description:

    Dispatches close requests.

Arguments:

    DeviceObject - The device object receiving the request.

    Irp - The request packet.

Return Value:

    STATUS_SUCCESS

--*/
{
    Log(L"[*] DeviceClose called\n");
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    Log(L"[+] DeviceClose succeeded\n");

    return STATUS_SUCCESS;
}



NTSTATUS
DeviceCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
)
/*++

Routine Description:

    Dispatches cleanup requests.  Does nothing right now.

Arguments:

    DeviceObject - The device object receiving the request.

    Irp - The request packet.

Return Value:

    STATUS_SUCCESS

--*/
{
    Log(L"[*] DeviceCleanup called\n");
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    Log(L"[+] DeviceCleanup succeeded\n");

    return STATUS_SUCCESS;
}



NTSTATUS
DeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
)
/*++

Routine Description:

    Dispatches ioctl requests.

Arguments:

    DeviceObject - The device object receiving the request.

    Irp - The request packet.

Return Value:

    Status returned from the method called.

--*/
{
    Log(L"[*] DeviceControl called\n");

    if (Irp == NULL) {
        Log(L"[-] DeviceControl: Irp == NULL\n");
        return STATUS_INVALID_PARAMETER;
    }

    PIO_STACK_LOCATION IrpStack;
    ULONG Ioctl;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    Status = STATUS_SUCCESS;

    IrpStack = IoGetCurrentIrpStackLocation(Irp); 

    // check that it is really an IRP_MJ_DEVICE_CONTROL
    if (IrpStack->MajorFunction != IRP_MJ_DEVICE_CONTROL) {
        Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        Log(L"[-] DeviceControl: IrpStack->MajorFunction != IRP_MJ_DEVICE_CONTROL\n");
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Ioctl = IrpStack->Parameters.DeviceIoControl.IoControlCode;

    switch (Ioctl)
    {
    case IOCTL_RULES_UPDATE:
        Log(L"[*] DeviceControl: IOCTL_RULES_UPDATE received\n");
        Status = Update(DeviceObject, Irp);
        break;

    case IOCTL_REGISTER_CALLBACK_ON_THREAD_CREATING:
        Log(L"[*] DeviceControl: IOCTL_REGISTER_CALLBACK_ON_THREAD_CREATING received !\n");
        Status = RegisterCallbackOnThreadCreating(DeviceObject, Irp);
        break;

    case IOCTL_UNREGISTER_CALLBACK_ON_THREAD_CREATING:
        Log(L"[*] DeviceControl: IOCTL_UNREGISTER_CALLBACK_ON_THREAD_CREATING received\n");
        Status = UnRegisterCallbackOnThreadCreating(DeviceObject, Irp);
        break;

    default:
        Log(L"[-] DeviceControl: Unrecognized ioctl code 0x%x", Ioctl);
    }

    //
    // Complete the irp and return.
    //

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    
    Log(L"[*] DeviceControl finished\n");

    return Status;

}


VOID
DeviceUnload(
    _In_ PDRIVER_OBJECT DriverObject
)
/*++

Routine Description:

    Cleans up any driver-level allocations and prepares for unload. All
    this driver needs to do is to delete the device object and the
    symbolic link between our device name and the Win32 visible name.

Arguments:

    DeviceObject - The device object receiving the request.

    Irp - The request packet.

Return Value:

    STATUS_NOT_IMPLEMENTED

--*/
{
    Log(L"[*] DeviceUnload called\n");

    NTSTATUS Status;

    if (ThreadCallbackRegistered) {
        Status = PsRemoveCreateThreadNotifyRoutine(CreateThreadNotifyRoutine);
        if (!NT_SUCCESS(Status)) {
            ThreadCallbackRegistered = TRUE;
            Log(L"[-] Device Control: PsRemoveCreateThreadNotifyRoutine failed: 0x%X\n", Status);
        }
        else {
            ThreadCallbackRegistered = FALSE;
        }
    }

    Status = UnRegesterCallbackPreNotificationBlock(CallbackPreNotificationCtx);
    if (!NT_SUCCESS(Status)) {
        Log(L"[-] DeviceUnload: UnRegesterCallbackPreNotificationBlock failed\n");
    }

    //
    // Delete the link from our device name to a name in the Win32 namespace.
    //
    UNICODE_STRING  DosDevicesLinkName;
    RtlInitUnicodeString(&DosDevicesLinkName, DOS_DEVICES_LINK_NAME);

    Log(L"[*] DeviceUnload: IoDeleteSymbolicLink: [%wZ]\n", &DosDevicesLinkName);
    
    Status = IoDeleteSymbolicLink(&DosDevicesLinkName);
    if (!NT_SUCCESS(Status)) {
        Log(L"[-] DeviceUnload: IoDeleteSymbolicLink failed\n");
    }

    IoDeleteDevice(DriverObject->DeviceObject);

    Log(L"[*] DeviceUnload: IoDeleteDevice succeded\n");
    Log(L"[*] DeviceUnload succeeded\n");

    LogStop();
}

