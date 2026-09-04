#include "ntzero/drivers.h"
#include "ntzero/memory.h"
#include "ntzero/hw.h"
#include "ntzero/serial.h"

#define NVME_REG_CAP   0x0000u
#define NVME_REG_VS    0x0008u
#define NVME_REG_CC    0x0014u
#define NVME_REG_CSTS  0x001Cu
#define NVME_REG_AQA   0x0024u
#define NVME_REG_ASQ   0x0028u
#define NVME_REG_ACQ   0x0030u
#define NVME_DOORBELL_BASE 0x1000u

#define NVME_ADMIN_IDENTIFY 0x06u
#define NVME_IO_READ        0x02u

#define NVME_ADMIN_DEPTH 16u

typedef struct NVME_COMMAND {
    u8 opcode;
    u8 flags;
    u16 cid;
    u32 nsid;
    u64 reserved0;
    u64 mptr;
    u64 prp1;
    u64 prp2;
    u32 cdw10;
    u32 cdw11;
    u32 cdw12;
    u32 cdw13;
    u32 cdw14;
    u32 cdw15;
} NVME_COMMAND;

typedef struct NVME_COMPLETION {
    u32 result;
    u32 reserved;
    u16 sq_head;
    u16 sq_id;
    u16 cid;
    u16 status;
} NVME_COMPLETION;

typedef struct NTZ_NVME_CONTROLLER {
    int ready;
    u64 bar;
    u32 doorbell_stride;
    u16 queue_depth;
    u16 next_cid;
    u16 sq_tail;
    u16 cq_head;
    u8 cq_phase;
    u32 namespace_id;
    u32 lba_size;
    u64 lba_count;
    NTZ_DMA_BUFFER admin_sq;
    NTZ_DMA_BUFFER admin_cq;
    NTZ_DMA_BUFFER identify;
} NTZ_NVME_CONTROLLER;

static NTZ_NVME_CONTROLLER g_nvme;

static void zero_bytes(void *ptr, u64 count) {
    volatile u8 *p = (volatile u8 *)ptr;
    while (count-- != 0u) *p++ = 0u;
}

static u64 read_u64(const u8 *p) {
    u64 value = 0u;
    u32 i;
    for (i = 0u; i < 8u; ++i) value |= (u64)p[i] << (i * 8u);
    return value;
}

static u64 pci_bar64(const NTZ_PCI_DEVICE *d, u32 index) {
    u32 low;
    u64 address;
    if (d == (const NTZ_PCI_DEVICE *)0 || index >= 6u) return 0u;
    low = d->bar[index];
    if ((low & 1u) != 0u) return 0u;
    address = (u64)(low & ~0xFu);
    if ((low & 0x06u) == 0x04u && index + 1u < 6u) address |= (u64)d->bar[index + 1u] << 32;
    return address;
}

static int wait_ready(u64 bar, int expected) {
    u32 spins;
    for (spins = 0u; spins < 20000000u; ++spins) {
        int ready = (MmioRead32(bar + NVME_REG_CSTS) & 1u) != 0u;
        if (ready == expected) return 1;
        CpuPause();
    }
    return 0;
}

static u64 sq_doorbell(const NTZ_NVME_CONTROLLER *c) {
    return c->bar + NVME_DOORBELL_BASE;
}

static u64 cq_doorbell(const NTZ_NVME_CONTROLLER *c) {
    return c->bar + NVME_DOORBELL_BASE + c->doorbell_stride;
}

static NTSTATUS admin_submit(NTZ_NVME_CONTROLLER *c, NVME_COMMAND *command, u32 *out_result) {
    volatile NVME_COMMAND *sq;
    volatile NVME_COMPLETION *cq;
    volatile NVME_COMPLETION *completion;
    u16 cid;
    u32 spins;

    if (c == (NTZ_NVME_CONTROLLER *)0 || command == (NVME_COMMAND *)0 || !c->ready) return STATUS_INVALID_PARAMETER;
    sq = (volatile NVME_COMMAND *)c->admin_sq.virtual_address;
    cq = (volatile NVME_COMPLETION *)c->admin_cq.virtual_address;
    cid = ++c->next_cid;
    if (cid == 0u) cid = ++c->next_cid;
    command->cid = cid;
    sq[c->sq_tail] = *command;
    __asm__ volatile ("" ::: "memory");
    c->sq_tail = (u16)((c->sq_tail + 1u) % c->queue_depth);
    MmioWrite32(sq_doorbell(c), c->sq_tail);

    completion = &cq[c->cq_head];
    for (spins = 0u; spins < 30000000u; ++spins) {
        u16 status = completion->status;
        if ((status & 1u) == c->cq_phase) {
            u16 completion_cid = completion->cid;
            u16 status_code = (u16)((status >> 1) & 0x07FFu);
            if (completion_cid != cid) return STATUS_NOT_FOUND;
            if (out_result != (u32 *)0) *out_result = completion->result;
            c->cq_head = (u16)(c->cq_head + 1u);
            if (c->cq_head == c->queue_depth) {
                c->cq_head = 0u;
                c->cq_phase ^= 1u;
            }
            MmioWrite32(cq_doorbell(c), c->cq_head);
            return status_code == 0u ? STATUS_SUCCESS : STATUS_NOT_FOUND;
        }
        CpuPause();
    }
    return STATUS_PENDING;
}

static NTSTATUS identify(NTZ_NVME_CONTROLLER *c, u32 nsid, u32 cns) {
    NVME_COMMAND cmd;
    zero_bytes(&cmd, sizeof(cmd));
    zero_bytes(c->identify.virtual_address, NTZ_PAGE_SIZE);
    cmd.opcode = NVME_ADMIN_IDENTIFY;
    cmd.nsid = nsid;
    cmd.prp1 = c->identify.physical_address;
    cmd.cdw10 = cns;
    return admin_submit(c, &cmd, (u32 *)0);
}

NTSTATUS NvmeInitializePolled(const NTZ_PCI_DEVICE *device) {
    u64 cap;
    u32 cc;
    u32 command_status;
    u32 max_entries;
    u32 mpsmin;
    NTSTATUS status;
    const u8 *id;
    u8 format;
    u8 lbads;

    if (device == (const NTZ_PCI_DEVICE *)0 || device->class_code != 0x01u || device->subclass != 0x08u || device->prog_if != 0x02u) {
        return STATUS_INVALID_PARAMETER;
    }
    zero_bytes(&g_nvme, sizeof(g_nvme));
    g_nvme.bar = pci_bar64(device, 0u);
    if (g_nvme.bar == 0u) return STATUS_NOT_FOUND;

    command_status = PciConfigRead32(device->bus, device->device, device->function, 0x04u);
    command_status |= 0x00000006u; /* Memory Space + Bus Master */
    PciConfigWrite32(device->bus, device->device, device->function, 0x04u, command_status);

    cap = MmioRead64(g_nvme.bar + NVME_REG_CAP);
    max_entries = (u32)(cap & 0xFFFFu) + 1u;
    if (max_entries < 2u) return STATUS_NOT_FOUND;
    g_nvme.queue_depth = (u16)(max_entries < NVME_ADMIN_DEPTH ? max_entries : NVME_ADMIN_DEPTH);
    g_nvme.doorbell_stride = 4u << (u32)((cap >> 32) & 0x0Fu);
    mpsmin = (u32)((cap >> 48) & 0x0Fu);
    if (mpsmin != 0u) return STATUS_NOT_FOUND; /* Alpha uses 4 KiB pages. */

    cc = MmioRead32(g_nvme.bar + NVME_REG_CC);
    if ((cc & 1u) != 0u) {
        MmioWrite32(g_nvme.bar + NVME_REG_CC, cc & ~1u);
        if (!wait_ready(g_nvme.bar, 0)) return STATUS_PENDING;
    }

    status = DmaAllocate(NTZ_PAGE_SIZE, NTZ_PAGE_SIZE, 0xFFFFFFFFULL, 1, &g_nvme.admin_sq);
    if (status != STATUS_SUCCESS) return status;
    status = DmaAllocate(NTZ_PAGE_SIZE, NTZ_PAGE_SIZE, 0xFFFFFFFFULL, 1, &g_nvme.admin_cq);
    if (status != STATUS_SUCCESS) return status;
    status = DmaAllocate(NTZ_PAGE_SIZE, NTZ_PAGE_SIZE, 0xFFFFFFFFULL, 1, &g_nvme.identify);
    if (status != STATUS_SUCCESS) return status;

    MmioWrite32(g_nvme.bar + NVME_REG_AQA,
                ((u32)(g_nvme.queue_depth - 1u) << 16) | (u32)(g_nvme.queue_depth - 1u));
    MmioWrite64(g_nvme.bar + NVME_REG_ASQ, g_nvme.admin_sq.physical_address);
    MmioWrite64(g_nvme.bar + NVME_REG_ACQ, g_nvme.admin_cq.physical_address);

    g_nvme.sq_tail = 0u;
    g_nvme.cq_head = 0u;
    g_nvme.cq_phase = 1u;
    g_nvme.next_cid = 0u;
    g_nvme.ready = 1;

    cc = (6u << 16) | (4u << 20) | 1u;
    MmioWrite32(g_nvme.bar + NVME_REG_CC, cc);
    if (!wait_ready(g_nvme.bar, 1)) {
        g_nvme.ready = 0;
        return STATUS_PENDING;
    }

    status = identify(&g_nvme, 0u, 1u);
    if (status != STATUS_SUCCESS) return status;
    id = (const u8 *)g_nvme.identify.virtual_address;
    SerialWrite("NVME: controller VS=");
    SerialWriteHex(MmioRead32(g_nvme.bar + NVME_REG_VS));
    SerialWrite(" model=");
    {
        u32 i;
        for (i = 24u; i < 64u; ++i) {
            char s[2];
            char ch = (char)id[i];
            if (ch < 32 || ch > 126) ch = ' ';
            s[0] = ch;
            s[1] = '\0';
            SerialWrite(s);
        }
    }
    SerialWrite("\n");

    g_nvme.namespace_id = 1u;
    status = identify(&g_nvme, g_nvme.namespace_id, 0u);
    if (status != STATUS_SUCCESS) return status;
    id = (const u8 *)g_nvme.identify.virtual_address;
    g_nvme.lba_count = read_u64(id + 0u);
    format = (u8)(id[26] & 0x0Fu);
    lbads = id[128u + (u32)format * 4u + 2u];
    if (lbads < 9u || lbads > 12u) return STATUS_NOT_FOUND;
    g_nvme.lba_size = 1u << lbads;

    SerialWrite("NVME: namespace 1 blocks=");
    SerialWriteDec(g_nvme.lba_count);
    SerialWrite(" lba_size=");
    SerialWriteDec(g_nvme.lba_size);
    SerialWrite(" bytes\n");
    return STATUS_SUCCESS;
}

NTSTATUS NvmeReadBlocks(u64 lba, u32 block_count, void *buffer, u32 buffer_bytes) {
    NVME_COMMAND cmd;
    NTZ_DMA_BUFFER dma;
    NTSTATUS status;
    u64 bytes;
    u64 i;
    u8 *dst;
    u8 *src;
    if (!g_nvme.ready || block_count == 0u || buffer == (void *)0 || g_nvme.lba_size == 0u) return STATUS_INVALID_PARAMETER;
    bytes = (u64)block_count * g_nvme.lba_size;
    if (bytes > buffer_bytes || block_count > 65536u) return STATUS_BUFFER_TOO_SMALL;
    if (bytes > NTZ_PAGE_SIZE) return STATUS_BUFFER_TOO_SMALL; /* Alpha: one PRP page only. */

    status = DmaAllocate(NTZ_PAGE_SIZE, NTZ_PAGE_SIZE, 0xFFFFFFFFULL, 1, &dma);
    if (status != STATUS_SUCCESS) return status;
    zero_bytes(&cmd, sizeof(cmd));
    cmd.opcode = NVME_IO_READ;
    cmd.nsid = g_nvme.namespace_id;
    cmd.prp1 = dma.physical_address;
    cmd.cdw10 = (u32)(lba & 0xFFFFFFFFu);
    cmd.cdw11 = (u32)(lba >> 32);
    cmd.cdw12 = block_count - 1u;

    /* The Alpha queue engine currently uses the admin queue transport only for Identify.
       I/O queue creation is the next step, so do not send READ on queue 0. */
    (void)cmd;
    status = STATUS_PENDING;
    if (status == STATUS_SUCCESS) {
        dst = (u8 *)buffer;
        src = (u8 *)dma.virtual_address;
        for (i = 0u; i < bytes; ++i) dst[i] = src[i];
    }
    (void)DmaFree(&dma);
    return status;
}
