/*++

    Module Name:
        UtDeviceControl

    Abstract:
        Implements IOCTL specific command handlers.

    Environment:
        Kernel mode only.

--*/

#pragma once

#include "UtMain.h"

typedef struct _UT_RECEIVE {
    UINT64 BitDuration;             // Duration of each bit
    UINT64 FrequencyOne;            // Frequency representing bit one
    UINT64 FrequencyZero;           // Frequency representing bit zero
    UINT32 OutputBufferLen;         // Desired length of the output buffer in bytes
    HANDLE NotificationEvent;       // Event to signal when buffer is updated
} UT_RECEIVE, *PUT_RECEIVE;

typedef struct _UT_SEND_MESSAGE {
    UINT64 BitDuration;             // Duration of each bit
    UINT64 FrequencyOne;            // Frequency representing bit one
    UINT64 FrequencyZero;           // Frequency representing bit zero
    UINT64 MessageLength;           // Length of the message in bytes
    UINT8  MessageData[1];          // Message data
} UT_SEND_MESSAGE, *PUT_SEND_MESSAGE;

NTSTATUS UtIoctlHandleSendMessage(
    _Inout_ PIRP                Request,
    _In_    PIO_STACK_LOCATION  StackLocation
);

NTSTATUS UtIoctlHandleReceive(
    _Inout_ PIRP                Request,
    _In_    PIO_STACK_LOCATION  StackLocation
);
