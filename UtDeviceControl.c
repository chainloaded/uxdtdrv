#include "UtDeviceControl.h"
#include "UtAudioHelper.h"
#include "UtWorkers.h"
#include "UtKsPin.h"

NTSTATUS UtIoctlHandleReceive(
    _Inout_ PIRP                Request,
    _In_    PIO_STACK_LOCATION  StackLocation
)
/*++

    Routine Description:
        Sets up a receive buffer for the UXDT client.

    Arguments:

        Request:
            Pointer to the client request.

        StackLocation:
            Pointer to the stack location.

    Return Value:

        NTSTATUS indicating success or failure.

--*/
{
    NTSTATUS                        Result;
    HANDLE                          MicrophoneDevice    = NULL;
    HANDLE                          PinHandle           = NULL;
    PUT_RECEIVE                     ReceiveParams       = NULL;
    PVOID                           ClientBuffer        = NULL;
    PVOID                           ClientBufferMapping = NULL;
    PVOID                           RingBufferOne       = NULL;
    PVOID                           RingBufferTwo       = NULL;
    PMDL                            ClientBufferMdl     = NULL;
    PUT_STREAM_CONTEXT              StreamContext       = NULL;
    PKEVENT                         NotificationEvent   = NULL;
    KSRTAUDIO_BUFFER                DmaBuffer           = { 0 };
    KSRTAUDIO_HWREGISTER            HwRegister          = { 0 };
    KSRTAUDIO_BUFFER_PROPERTY       RtProperty          = { 0 };
    KSRTAUDIO_HWREGISTER_PROPERTY   HwRegProperty       = { 0 };
    KSRTAUDIO_HWLATENCY             HwLatency           = { 0 };
    KSPROPERTY                      Property            = { 0 };
    UINT32                          AudioBufferSize     = 0;
    UINT32                          PinID               = 0;

    // Get file context
    StreamContext = StackLocation->FileObject->FsContext;

    if (!StreamContext)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    // Acquire push lock
    ExAcquirePushLockExclusive(&StreamContext->IoCtlPushLock);

    if (StreamContext->Initialized || StreamContext->IsClosing)
    {
        // Stream was already initialized on this object
        Result = STATUS_INVALID_DEVICE_STATE;
        goto Done;
    }

    // Do length checks
    if (StackLocation->Parameters.DeviceIoControl.InputBufferLength < sizeof(UT_RECEIVE))
    {
        Result = STATUS_INVALID_PARAMETER;
        goto Done;
    }

    if (StackLocation->Parameters.DeviceIoControl.OutputBufferLength < sizeof(PVOID))
    {
        Result = STATUS_BUFFER_TOO_SMALL;
        goto Done;
    }

    // Retrieve the input buffer
    ReceiveParams = (PUT_RECEIVE)Request->AssociatedIrp.SystemBuffer;

    // Max user buffer size is 0x1000 for now
    if (ReceiveParams->OutputBufferLen > PAGE_SIZE || ReceiveParams->OutputBufferLen <= sizeof(UINT64))
    {
        Result = STATUS_INVALID_PARAMETER;
        goto Done;
    }

    // Try to find microphone device
    Result = UtAhFindMicrophone(&MicrophoneDevice, &PinID);
    
    if (!NT_SUCCESS(Result))
        goto Done;

    // Create microphone pin
    Result = UtKsConnectAudioPin(MicrophoneDevice, PinID, &PinHandle);

    if (!NT_SUCCESS(Result))
        goto Done;

    // Request the hardware latency with KSPROPERTY_RTAUDIO_HWLATENCY
    Property.Set    = KsPropsetId_RtAudio;
    Property.Id     = KSPROPERTY_RTAUDIO_HWLATENCY;
    Property.Flags  = KSPROPERTY_TYPE_GET;

    Result = UtKsQueryAudioPin(
        PinHandle,
        &Property,
        sizeof(KSPROPERTY),
        &HwLatency,
        sizeof(KSRTAUDIO_HWLATENCY)
    );

    if (!NT_SUCCESS(Result))
        goto Done;

    // Request the hardware registers with KSPROPERTY_RTAUDIO_HWREGISTER
    AudioBufferSize = (44100 * (2 * (16 / 8)) * 10) / 1000;     // 10ms buffer
    AudioBufferSize = (AudioBufferSize + 3) & ~3;               // Align to 4 bytes

    RtProperty.BaseAddress          = NULL;
    RtProperty.RequestedBufferSize  = AudioBufferSize * 4;
    RtProperty.Property.Set         = KsPropsetId_RtAudio;
    RtProperty.Property.Id          = KSPROPERTY_RTAUDIO_BUFFER;
    RtProperty.Property.Flags       = KSPROPERTY_TYPE_GET;

    Result = UtKsQueryAudioPin(
        PinHandle,
        &RtProperty,
        sizeof(KSRTAUDIO_BUFFER_PROPERTY),
        &DmaBuffer,
        sizeof(KSRTAUDIO_BUFFER)
    );

    if (!NT_SUCCESS(Result))
        goto Done;

    // Request the hardware registers with KSPROPERTY_RTAUDIO_HWREGISTER
    HwRegProperty.BaseAddress       = NULL;
    HwRegProperty.Property.Set      = KsPropsetId_RtAudio;
    HwRegProperty.Property.Id       = KSPROPERTY_RTAUDIO_POSITIONREGISTER;
    HwRegProperty.Property.Flags    = KSPROPERTY_TYPE_GET;

    Result = UtKsQueryAudioPin(
        PinHandle,
        &HwRegProperty,
        sizeof(KSRTAUDIO_HWREGISTER_PROPERTY),
        &HwRegister,
        sizeof(KSRTAUDIO_HWREGISTER)
    );

    if (!NT_SUCCESS(Result))
        goto Done;

    // Start the pin
    Result = UtKsSetAudioPinState(PinHandle, KSSTATE_RUN);

    if (!NT_SUCCESS(Result))
        goto Done;

    // Create the UXDT buffer
    ClientBuffer = ExAllocatePool2(
        POOL_FLAG_NON_PAGED, 
        ReceiveParams->OutputBufferLen, 
        UT_TAG_MAIN
    );

    if (ClientBuffer == NULL)
    {
        Result = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    // Allocate MDL for the client buffer
    ClientBufferMdl = IoAllocateMdl(
        ClientBuffer,
        ReceiveParams->OutputBufferLen,
        FALSE,
        FALSE,
        NULL
    );

    if (ClientBufferMdl == NULL)
    {
        Result = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    // Build the MDL
    MmBuildMdlForNonPagedPool(ClientBufferMdl);

    // Map the buffer for the client in user space
    __try
    {
        ClientBufferMapping = MmMapLockedPagesSpecifyCache(
            ClientBufferMdl,
            UserMode,
            MmCached,
            NULL,
            FALSE,
            NormalPagePriority
        );
    } 
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        Result = STATUS_ACCESS_VIOLATION;
        goto Done;
    }

    // Open user event
    Result = ObReferenceObjectByHandle(
        ReceiveParams->NotificationEvent,
        EVENT_MODIFY_STATE,
        *ExEventObjectType,
        UserMode,
        &NotificationEvent,
        NULL
    );

    if (!NT_SUCCESS(Result))
        goto Done;

    // Allocate ring buffers
    RingBufferOne = ExAllocatePool2(POOL_FLAG_PAGED, RING_BUFFER_SIZE, UT_TAG_MAIN);
    RingBufferTwo = ExAllocatePool2(POOL_FLAG_PAGED, RING_BUFFER_SIZE, UT_TAG_MAIN);

    if (!RingBufferOne || !RingBufferTwo)
    {
        Result = STATUS_NO_MEMORY;
        goto Done;
    }

    // Initialize the FS context
    StreamContext->Initialized          = TRUE;
    StreamContext->PinHandle            = PinHandle;
    StreamContext->NotificationEvent    = ReceiveParams->NotificationEvent;
    StreamContext->WaveRtBuffer         = DmaBuffer.BufferAddress;
    StreamContext->WaveRtBufferSize     = DmaBuffer.ActualBufferSize;
    StreamContext->ClientBuffer         = ClientBufferMapping;
    StreamContext->ClientBufferKernel   = ClientBuffer;
    StreamContext->ClientBufferSize     = ReceiveParams->OutputBufferLen;
    StreamContext->ClientBufferMdl      = ClientBufferMdl;
    StreamContext->HwRegister           = HwRegister.Register;
    StreamContext->HwRegisterSize       = HwRegister.Width;
    StreamContext->FifoSize             = HwLatency.FifoSize;
    StreamContext->ClientProcessId      = PsGetCurrentProcessId();

    StreamContext->SharedRingBuffer[0]  = RingBufferOne;
    StreamContext->SharedRingBuffer[1]  = RingBufferTwo;

    KeInitializeEvent(&StreamContext->BufferFilledEvent, SynchronizationEvent, FALSE);

    // Create worker threads
    Result = PsCreateSystemThread(
        &StreamContext->DmaWorker,
        THREAD_ALL_ACCESS,
        NULL,
        NtCurrentProcess(),
        NULL,
        UtWoDmaWorker,
        StreamContext
    );

    if (!NT_SUCCESS(Result))
    {
        StreamContext->Initialized = FALSE;
        goto Done;
    }

    Result = PsCreateSystemThread(
        &StreamContext->UxdtWorker,
        THREAD_ALL_ACCESS,
        NULL,
        NtCurrentProcess(),
        NULL,
        UtWoUxdtWorker,
        StreamContext
    );

    if (!NT_SUCCESS(Result))
    {
        StreamContext->Initialized = FALSE;

        ZwWaitForSingleObject(StreamContext->DmaWorker, FALSE, NULL);
        ZwClose(StreamContext->DmaWorker);
        goto Done;
    }

    Result                          = STATUS_SUCCESS;
    Request->IoStatus.Information   = sizeof(PVOID);

    // Return pointer to user mapping
    RtlCopyMemory(Request->AssociatedIrp.SystemBuffer, &ClientBufferMapping, sizeof(PVOID));

Done:

    if (!NT_SUCCESS(Result))
    {
        if (RingBufferOne)
            ExFreePoolWithTag(RingBufferOne, UT_TAG_MAIN);

        if (RingBufferTwo)
            ExFreePoolWithTag(RingBufferTwo, UT_TAG_MAIN);

        if (PinHandle)
            ZwClose(PinHandle);

        if (ClientBufferMapping)
            MmUnmapLockedPages(ClientBufferMapping, ClientBufferMdl);

        if (ClientBufferMdl)
            IoFreeMdl(ClientBufferMdl);

        if (ClientBuffer)
            ExFreePoolWithTag(ClientBuffer, UT_TAG_MAIN);

        if (NotificationEvent)
            ObDereferenceObject(NotificationEvent);
    }

    if (MicrophoneDevice)
        ZwClose(MicrophoneDevice);

    ExReleasePushLockExclusive(&StreamContext->IoCtlPushLock);

    return Result;
}