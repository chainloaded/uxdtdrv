/*++

    Module Name:
        UtAudioHelper

    Abstract:
        Implements various audio helper functions.

    Environment:
        Kernel mode only.

--*/

#pragma once

#include "UtMain.h"

NTSTATUS UtAhFindMicrophone(
    _Out_ PHANDLE DeviceHandle,
    _Out_ PUINT32 PinID
);