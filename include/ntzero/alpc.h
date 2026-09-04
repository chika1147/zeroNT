#ifndef NTZERO_ALPC_H
#define NTZERO_ALPC_H

#include "types.h"

#define NTZ_ALPC_MAX_PORTS 16u
#define NTZ_ALPC_MAX_MESSAGES 64u
#define NTZ_ALPC_NAME_MAX 64u
#define NTZ_ALPC_PAYLOAD_MAX 256u

typedef enum NTZ_ALPC_PORT_STATE {
    NtzAlpcPortFree = 0,
    NtzAlpcConnectionPort = 1,
    NtzAlpcClientPort = 2,
    NtzAlpcServerPort = 3,
    NtzAlpcDisconnected = 4
} NTZ_ALPC_PORT_STATE;

typedef struct NTZ_ALPC_MESSAGE {
    u64 message_id;
    u64 reply_to;
    u64 sender_pid;
    NTZ_HANDLE source_port;
    NTZ_HANDLE target_port;
    u32 data_length;
    u32 flags;
    u8 data[NTZ_ALPC_PAYLOAD_MAX];
} NTZ_ALPC_MESSAGE;

void AlpcInitialize(void);
NTSTATUS NtAlpcCreatePort(const char *name, u64 owner_pid, NTZ_HANDLE *out_port);
NTSTATUS NtAlpcConnectPort(const char *name, u64 client_pid, NTZ_HANDLE *out_client_port);
NTSTATUS NtAlpcAcceptConnectPort(NTZ_HANDLE connection_port, u64 server_pid, NTZ_HANDLE *out_server_port);
NTSTATUS NtAlpcSendWaitReceivePort(NTZ_HANDLE port, const NTZ_ALPC_MESSAGE *send_message, NTZ_ALPC_MESSAGE *receive_message);
NTSTATUS NtAlpcDisconnectPort(NTZ_HANDLE port);

#endif
