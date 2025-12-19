/*++

    Module Name:
        UtMain

    Abstract:
        Implements the main entry point for the UT driver, common definitions 
        and WDK headers inclusion.

    Environment:
        Kernel mode only.

--*/


#pragma once

#pragma warning(push)
#pragma warning (disable : 28230 28285 28301 6387 28160 28252 28253 28196)

#include <ntifs.h>
#include <WinDef.h>

#define _INC_MMSYSTEM
#include "mmeapi.h"
#include <ks.h>
#include <ksmedia.h>

#define UT_TAG_MAIN 'mmtu'

#define UT_TARGET_SAMPLE_FREQUENCY  44100
#define UT_TARGET_CHANNELS          2
#define UT_TARGET_BITS_PER_SAMPLE   16

#define RING_BUFFER_SIZE            6000000

#define STATIC_PINCATEGORY_CAPTURE  0xfb6c4281, 0x0353, 0x11d1, 0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba
#define STATIC_FORMATSUBTYPE_PCM    0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 }

#define FILE_DEVICE_DEFAULT 0x8000

#define IOCTL_UT_SEND_MESSAGE CTL_CODE(     \
    FILE_DEVICE_DEFAULT,                    \
    0x800,                                  \
    METHOD_BUFFERED,                        \
    FILE_WRITE_ACCESS )

#define IOCTL_UT_RECEIVE CTL_CODE(          \
    FILE_DEVICE_DEFAULT,                    \
    0x801,                                  \
    METHOD_BUFFERED,                        \
    FILE_READ_ACCESS )

typedef struct _UT_STREAM_CONTEXT
{
    EX_PUSH_LOCK IoCtlPushLock;
    BOOLEAN Initialized;
    BOOLEAN IsClosing;
    HANDLE  PinHandle;
    PKEVENT NotificationEvent;
    PVOID   SharedRingBuffer[2];
    PVOID   WaveRtBuffer;
    PVOID   ClientBuffer;
    PVOID   ClientBufferKernel;
    PVOID   HwRegister;
    UINT32  RingBufferSize;
    UINT32  HwRegisterSize;
    UINT32  WaveRtBufferSize;
    UINT32  ClientBufferSize;
    UINT32  FifoSize;
    PMDL    ClientBufferMdl;
    HANDLE  ClientProcessId;
    HANDLE  DmaWorker;
    HANDLE  UxdtWorker;
    KEVENT  BufferFilledEvent;
} UT_STEAM_CONTEXT, *PUT_STREAM_CONTEXT;