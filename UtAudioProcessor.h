/*++

    Module Name:
        UtAudioProcessor

    Abstract:
        Implements UXDT specific audio processing
        functionality.

    Environment:
        Kernel mode only.

--*/

#pragma once

#include "UtMain.h"

#define UT_FREQ_SPACE           1000.0f     // Represents Bit 0
#define UT_FREQ_MARK            2000.0f     // Represents Bit 1

#define UT_BIT_DURATION_MS      20          // Duration of one bit in milliseconds

#define UT_PREAMBLE_VAL         0b1111000011110000      // 16-bit Preamble Pattern
#define UT_CRC_POLY             0x8005                  // CRC16-IBM Polynomial

#define UT_PI                   3.14159265359f

#define UT_SAMPLES_PER_BIT      ((UT_TARGET_SAMPLE_FREQUENCY * UT_BIT_DURATION_MS) / 1000)

NTSTATUS UtApAnalyzePcmStream(
    _In_    PVOID   RawBuffer,
    _In_    UINT32  BufferSize,
    _Out_   PUINT8  OutputBuffer,
    _In_    UINT32  MaxOutputSize,
    _Out_   PUINT32 BytesWritten
);