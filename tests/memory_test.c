#include <stdio.h>
#include "ntzero/memory.h"

typedef struct TEST_EFI_DESCRIPTOR {
    u32 type;
    u32 pad;
    u64 physical_start;
    u64 virtual_start;
    u64 number_of_pages;
    u64 attribute;
} TEST_EFI_DESCRIPTOR;

int main(void) {
    TEST_EFI_DESCRIPTOR map[3];
    NTZ_BOOT_INFO boot;
    NTZ_MEMORY_STATS stats;
    NTZ_DMA_BUFFER dma;
    u64 a = 0u;
    u64 b = 0u;

    map[0].type = 0u;
    map[0].pad = 0u;
    map[0].physical_start = 0u;
    map[0].virtual_start = 0u;
    map[0].number_of_pages = 256u;
    map[0].attribute = 0u;

    map[1].type = 7u;
    map[1].pad = 0u;
    map[1].physical_start = 0x100000ULL;
    map[1].virtual_start = 0u;
    map[1].number_of_pages = 1024u;
    map[1].attribute = 0u;

    map[2].type = 7u;
    map[2].pad = 0u;
    map[2].physical_start = 0x1000000ULL;
    map[2].virtual_start = 0u;
    map[2].number_of_pages = 2048u;
    map[2].attribute = 0u;

    boot.magic = NTZERO_BOOT_MAGIC;
    boot.version = 0u;
    boot.uefi_memory_map = map;
    boot.uefi_memory_map_size = sizeof(map);
    boot.uefi_descriptor_size = sizeof(TEST_EFI_DESCRIPTOR);
    boot.uefi_descriptor_version = 1u;

    if (PmmInitializeFromBoot(&boot) != STATUS_SUCCESS) return 1;
    PmmGetStats(&stats);
    if (stats.free_pages != 3072u) return 2;

    if (PmmAllocatePages(4u, 0xFFFFFFFFULL, 1u, &a) != STATUS_SUCCESS) return 3;
    if (a != 0x100000ULL) return 4;
    if (PmmAllocatePages(8u, 0xFFFFFFFFULL, 16u, &b) != STATUS_SUCCESS) return 5;
    if ((b & 0xFFFFu) != 0u) return 6;

    if (PmmFreePages(a, 4u) != STATUS_SUCCESS) return 7;
    if (PmmFreePages(b, 8u) != STATUS_SUCCESS) return 8;

    if (DmaAllocate(8192u, 4096u, 0xFFFFFFFFULL, 0, &dma) != STATUS_SUCCESS) return 9;
    if (dma.page_count != 2u || dma.physical_address == 0u) return 10;
    if (DmaFree(&dma) != STATUS_SUCCESS) return 11;

    PmmGetStats(&stats);
    if (stats.free_pages != 3072u || stats.allocated_pages != 0u) return 12;

    printf("PMM/DMA PASS free_pages=%llu ranges=%u\n",
           (unsigned long long)stats.free_pages,
           (unsigned)stats.free_ranges);
    return 0;
}
