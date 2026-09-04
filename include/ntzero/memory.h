#ifndef NTZERO_MEMORY_H
#define NTZERO_MEMORY_H

#include "types.h"
#include "boot.h"

#define NTZ_PAGE_SIZE 4096ULL
#define NTZ_PMM_MAX_RANGES 256u

typedef struct NTZ_MEMORY_STATS {
    u64 total_pages;
    u64 free_pages;
    u64 allocated_pages;
    u32 free_ranges;
} NTZ_MEMORY_STATS;

typedef struct NTZ_DMA_BUFFER {
    void *virtual_address;
    u64 physical_address;
    u64 size;
    u64 page_count;
} NTZ_DMA_BUFFER;

NTSTATUS PmmInitializeFromBoot(const NTZ_BOOT_INFO *boot);
NTSTATUS PmmAllocatePages(u64 page_count, u64 max_address, u64 alignment_pages, u64 *out_physical);
NTSTATUS PmmFreePages(u64 physical_address, u64 page_count);
void PmmGetStats(NTZ_MEMORY_STATS *stats);

NTSTATUS DmaAllocate(u64 bytes, u64 alignment, u64 max_address, int zero_memory, NTZ_DMA_BUFFER *out_buffer);
NTSTATUS DmaFree(NTZ_DMA_BUFFER *buffer);

#endif
