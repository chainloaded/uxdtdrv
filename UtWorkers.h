/*++

    Module Name:
        UtKsWorkers

    Abstract:
        Implements audio worker threads responsible
        for processing audio data.

    Environment:
        Kernel mode only.

--*/

#pragma once

#include "UtMain.h"

void _Function_class_(KSTART_ROUTINE) UtWoDmaWorker(
    _In_ PVOID Context
);

void _Function_class_(KSTART_ROUTINE) UtWoUxdtWorker(
    _In_ PVOID Context
);