#include "UtAudioHelper.h"
#include "UtKsPin.h"

NTSTATUS UtAhFindMicrophone(
    _Out_ PHANDLE DeviceHandle,
    _Out_ PUINT32 PinID
)
/*++
    Routine Description:
        Finds a suitable microphone audio capture pin on the system and
        returns a handle to the device and the pin id.

    Arguments:

        DeviceHandle:
            Receives a handle to the microphone device if found.

        PinID:
            Receives the pin index on the device that represents the
            microphone/capture pin.

    Return Value:

        NTSTATUS value indicating success (STATUS_SUCCESS) or the failure
        code describing why a microphone/pin could not be found.
--*/
{
    NTSTATUS            Result;
    BOOLEAN             DeviceFound             = FALSE;
    HANDLE              DeviceHandleLocal       = NULL;
    PWSTR               DeviceList              = NULL;
    PWSTR               CurrentString           = NULL;
    PKSMULTIPLE_ITEM    Items                   = NULL;
    PKSDATARANGE        Range                   = NULL;
    PKSDATARANGE_AUDIO  AudioFormat             = { 0 };
    UNICODE_STRING      DeviceName              = { 0 };
    OBJECT_ATTRIBUTES   DeviceAttribtutes       = { 0 };
    IO_STATUS_BLOCK     IoStatusBlock           = { 0 };
    GUID                PinCategory             = { 0 };
    UINT32              PinCount                = 0;
    UINT32              Required                = 0;

    // Get device interface names for audio capture devices
    Result = IoGetDeviceInterfaces(
        &KsCategoryCapture,
        NULL,
        0,
        &DeviceList
    );

    if (!NT_SUCCESS(Result))
        goto Done;

    // Iterate through the device list to find a microphone
    Result          = STATUS_NOT_FOUND;
    CurrentString   = DeviceList;

    for (; *CurrentString != '\0'; CurrentString += wcslen(CurrentString) + 1)
    {
        // Close previous handle if any
        if (DeviceHandleLocal != NULL)
        {
            ZwClose(DeviceHandleLocal);
            DeviceHandleLocal = NULL;
        }

        // Initialize a unicode string for the device path
        RtlInitUnicodeString(&DeviceName, CurrentString);

        // Initialize object attributes
        InitializeObjectAttributes(
            &DeviceAttribtutes,
            &DeviceName,
            OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
            NULL,
            NULL
        );

        // Open the device
        Result = ZwOpenFile(
            &DeviceHandleLocal,
            GENERIC_READ | 
            GENERIC_WRITE,
            &DeviceAttribtutes,
            &IoStatusBlock,
            FILE_SHARE_READ | 
            FILE_SHARE_WRITE,
            FILE_SYNCHRONOUS_IO_NONALERT
        );

        // Continue if fail
        if (!NT_SUCCESS(Result))
            continue;

        // Get this devices pin count
        Result = UtKsQueryPin(
            DeviceHandleLocal,
            KSPROPERTY_PIN_CTYPES,
            0,
            sizeof(UINT32),
            &PinCount,
            NULL
        );

        if (!NT_SUCCESS(Result))
            continue;

        // Iterate through pins to find a microphone pin
        Result = STATUS_NOT_FOUND;

        for (UINT32 i = 0; i < PinCount; i++)
        {
            // Get the pins category
            Result = UtKsQueryPin(
                DeviceHandleLocal,
                KSPROPERTY_PIN_CATEGORY,
                i,
                sizeof(GUID),
                &PinCategory,
                NULL
            );

            if (!NT_SUCCESS(Result))
                continue;

            // Check if this is a microphone pin
            if (RtlCompareMemory(&PinCategory, &KsPinCategoryCapture, sizeof(GUID)) != sizeof(GUID))
            {
                // PINNAME_CAPTURE might contain audio ranges as well
                if (RtlCompareMemory(&PinCategory, &KsPinNameCapture, sizeof(GUID)) != sizeof(GUID))
                    continue;
            }

            // Get required sizes for data ranges
            Result = UtKsQueryPin(
                DeviceHandleLocal,
                KSPROPERTY_PIN_DATARANGES,
                i,
                0,
                NULL,
                &Required
            );

            // Check expected result
            if (Result != STATUS_BUFFER_OVERFLOW)
                continue;

            if (Required == 0)
                continue;

            // Free items from previous iteration
            if (Items != NULL)
            {
                ExFreePoolWithTag(Items, UT_TAG_MAIN);
                Items = NULL;
            }

            // Allocate buffer for data ranges
            Items = ExAllocatePool2(
                POOL_FLAG_NON_PAGED,
                Required,
                UT_TAG_MAIN
            );

            if (Items == NULL)
            {
                Result = STATUS_INSUFFICIENT_RESOURCES;
                goto Done;
            }

            // Get data ranges
            Result = UtKsQueryPin(
                DeviceHandleLocal,
                KSPROPERTY_PIN_DATARANGES,
                i,
                Required,
                Items,
                NULL
            );

            if (!NT_SUCCESS(Result))
                continue;

            Result  = STATUS_NOT_FOUND;
            Range   = (PKSDATARANGE)(Items + 1);

            // Iterate through data ranges to find PCM format
            for (UINT32 x = 0; x < Items->Count; 
                x++, Range = (PKSDATARANGE)(((PUCHAR)Range) + Range->FormatSize))
            {
                // Check if GUIDs match
                if (RtlCompareMemory(&Range->MajorFormat, &KsDataFormatTypeAudio, sizeof(GUID)) != sizeof(GUID))
                    continue;

                if (RtlCompareMemory(&Range->SubFormat, &KsDataFormatSubTypePcm, sizeof(GUID)) != sizeof(GUID))
                    continue;

                if (RtlCompareMemory(&Range->Specifier, &KsDataFormatSpecWave, sizeof(GUID)) != sizeof(GUID))
                    continue;

                // We found a suited one
                AudioFormat = (PKSDATARANGE_AUDIO)Range;

                // For now, we only support 16 bits per sample
                if (AudioFormat->MaximumBitsPerSample < 16 || 
                    AudioFormat->MinimumBitsPerSample > 16)
                    continue;

                // For now, only support up to 2 channels
                if (AudioFormat->MaximumChannels > 2)
                    continue;

                // For now, only support 44100 Hz
                if (AudioFormat->MinimumSampleFrequency <= 44100 &&
                    AudioFormat->MaximumSampleFrequency >= 44100)
                    continue;

                // Found, return info
                DeviceFound     = TRUE;
                *DeviceHandle   = DeviceHandleLocal;
                *PinID          = i;

                break;
            }

            if (DeviceFound == TRUE)
                break;
        }

        if (DeviceFound == TRUE)
            break;
    }

    if (DeviceFound != TRUE)
    {
        Result = STATUS_NOT_FOUND;
        goto Done;
    }

Done:

    if (DeviceFound != TRUE && DeviceHandleLocal != NULL)
        ZwClose(DeviceHandleLocal);

    if (Items != NULL)
        ExFreePoolWithTag(Items, UT_TAG_MAIN);

    if (DeviceList != NULL)
        ExFreePool(DeviceList);

    return Result;
}