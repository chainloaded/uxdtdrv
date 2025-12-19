#include "UtWorkers.h"
#include "UtAudioProcessor.h"

void _Function_class_(KSTART_ROUTINE) UtWoDmaWorker(
    _In_ PVOID Context
)
/*++
    Routine Description:
        Worker thread that reads audio data from the WaveRT DMA buffer and
        copies it into the shared ring buffers.

    Arguments:

        Context:
            Pointer to a `UT_STREAM_CONTEXT` structure representing the
            current stream state.

    Return Value:
        None. 
--*/
{
    PUT_STREAM_CONTEXT  Ctx             = Context;
    LARGE_INTEGER       Timeout         = { 0 };
    UINT64              Position        = 0;
    UINT64              BytesToRead     = 0;
    UINT64              LastPosition    = 0;
    UINT64              BytesRead       = 0;
    UINT32              BytesPerFrame   = 0;
    UINT32              ReadCurrentBuf  = 0;
    UINT32              FirstPart       = 0;
    UINT32              SecondPart      = 0;
    UINT8               BufIndex        = 0;

    // Set timeout to 8ms
    Timeout.QuadPart = -80000; // 8ms in 100ns units

    // Calculate the bytes per frame
    BytesPerFrame = UT_TARGET_CHANNELS * (UT_TARGET_BITS_PER_SAMPLE / 8);

    while (!Ctx->IsClosing && Ctx->Initialized)
    {
        // Get hold of current ADC position
        if (Ctx->HwRegisterSize == 64)
        {
            Position = *((PUINT64)Ctx->HwRegister);
        }
        else
        {
            Position = *((PUINT32)Ctx->HwRegister);
        }

        // Compensate for HW FIFO to get to last read buffer position
        Position += Ctx->FifoSize;

        // Align with DMA buffer size
        Position %= Ctx->WaveRtBufferSize;

        // Align with position on frame boundary
        Position -= Position % BytesPerFrame;

        BytesToRead = (Ctx->WaveRtBufferSize + Position - LastPosition)
            % Ctx->WaveRtBufferSize;

        // Align to frame size
        BytesToRead -= BytesToRead % BytesPerFrame;

        if (BytesToRead > 0)
        {
            // Limit the read size based on how much space is left in current buffer
            BytesRead = (UINT32)min(BytesToRead, Ctx->RingBufferSize - ReadCurrentBuf);

            // Check if the buffer data is contiguous
            if (Position >= LastPosition)
            {
                RtlCopyMemory(
                    (PVOID)((UINT64)Ctx->SharedRingBuffer[BufIndex] + ReadCurrentBuf), 
                    (PVOID)((UINT64)Ctx->WaveRtBuffer + LastPosition), 
                    BytesRead
                );

                ReadCurrentBuf += (UINT32)BytesRead;
            }
            else
            {
                // Buffer wraps around, so split into two copies
                // Calculate for the first part the size of the tail
                FirstPart = Ctx->WaveRtBufferSize - (UINT32)LastPosition;
                FirstPart = (UINT32)min(FirstPart, BytesRead);

                // Calculate the position tracker for the second part
                SecondPart = (UINT32)(BytesRead - FirstPart);

                // Copy the tail
                RtlCopyMemory(
                    (PVOID)((UINT64)Ctx->SharedRingBuffer[BufIndex] + ReadCurrentBuf),
                    (PVOID)((UINT64)Ctx->WaveRtBuffer + LastPosition),
                    FirstPart
                );

                ReadCurrentBuf += FirstPart;

                // Copy the beginning of the buffer
                RtlCopyMemory(
                    (PVOID)((UINT64)Ctx->SharedRingBuffer[BufIndex] + ReadCurrentBuf),
                    Ctx->WaveRtBuffer,
                    SecondPart
                );

                ReadCurrentBuf += SecondPart;
            }

            // is the entire current buffer in the ring buffer consumed?
            if (ReadCurrentBuf == Ctx->RingBufferSize)
            {
                // Switch buffer index
                BufIndex = (BufIndex == 0 ? 1 : 0);
                ReadCurrentBuf = 0;

                // Signal filled event
                KeSetEvent(&Ctx->BufferFilledEvent, FALSE, FALSE);
            }

            // Advance our LastPosition within the DMA buffer
            LastPosition = (LastPosition + BytesRead) % Ctx->WaveRtBufferSize;
        }

        /* Sleep for the timeout interval */
        KeDelayExecutionThread(KernelMode, FALSE, &Timeout);
    }

    // Signal filled event to make sure the other worker finishes up as well
    KeSetEvent(&Ctx->BufferFilledEvent, FALSE, FALSE);
}

void _Function_class_(KSTART_ROUTINE) UtWoUxdtWorker(
    _In_ PVOID Context
)
/*++
* 
    Routine Description:
        Worker thread that consumes the filled ring buffers, decodes any
        UXDT messages found and copies results into the client's shared
        user buffer.

    Arguments:

        Context:
            Pointer to a `UT_STREAM_CONTEXT` structure representing the
            current stream state.

    Return Value:
        None.

--*/
{
    NTSTATUS            Status;
    PUT_STREAM_CONTEXT  Ctx             = Context;
    UINT8               BufferIndex     = 0;

    while (!Ctx->IsClosing && Ctx->Initialized)
    {
        // Wait for the buffer event
        KeWaitForSingleObject(&Ctx->BufferFilledEvent, Executive, KernelMode, FALSE, NULL);

        if (Ctx->IsClosing || !Ctx->Initialized)
            break;

        // Analyze the current buffer and try to find a message
        Status = UtApAnalyzePcmStream(
            Ctx->SharedRingBuffer[BufferIndex],
            RING_BUFFER_SIZE,
            (PVOID)((UINT64)Ctx->ClientBufferKernel + sizeof(UINT32)),
            Ctx->ClientBufferSize - sizeof(UINT32),
            ((PUINT32)Ctx->ClientBufferKernel)
        );

        if (NT_SUCCESS(Status))
        {
            // Signal event
            KeSetEvent(Ctx->NotificationEvent, 0, FALSE);
        }

        BufferIndex = (BufferIndex == 0 ? 1 : 0);
    }
}