/*++

    Module Name:
        UtKsPin

    Abstract:
        Provices abstractions for KS pin interactions.

    Environment:
        Kernel mode only.

--*/

#pragma once

#include "UtMain.h"

extern GUID KsCategoryCapture;
extern GUID KsPinNameCapture;
extern GUID KsPinCategoryCapture;

extern GUID KsPropsetId_RtAudio;

extern GUID KsDataFormatTypeAudio;
extern GUID KsDataFormatSubTypePcm;
extern GUID KsDataFormatSpecWave;

extern GUID KsPropsetId_Pin;

NTSTATUS UtKsSetAudioPinState(
    _In_ HANDLE   PinHandle,
    _In_ KSSTATE  State
);

NTSTATUS UtKsQueryPin(
    _In_        HANDLE  DeviceHandle,
    _In_        UINT32  PropertyId,
    _In_        UINT32  PinId,
    _In_        UINT32  OutputBufferLength,
    _Out_       PVOID   OutputBuffer,
    _Out_opt_   PUINT32 Required
);

NTSTATUS UtKsConnectAudioPin(
    _In_    HANDLE  DeviceHandle,
    _In_    UINT32  PinId,
    _Out_   PHANDLE PinHandle
);

NTSTATUS UtKsQueryAudioPin(
    _In_    HANDLE  PinHandle,
    _In_    PVOID   InBuffer,
    _In_    UINT32  InBufferSize,
    _Out_   PVOID   OutBuffer,
    _In_    UINT32  OutBufferSize
);