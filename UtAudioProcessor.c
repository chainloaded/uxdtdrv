#include "UtAudioProcessor.h"

int _fltused = 0;

FLOAT UtAppCos(
    _In_ FLOAT Angle
)
/*++

    Routine Description:

        Computes the Cosine of an angle using a Taylor Series approximation.

    Arguments:

        Angle:
            The angle in radians.

    Return Value:
        The cosine of the angle.

--*/
{
    FLOAT Sq = Angle * Angle;
    FLOAT Result = 1.0f;

    // Term: - x^2 / 2
    Result -= (Sq / 2.0f);

    // Term: + x^4 / 24
    Result += (Sq * Sq / 24.0f);

    // Term: - x^6 / 720
    Result -= (Sq * Sq * Sq / 720.0f);

    return Result;
}

FLOAT UtAppGoertzelEnergy(
    _In_ PINT16 Buffer,
    _In_ ULONG  NumSamples,
    _In_ FLOAT  TargetFreq
)
/*++

    Routine Description:

        Calculates the relative energy (magnitude squared) of a specific frequency 
        component within a block of PCM samples using the Goertzel algorithm.

    Arguments:

        Buffer:
            Pointer to the raw PCM data (interleaved stereo).

        NumSamples:
            Number of stereo frames to process.

        TargetFreq:
            The frequency to detect (Hz).

    Return Value:
        The squared magnitude of the target frequency.

--*/
{
    FLOAT   Omega;
    FLOAT   Coeff;
    FLOAT   Q0, Q1, Q2;
    ULONG   i;
    INT16   MonoSample;

    // Precompute the coefficient.
    // Omega = (2 * PI * Freq) / SampleRate
    Omega = (2.0f * UT_PI * TargetFreq) / (FLOAT)UT_TARGET_CHANNELS;
    Coeff = 2.0f * UtAppCos(Omega);

    Q1 = 0.0f;
    Q2 = 0.0f;

    // Run the Goertzel filter loop.
    // Buffer is stereo (L, R, L, R), so we iterate 2 * NumSamples.
    for (i = 0; i < NumSamples * 2; i += 2) 
    {

        // Downmix Stereo to Mono: (L + R) / 2
        MonoSample = (Buffer[i] + Buffer[i + 1]) / 2;

        Q0 = (Coeff * Q1) - Q2 + (FLOAT)MonoSample;
        Q2 = Q1;
        Q1 = Q0;
    }

    // Calculate Magnitude Squared = Q1^2 + Q2^2 - Q1*Q2*Coeff
    return (Q1 * Q1) + (Q2 * Q2) - (Q1 * Q2 * Coeff);
}

USHORT UtUpdateCrc16(
    _In_ USHORT Crc,
    _In_ UCHAR  Data
)
/*++

    Routine Description:
        Updates the running CRC16-IBM checksum with a new byte of data.

    Arguments:
        Crc: The current CRC value.

    Data:
        The byte to add.

    Return Value:
        The updated CRC value.

--*/
{
    USHORT x;

    x = (USHORT)(((Crc >> 8) ^ Data) & 0xFF);
    x ^= x >> 4;
    return (Crc << 8) ^ (USHORT)((x << 12) ^ (x << 5) ^ x);
}

NTSTATUS UtApAnalyzePcmStream(
    _In_    PVOID   RawBuffer,
    _In_    UINT32  BufferSize,
    _Out_   PUINT8  OutputBuffer,
    _In_    UINT32  MaxOutputSize,
    _Out_   PUINT32 BytesWritten
)
/*++

    Routine Description:
        Analyzes a raw PCM buffer to detect and decode an FSK encoded message.

    Arguments:

        RawBuffer:
            Pointer to the raw PCM audio data (16-bit, Stereo).

        BufferSize:
            Size of RawBuffer in bytes.

        OutputBuffer:
            Caller-allocated buffer to receive the decoded ASCII text.

        MaxOutputSize: 
            Size of OutputBuffer in bytes.

        BytesWritten:
            Receives the number of valid bytes written to OutputBuffer.

    Return Value:

        STATUS_SUCCESS             - Message found, CRC valid, payload extracted.
        STATUS_NOT_FOUND           - Preamble not detected in stream.
        STATUS_DATA_CHECKSUM_ERROR - Message found, but CRC check failed.
        STATUS_BUFFER_TOO_SMALL    - Message found, but OutputBuffer is too small.

--*/
{
    NTSTATUS         Status;
    BOOLEAN          PreambleFound          = FALSE;
    KFLOATING_SAVE   FloatState;
    PINT16           PcmData;
    UINT32           TotalFrames;
    UINT32           CurrentFrameOffset     = 0;
    UINT16           ShiftRegister          = 0;
    UINT16           ReceivedCrc            = 0;
    UINT16           CalculatedCrc          = 0;
    UINT32           PayloadIndex           = 0;
    UINT8            CurrentChar            = 0;
    UINT32           BitIndexInChar         = 0;

    PcmData             = (PINT16)RawBuffer;
    TotalFrames         = BufferSize / 4; 
    *BytesWritten       = 0;

    // Save Floating Point State.
    Status = KeSaveFloatingPointState(&FloatState);

    if (!NT_SUCCESS(Status)) 
    {
        return Status;
    }

    // Default return status if nothing is found
    Status = STATUS_NOT_FOUND;

    // Iterate through the PCM buffer in chunks of bit-duration.
    while (CurrentFrameOffset + UT_SAMPLES_PER_BIT <= TotalFrames) 
    {

        FLOAT   EnergyZero;
        FLOAT   EnergyOne;
        UINT32  DetectedBit;

        // Calculate energy at both frequencies for the current window
        EnergyZero = UtAppGoertzelEnergy(
            &PcmData[CurrentFrameOffset * 2], 
            UT_SAMPLES_PER_BIT, 
            UT_FREQ_SPACE
        );

        EnergyOne  = UtAppGoertzelEnergy(
            &PcmData[CurrentFrameOffset * 2], 
            UT_SAMPLES_PER_BIT, 
            UT_FREQ_MARK
        );

        // Determine if this is a 1 or a 0
        DetectedBit = (EnergyOne > EnergyZero) ? 1 : 0;

        // Advance the window
        CurrentFrameOffset += UT_SAMPLES_PER_BIT;

        // Preamble Search
        if (!PreambleFound) 
        {
            // Shift the new bit into our history window
            ShiftRegister = (UINT16)((ShiftRegister << 1) | DetectedBit);

            // Check if the history matches our preamble pattern
            if (ShiftRegister == UT_PREAMBLE_VAL) 
            {
                PreambleFound = TRUE;

                // Prepare to read CRC. We use PayloadIndex to count CRC bits temporarily.
                PayloadIndex = 0; 
            }

            continue;
        }

        // Read CRC (16 bits)
        if (PayloadIndex < 16) 
        {
            ReceivedCrc = (UINT16)((ReceivedCrc << 1) | DetectedBit);
            PayloadIndex++;

            if (PayloadIndex == 16) 
            {
                // CRC read complete. Reset state to read Payload.
                PayloadIndex    = 0;
                BitIndexInChar  = 0;
                CurrentChar     = 0;
                CalculatedCrc   = 0; 
            }
            continue;
        }

        // Read Payload
        CurrentChar = (CurrentChar << 1) | DetectedBit;
        BitIndexInChar++;

        if (BitIndexInChar == 8) {
            // We have a full byte.

            // Output buffer overflow
            if (PayloadIndex >= MaxOutputSize) 
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                goto Done;
            }

            // Store the character
            OutputBuffer[PayloadIndex] = CurrentChar;

            // Update the calculated CRC
            CalculatedCrc = UtUpdateCrc16(CalculatedCrc, CurrentChar);

            // Check for End of Message (Null Terminator)
            if (CurrentChar == 0x00) 
            {
                // Verify Integrity
                if (CalculatedCrc == ReceivedCrc) 
                {
                    *BytesWritten = PayloadIndex;
                    Status = STATUS_SUCCESS;
                } 
                else 
                {
                    Status = STATUS_DATA_CHECKSUM_ERROR;
                }

                goto Done;
            }

            // Prepare for next character
            PayloadIndex++;
            BitIndexInChar  = 0;
            CurrentChar     = 0;
        }
    }

Done:

    KeRestoreFloatingPointState(&FloatState);

    return Status;
}