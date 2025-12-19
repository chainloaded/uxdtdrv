#include "UtKsPin.h"

// KS properties and categories
GUID KsCategoryCapture      = { STATIC_KSCATEGORY_CAPTURE };
GUID KsPinNameCapture       = { STATIC_PINNAME_VIDEO_CAPTURE };
GUID KsPinCategoryCapture   = { STATIC_PINCATEGORY_CAPTURE };

// KS property sets
GUID KsPropsetId_Pin        = { STATIC_KSPROPSETID_Pin };
GUID KsPropsetId_RtAudio    = { STATIC_KSPROPSETID_RtAudio };
GUID KsPropsetId_Audio      = { STATIC_KSPROPSETID_Audio };
GUID KsMediumSetId_Def      = { STATIC_KSMEDIUMSETID_Standard };
GUID KsInterfaceSetId_Def   = { STATIC_KSINTERFACESETID_Standard };
GUID KsPropsetId_Connect    = { STATIC_KSPROPSETID_Connection };

// Data format types
GUID KsDataFormatTypeAudio  = { STATIC_KSDATAFORMAT_TYPE_AUDIO };
GUID KsDataFormatSubTypePcm = { STATIC_FORMATSUBTYPE_PCM };
GUID KsDataFormatSpecWave   = { STATIC_KSDATAFORMAT_SPECIFIER_WAVEFORMATEX };
GUID KKsDataSubFormatPcm    = { STATIC_KSDATAFORMAT_SUBTYPE_PCM };

NTSTATUS UtKsSetAudioPinState(
    _In_ HANDLE   PinHandle,
    _In_ KSSTATE  State
)
/*++
    Routine Description:
        Sets the KS pin state for the given audio pin handle.

    Arguments:

        PinHandle:
            Handle to the pin to change state on.

        State:
            Desired KSSTATE to set for the pin.

    Return Value:

        NTSTATUS indicating success or failure of the operation.
--*/
{
    NTSTATUS            Result;
    HANDLE              EventHandle     = NULL;
    KSPROPERTY          Property        = { 0 };
    IO_STATUS_BLOCK     IoStatusBlock   = { 0 };

    // Initialize event handle for turning mic on
    Result = ZwCreateEvent(&EventHandle, EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE);

    if (!NT_SUCCESS(Result))
        goto Done;

    Property.Set   = KsPropsetId_Connect;
    Property.Id    = KSPROPERTY_CONNECTION_STATE;
    Property.Flags = KSPROPERTY_TYPE_SET;

    // Send the IOCTL
    Result = ZwDeviceIoControlFile(
        PinHandle,
        EventHandle,
        NULL,
        NULL,
        &IoStatusBlock,
        IOCTL_KS_PROPERTY,
        &Property,
        sizeof(Property),
        &State,
        sizeof(KSSTATE)
    );

    if (Result == STATUS_PENDING)
    {
        ZwWaitForSingleObject(EventHandle, FALSE, NULL);
        Result = IoStatusBlock.Status;
    }

Done:

    if (EventHandle)
        ZwClose(EventHandle);

    return Result;
}

NTSTATUS UtKsQueryAudioPin(
    _In_    HANDLE  PinHandle,
    _In_    PVOID   InBuffer,
    _In_    UINT32  InBufferSize,
    _Out_   PVOID   OutBuffer,
    _In_    UINT32  OutBufferSize
)
/*++
    Routine Description:
        Queries a KS audio property on the given pin handle. This helper
        wraps the IOCTL_KS_PROPERTY call and handles waiting for completion.

    Arguments:

        PinHandle:
            Handle to the audio pin to query.

        InBuffer:
            Pointer to the input property structure.

        InBufferSize:
            Size of the input buffer.

        OutBuffer:
            Pointer to the output buffer that will receive data.

        OutBufferSize:
            Size of the output buffer.

    Return Value:

        NTSTATUS indicating success or failure of the operation.
--*/
{
    NTSTATUS        Result;
    HANDLE          EventHandle = NULL;
    IO_STATUS_BLOCK IoStatusBlock = { 0 };

    // Create an event to signal completion
    Result = ZwCreateEvent(&EventHandle, EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE);

    if (!NT_SUCCESS(Result))
        goto Done;

    // Send the IOCTL to query the property
    Result = ZwDeviceIoControlFile(
        PinHandle,
        NULL,
        NULL,
        NULL,
        &IoStatusBlock,
        IOCTL_KS_PROPERTY,
        InBuffer,
        InBufferSize,
        OutBuffer,
        OutBufferSize
    );

    // Wait for the operation to complete
    if (Result == STATUS_PENDING)
    {
        ZwWaitForSingleObject(EventHandle, FALSE, NULL);
        Result = IoStatusBlock.Status;
    }

Done:

    if (EventHandle)
        ZwClose(EventHandle);

    return Result;
}

NTSTATUS UtKsConnectAudioPin(
    _In_    HANDLE  DeviceHandle,
    _In_    UINT32  PinId,
    _Out_   PHANDLE PinHandle
)
/*++
    Routine Description:
        Connects to an audio pin on a KS audio device. Builds a
        KSPIN_CONNECT structure with a 44100Hz, 16-bit, stereo PCM
        waveformat and calls KsCreatePin to open the pin.

    Arguments:

        DeviceHandle:
            Handle to the KS audio device to create the pin on.

        PinId:
            Index of the pin to connect.

        PinHandle:
            Receives a handle to the created pin on success.
    
    Return Value:

        NTSTATUS indicating success or failure of the pin creation.
--*/
{
    NTSTATUS                    Result;
    PKSPIN_CONNECT              Connect         = NULL;
    PKSDATAFORMAT_WAVEFORMATEX  WaveFormat      = NULL;
    UINT32                      BlockAlign      = 0;
    UINT32                      AvgBytesPerSec  = 0;

    // Calculate alignments
    BlockAlign      = (2 * 16) / 8;
    AvgBytesPerSec  = 44100 * BlockAlign;

    // Allocate connect structure
    Connect = ExAllocatePool2(
        POOL_FLAG_NON_PAGED, 
        sizeof(KSPIN_CONNECT) + sizeof(KSDATAFORMAT_WAVEFORMATEX), 
        UT_TAG_MAIN
    );

    if (Connect == NULL)
    {
        Result = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    // Populate the connection interface
    Connect->Interface.Set                                  = KsInterfaceSetId_Def;
    Connect->Interface.Id                                   = KSINTERFACE_STANDARD_LOOPED_STREAMING;
    Connect->Interface.Flags                                = 0;

    // Populate the connection medium
    Connect->Medium.Set                                     = KsMediumSetId_Def;
    Connect->Medium.Id                                      = KSMEDIUM_TYPE_ANYINSTANCE;
    Connect->Medium.Flags                                   = 0;

    // Set the pin ID
    Connect->PinId                                          = PinId;
    Connect->PinToHandle                                    = 0;

    // Set the priority
    Connect->Priority.PriorityClass                         = KSPRIORITY_NORMAL;
    Connect->Priority.PrioritySubClass                      = 1;

    // Populate the data format
    WaveFormat                                              = (PKSDATAFORMAT_WAVEFORMATEX)(Connect + 1);

    WaveFormat->DataFormat.FormatSize                       = sizeof(KSDATAFORMAT_WAVEFORMATEX);
    WaveFormat->DataFormat.Flags                            = 0;
    WaveFormat->DataFormat.SampleSize                       = 0;
    WaveFormat->DataFormat.MajorFormat                      = KsDataFormatTypeAudio;
    WaveFormat->DataFormat.SubFormat                        = KsDataFormatSubTypePcm;
    WaveFormat->DataFormat.Specifier                        = KsDataFormatSpecWave;

    // Populate the wave format extensible structure
    WaveFormat->WaveFormatEx.wFormatTag                     = WAVE_FORMAT_PCM;
    WaveFormat->WaveFormatEx.nChannels                      = 2;
    WaveFormat->WaveFormatEx.wBitsPerSample                 = 16;
    WaveFormat->WaveFormatEx.nSamplesPerSec                 = 44100;
    WaveFormat->WaveFormatEx.nBlockAlign                    = BlockAlign;
    WaveFormat->WaveFormatEx.nAvgBytesPerSec                = AvgBytesPerSec;
    WaveFormat->WaveFormatEx.cbSize                         = 0; // Size of the extended format

    // Finally, create the pin
    Result = KsCreatePin(DeviceHandle, Connect, GENERIC_READ, PinHandle);

Done:

    if (Connect)
        ExFreePoolWithTag(Connect, UT_TAG_MAIN);

    return Result;
}

NTSTATUS UtKsQueryPin(
    _In_        HANDLE  DeviceHandle,
    _In_        UINT32  PropertyId,
    _In_        UINT32  PinId,
    _In_        UINT32  OutputBufferLength,
    _Out_       PVOID   OutputBuffer,
    _Out_opt_   PUINT32 Required
)
/*++
    Routine Description:
        Queries a pin property on a KS device. Wraps IOCTL_KS_PROPERTY for
        pin-related properties and optionally returns the required size
        when the provided buffer is too small.

    Arguments:

        DeviceHandle:
            Handle to the KS device to query.

        PropertyId:
            The KSPROPERTY id to query (e.g. KSPROPERTY_PIN_CTYPES).

        PinId:
            The pin index to perform the query on.

        OutputBufferLength:
            Length of the output buffer.

        OutputBuffer:
            Pointer to the buffer to receive the property data.

        Required:
            Optional pointer that receives the required buffer size when
            the call returns STATUS_BUFFER_OVERFLOW.

    Return Value:

        NTSTATUS indicating success or failure of the query.
--*/

{
    NTSTATUS        Result;
    IO_STATUS_BLOCK IoStatusBlock   = { 0 };
    KSP_PIN         Pin             = { 0 };

    // Populate property structure
    Pin.Property.Set    = KsPropsetId_Pin;
    Pin.Property.Flags  = KSPROPERTY_TYPE_GET;
    Pin.Property.Id     = PropertyId;
    Pin.PinId           = PinId;

    // Query the property
    Result = ZwDeviceIoControlFile(
        DeviceHandle,
        NULL,
        NULL,
        NULL,
        &IoStatusBlock,
        IOCTL_KS_PROPERTY,
        &Pin,
        sizeof(Pin),
        OutputBuffer,
        OutputBufferLength
    );

    // Return required size if buffer was too small
    if (Result == STATUS_BUFFER_OVERFLOW && Required != NULL)
    {
        *Required = (UINT32)IoStatusBlock.Information;
    }

    return Result;
}