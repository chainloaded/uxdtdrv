#include "UtMain.h"
#include "UtDeviceControl.h"

PDEVICE_OBJECT g_UtDevice;

NTSTATUS _Function_class_(DRIVER_DISPATCH) UtDispatch(
    _Inout_ PDEVICE_OBJECT  DeviceObject,
    _Inout_ PIRP            Request
)
/*++

    Routine Description:
        Generic IOCTL dispatch entry point.

    Arguments:

        DeviceObject:
            Supplies a pointer to the device object
            the request refers to.

        Request:
            The request.

    Return Value:

        NTSTATUS indicating success or failure.

--*/
{
    NTSTATUS            Result;
    PIO_STACK_LOCATION  StackLocation   = NULL;
    PUT_STREAM_CONTEXT  StreamContext   = NULL;

    Result          = STATUS_INVALID_DEVICE_REQUEST;
    StackLocation   = IoGetCurrentIrpStackLocation(Request);

    Request->IoStatus.Information = 0;

    // Check if this is one of the supported major codes
    if (StackLocation->MajorFunction == IRP_MJ_CREATE)
    {
        // Allocate context state
        StackLocation->FileObject->FsContext = ExAllocatePool2(
            POOL_FLAG_NON_PAGED,
            sizeof(UT_STEAM_CONTEXT),
            UT_TAG_MAIN
        );

        if (!StackLocation->FileObject->FsContext)
        {
            Result = STATUS_NO_MEMORY;
            goto Done;
        }

        StreamContext = StackLocation->FileObject->FsContext;

        // Initialize push lock
        ExInitializePushLock(&StreamContext->IoCtlPushLock);

        Result = STATUS_SUCCESS;
        goto Done;
    }

    if (StackLocation->MajorFunction == IRP_MJ_CLEANUP)
    {
        StreamContext = StackLocation->FileObject->FsContext;

        if (StreamContext)
        {
            ExAcquirePushLockExclusive(&StreamContext->IoCtlPushLock);

            // Signal that worker threads need to finish
            StreamContext->IsClosing = TRUE;

            if (StreamContext->Initialized)
            {
                // Wait on the threads to finish
                ZwWaitForSingleObject(StreamContext->DmaWorker, FALSE, NULL);
                ZwWaitForSingleObject(StreamContext->UxdtWorker, FALSE, NULL);

                // Close worker threads
                ZwClose(StreamContext->DmaWorker);
                ZwClose(StreamContext->UxdtWorker);

                // Close KS pin handle
                ZwClose(StreamContext->PinHandle);

                // Unmap the client buffer
                MmUnmapLockedPages(StreamContext->ClientBuffer, StreamContext->ClientBufferMdl);
            }

            ExReleasePushLockExclusive(&StreamContext->IoCtlPushLock);
        }

        Result = STATUS_SUCCESS;
        goto Done;
    }

    if (StackLocation->MajorFunction == IRP_MJ_CLOSE)
    {
        StreamContext = StackLocation->FileObject->FsContext;

        if (StreamContext && StreamContext->Initialized)
        {
            // Dereference the notification event
            ObDereferenceObject(StreamContext->NotificationEvent);

            // Free the MDL
            IoFreeMdl(StreamContext->ClientBufferMdl);

            // Free the underlying client buffer
            ExFreePoolWithTag(StreamContext->ClientBufferKernel, UT_TAG_MAIN);

            // Free the ring buffers
            ExFreePoolWithTag(StreamContext->SharedRingBuffer[0], UT_TAG_MAIN);
            ExFreePoolWithTag(StreamContext->SharedRingBuffer[1], UT_TAG_MAIN);

            // Free the context
            ExFreePoolWithTag(StreamContext, UT_TAG_MAIN);
        }

        Result = STATUS_SUCCESS;
        goto Done;
    }

    // Is this a device IO control?
    if (StackLocation->MajorFunction == IRP_MJ_DEVICE_CONTROL)
    {
        switch (StackLocation->Parameters.DeviceIoControl.IoControlCode)
        {

        case IOCTL_UT_RECEIVE:

            Result = UtIoctlHandleReceive(Request, StackLocation);
            break;

        default:
            break;
        }
    }

Done:

    // Complete the request
    Request->IoStatus.Status = Result;
    IoCompleteRequest(Request, IO_NO_INCREMENT);

    return Result;
}

VOID _Function_class_(DRIVER_UNLOAD) UtUnload(
    _In_ PDRIVER_OBJECT DriverObject
)
/*++

    Routine Description:
        Called when the driver gets unloaded.

    Arguments:

        DriverObject:
            Supplies a pointer to the driver object.

    Return Value:

        None.

--*/
{
    // Delete the device
    IoDeleteDevice(g_UtDevice);
}

NTSTATUS UtEntry(
    _Inout_ PDRIVER_OBJECT  DriverObject,
    _In_    PUNICODE_STRING ServicePath
)
/*++

    Routine Description:
        This routine is the entry point for the driver.

    Arguments:

        DriverObject:
            Supplies a pointer to the driver object.

        RegistryPath:
            Supplies a pointer to the driver-specific registry path.

    Return Value:

        STATUS_SUCCESS if initialization succeeded, otherwise an error
        NTSTATUS value.

--*/
{
    NTSTATUS        Result;
    UNICODE_STRING  DeviceName;

    // Initialize device name
    RtlInitUnicodeString(&DeviceName, L"\\Device\\UXDTDRV");

    // Create a device
    Result = IoCreateDevice(
        DriverObject,
        0,
        &DeviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &g_UtDevice
    );

    if (!NT_SUCCESS(Result))
        goto Done;

    // Setup dispatch routines
    DriverObject->MajorFunction[IRP_MJ_CLOSE]           = UtDispatch;
    DriverObject->MajorFunction[IRP_MJ_CREATE]          = UtDispatch;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL]  = UtDispatch;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP]         = UtDispatch;

    // Set unload routine
    DriverObject->DriverUnload = UtUnload;

Done:

    return Result;
}
