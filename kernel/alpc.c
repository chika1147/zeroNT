#include "ntzero/alpc.h"

typedef struct NTZ_ALPC_PORT {
    u32 in_use;
    NTZ_ALPC_PORT_STATE state;
    u64 owner_pid;
    NTZ_HANDLE peer;
    NTZ_HANDLE listener;
    u64 pending_connection_pid;
    char name[NTZ_ALPC_NAME_MAX];
} NTZ_ALPC_PORT;

typedef struct NTZ_ALPC_QUEUE_ITEM {
    u32 in_use;
    NTZ_ALPC_MESSAGE message;
} NTZ_ALPC_QUEUE_ITEM;

static NTZ_ALPC_PORT g_ports[NTZ_ALPC_MAX_PORTS];
static NTZ_ALPC_QUEUE_ITEM g_messages[NTZ_ALPC_MAX_MESSAGES];
static u64 g_next_message_id = 1u;

static void zero_bytes(void *ptr, usize count) {
    u8 *p = (u8 *)ptr;
    while (count-- != 0u) {
        *p++ = 0u;
    }
}

static void copy_bytes(void *dst, const void *src, usize count) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (count-- != 0u) {
        *d++ = *s++;
    }
}

static int string_equal(const char *a, const char *b) {
    if (a == (const char *)0 || b == (const char *)0) {
        return 0;
    }
    while (*a != '\0' && *b != '\0') {
        if (*a++ != *b++) {
            return 0;
        }
    }
    return *a == *b;
}

static void string_copy(char *dst, const char *src, usize cap) {
    usize i = 0u;
    if (cap == 0u) {
        return;
    }
    while (src[i] != '\0' && i + 1u < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static NTZ_HANDLE handle_from_index(u32 index) {
    return (NTZ_HANDLE)(index + 1u);
}

static NTZ_ALPC_PORT *port_from_handle(NTZ_HANDLE handle) {
    u64 index;
    if (handle == 0u) {
        return (NTZ_ALPC_PORT *)0;
    }
    index = handle - 1u;
    if (index >= NTZ_ALPC_MAX_PORTS || g_ports[index].in_use == 0u) {
        return (NTZ_ALPC_PORT *)0;
    }
    return &g_ports[index];
}

static NTZ_ALPC_PORT *allocate_port(NTZ_HANDLE *out_handle) {
    u32 i;
    for (i = 0u; i < NTZ_ALPC_MAX_PORTS; ++i) {
        if (g_ports[i].in_use == 0u) {
            zero_bytes(&g_ports[i], sizeof(g_ports[i]));
            g_ports[i].in_use = 1u;
            *out_handle = handle_from_index(i);
            return &g_ports[i];
        }
    }
    return (NTZ_ALPC_PORT *)0;
}

static NTZ_ALPC_PORT *find_listener(const char *name, NTZ_HANDLE *handle) {
    u32 i;
    for (i = 0u; i < NTZ_ALPC_MAX_PORTS; ++i) {
        if (g_ports[i].in_use != 0u &&
            g_ports[i].state == NtzAlpcConnectionPort &&
            string_equal(g_ports[i].name, name)) {
            if (handle != (NTZ_HANDLE *)0) {
                *handle = handle_from_index(i);
            }
            return &g_ports[i];
        }
    }
    return (NTZ_ALPC_PORT *)0;
}

static NTSTATUS enqueue_message(NTZ_ALPC_MESSAGE *message) {
    u32 i;
    for (i = 0u; i < NTZ_ALPC_MAX_MESSAGES; ++i) {
        if (g_messages[i].in_use == 0u) {
            g_messages[i].in_use = 1u;
            copy_bytes(&g_messages[i].message, message, sizeof(*message));
            return STATUS_SUCCESS;
        }
    }
    return STATUS_INSUFFICIENT_RESOURCES;
}

static NTSTATUS dequeue_for_port(NTZ_HANDLE port, NTZ_ALPC_MESSAGE *message) {
    u32 i;
    for (i = 0u; i < NTZ_ALPC_MAX_MESSAGES; ++i) {
        if (g_messages[i].in_use != 0u && g_messages[i].message.target_port == port) {
            copy_bytes(message, &g_messages[i].message, sizeof(*message));
            zero_bytes(&g_messages[i], sizeof(g_messages[i]));
            return STATUS_SUCCESS;
        }
    }
    return STATUS_PENDING;
}

void AlpcInitialize(void) {
    zero_bytes(g_ports, sizeof(g_ports));
    zero_bytes(g_messages, sizeof(g_messages));
    g_next_message_id = 1u;
}

NTSTATUS NtAlpcCreatePort(const char *name, u64 owner_pid, NTZ_HANDLE *out_port) {
    NTZ_ALPC_PORT *port;
    NTZ_HANDLE unused;
    if (name == (const char *)0 || out_port == (NTZ_HANDLE *)0 || *name == '\0') {
        return STATUS_INVALID_PARAMETER;
    }
    if (find_listener(name, &unused) != (NTZ_ALPC_PORT *)0) {
        return STATUS_OBJECT_NAME_COLLISION;
    }
    port = allocate_port(out_port);
    if (port == (NTZ_ALPC_PORT *)0) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    port->state = NtzAlpcConnectionPort;
    port->owner_pid = owner_pid;
    string_copy(port->name, name, NTZ_ALPC_NAME_MAX);
    return STATUS_SUCCESS;
}

NTSTATUS NtAlpcConnectPort(const char *name, u64 client_pid, NTZ_HANDLE *out_client_port) {
    NTZ_HANDLE listener_handle = 0u;
    NTZ_ALPC_PORT *listener;
    NTZ_ALPC_PORT *client;
    if (name == (const char *)0 || out_client_port == (NTZ_HANDLE *)0) {
        return STATUS_INVALID_PARAMETER;
    }
    listener = find_listener(name, &listener_handle);
    if (listener == (NTZ_ALPC_PORT *)0) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (listener->pending_connection_pid != 0u) {
        return STATUS_PENDING;
    }
    client = allocate_port(out_client_port);
    if (client == (NTZ_ALPC_PORT *)0) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    client->state = NtzAlpcClientPort;
    client->owner_pid = client_pid;
    client->listener = listener_handle;
    listener->pending_connection_pid = client_pid;
    listener->peer = *out_client_port;
    return STATUS_SUCCESS;
}

NTSTATUS NtAlpcAcceptConnectPort(NTZ_HANDLE connection_port, u64 server_pid, NTZ_HANDLE *out_server_port) {
    NTZ_ALPC_PORT *listener = port_from_handle(connection_port);
    NTZ_ALPC_PORT *server;
    NTZ_ALPC_PORT *client;
    if (listener == (NTZ_ALPC_PORT *)0 || out_server_port == (NTZ_HANDLE *)0 ||
        listener->state != NtzAlpcConnectionPort) {
        return STATUS_INVALID_PARAMETER;
    }
    if (listener->pending_connection_pid == 0u || listener->peer == 0u) {
        return STATUS_PENDING;
    }
    client = port_from_handle(listener->peer);
    if (client == (NTZ_ALPC_PORT *)0) {
        listener->pending_connection_pid = 0u;
        listener->peer = 0u;
        return STATUS_PORT_DISCONNECTED;
    }
    server = allocate_port(out_server_port);
    if (server == (NTZ_ALPC_PORT *)0) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    server->state = NtzAlpcServerPort;
    server->owner_pid = server_pid;
    server->peer = listener->peer;
    server->listener = connection_port;
    client->peer = *out_server_port;
    listener->pending_connection_pid = 0u;
    listener->peer = 0u;
    return STATUS_SUCCESS;
}

NTSTATUS NtAlpcSendWaitReceivePort(NTZ_HANDLE port_handle, const NTZ_ALPC_MESSAGE *send_message, NTZ_ALPC_MESSAGE *receive_message) {
    NTZ_ALPC_PORT *port = port_from_handle(port_handle);
    NTZ_ALPC_MESSAGE local;
    NTSTATUS status;
    if (port == (NTZ_ALPC_PORT *)0 || port->state == NtzAlpcDisconnected) {
        return STATUS_PORT_DISCONNECTED;
    }
    if (send_message != (const NTZ_ALPC_MESSAGE *)0) {
        if (port->peer == 0u || port_from_handle(port->peer) == (NTZ_ALPC_PORT *)0) {
            return STATUS_PORT_DISCONNECTED;
        }
        zero_bytes(&local, sizeof(local));
        copy_bytes(&local, send_message, sizeof(local));
        if (local.data_length > NTZ_ALPC_PAYLOAD_MAX) {
            return STATUS_INVALID_PARAMETER;
        }
        local.message_id = g_next_message_id++;
        if (g_next_message_id == 0u) {
            g_next_message_id = 1u;
        }
        local.source_port = port_handle;
        local.target_port = port->peer;
        local.sender_pid = port->owner_pid;
        status = enqueue_message(&local);
        if (status != STATUS_SUCCESS) {
            return status;
        }
    }
    if (receive_message != (NTZ_ALPC_MESSAGE *)0) {
        return dequeue_for_port(port_handle, receive_message);
    }
    return STATUS_SUCCESS;
}

NTSTATUS NtAlpcDisconnectPort(NTZ_HANDLE port_handle) {
    NTZ_ALPC_PORT *port = port_from_handle(port_handle);
    NTZ_ALPC_PORT *peer;
    if (port == (NTZ_ALPC_PORT *)0) {
        return STATUS_INVALID_PARAMETER;
    }
    peer = port_from_handle(port->peer);
    if (peer != (NTZ_ALPC_PORT *)0) {
        peer->peer = 0u;
        peer->state = NtzAlpcDisconnected;
    }
    port->peer = 0u;
    port->state = NtzAlpcDisconnected;
    return STATUS_SUCCESS;
}
