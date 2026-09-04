#include <stdio.h>
#include <string.h>
#include "ntzero/alpc.h"

int main(void) {
    NTZ_HANDLE listener = 0;
    NTZ_HANDLE client = 0;
    NTZ_HANDLE server = 0;
    NTZ_ALPC_MESSAGE tx = {0};
    NTZ_ALPC_MESSAGE rx = {0};

    AlpcInitialize();

    if (NtAlpcCreatePort("\\RPC Control\\NTZeroServicePort", 32, &listener) != STATUS_SUCCESS) return 1;
    if (NtAlpcConnectPort("\\RPC Control\\NTZeroServicePort", 36, &client) != STATUS_SUCCESS) return 2;
    if (NtAlpcAcceptConnectPort(listener, 32, &server) != STATUS_SUCCESS) return 3;

    tx.data_length = 9;
    memcpy(tx.data, "heartbeat", 9);

    if (NtAlpcSendWaitReceivePort(client, &tx, 0) != STATUS_SUCCESS) return 4;
    if (NtAlpcSendWaitReceivePort(server, 0, &rx) != STATUS_SUCCESS) return 5;
    if (rx.data_length != 9 || memcmp(rx.data, "heartbeat", 9) != 0 || rx.sender_pid != 36) return 6;

    printf("ALPC PASS id=%llu sender=%llu\n",
           (unsigned long long)rx.message_id,
           (unsigned long long)rx.sender_pid);
    return 0;
}
