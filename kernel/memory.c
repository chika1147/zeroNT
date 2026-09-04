#include "ntzero/memory.h"

#define EFI_CONVENTIONAL_MEMORY 7u

typedef struct NTZ_FREE_RANGE {
    u64 base;
    u64 pages;
} NTZ_FREE_RANGE;

static NTZ_FREE_RANGE g_ranges[NTZ_PMM_MAX_RANGES];
static u32 g_range_count;
static NTZ_MEMORY_STATS g_stats;

static u64 align_up_u64(u64 value, u64 alignment) {
    u64 mask;
    if (alignment <= 1u) return value;
    mask = alignment - 1u;
    return (value + mask) & ~mask;
}

static int is_power_of_two(u64 value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

static u32 read_u32_unaligned(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u64 read_u64_unaligned(const u8 *p) {
    u64 lo = read_u32_unaligned(p);
    u64 hi = read_u32_unaligned(p + 4u);
    return lo | (hi << 32);
}

static void remove_range(u32 index) {
    u32 i;
    if (index >= g_range_count) return;
    for (i = index; i + 1u < g_range_count; ++i) g_ranges[i] = g_ranges[i + 1u];
    --g_range_count;
}

static NTSTATUS insert_range_sorted(u64 base, u64 pages) {
    u32 pos = 0u;
    u32 i;
    if (pages == 0u) return STATUS_INVALID_PARAMETER;
    if (g_range_count >= NTZ_PMM_MAX_RANGES) return STATUS_INSUFFICIENT_RESOURCES;
    while (pos < g_range_count && g_ranges[pos].base < base) ++pos;
    for (i = g_range_count; i > pos; --i) g_ranges[i] = g_ranges[i - 1u];
    g_ranges[pos].base = base;
    g_ranges[pos].pages = pages;
    ++g_range_count;

    if (pos > 0u) {
        NTZ_FREE_RANGE *prev = &g_ranges[pos - 1u];
        NTZ_FREE_RANGE *cur = &g_ranges[pos];
        u64 prev_end = prev->base + prev->pages * NTZ_PAGE_SIZE;
        if (prev_end == cur->base) {
            prev->pages += cur->pages;
            remove_range(pos);
            --pos;
        }
    }
    if (pos + 1u < g_range_count) {
        NTZ_FREE_RANGE *cur = &g_ranges[pos];
        NTZ_FREE_RANGE *next = &g_ranges[pos + 1u];
        u64 cur_end = cur->base + cur->pages * NTZ_PAGE_SIZE;
        if (cur_end == next->base) {
            cur->pages += next->pages;
            remove_range(pos + 1u);
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS PmmInitializeFromBoot(const NTZ_BOOT_INFO *boot) {
    const u8 *cursor;
    u64 offset;
    if (boot == (const NTZ_BOOT_INFO *)0 || boot->uefi_memory_map == (void *)0 ||
        boot->uefi_descriptor_size < 32u || boot->uefi_memory_map_size < boot->uefi_descriptor_size) {
        return STATUS_INVALID_PARAMETER;
    }

    g_range_count = 0u;
    g_stats.total_pages = 0u;
    g_stats.free_pages = 0u;
    g_stats.allocated_pages = 0u;
    g_stats.free_ranges = 0u;
    cursor = (const u8 *)boot->uefi_memory_map;

    for (offset = 0u; offset + boot->uefi_descriptor_size <= boot->uefi_memory_map_size; offset += boot->uefi_descriptor_size) {
        const u8 *d = cursor + offset;
        u32 type = read_u32_unaligned(d + 0u);
        u64 physical = read_u64_unaligned(d + 8u);
        u64 pages = read_u64_unaligned(d + 24u);
        g_stats.total_pages += pages;
        if (type == EFI_CONVENTIONAL_MEMORY && pages != 0u) {
            if (physical < 0x100000ULL) {
                u64 low_pages = (0x100000ULL - physical + NTZ_PAGE_SIZE - 1u) / NTZ_PAGE_SIZE;
                if (low_pages >= pages) continue;
                physical += low_pages * NTZ_PAGE_SIZE;
                pages -= low_pages;
            }
            if (insert_range_sorted(physical, pages) != STATUS_SUCCESS) return STATUS_INSUFFICIENT_RESOURCES;
            g_stats.free_pages += pages;
        }
    }
    g_stats.free_ranges = g_range_count;
    return g_range_count != 0u ? STATUS_SUCCESS : STATUS_NO_MEMORY;
}

NTSTATUS PmmAllocatePages(u64 page_count, u64 max_address, u64 alignment_pages, u64 *out_physical) {
    u32 i;
    if (page_count == 0u || out_physical == (u64 *)0 || alignment_pages == 0u || !is_power_of_two(alignment_pages)) {
        return STATUS_INVALID_PARAMETER;
    }
    if (max_address == 0u) max_address = ~0ULL;

    for (i = 0u; i < g_range_count; ++i) {
        u64 range_start = g_ranges[i].base;
        u64 range_end = range_start + g_ranges[i].pages * NTZ_PAGE_SIZE;
        u64 alignment = alignment_pages * NTZ_PAGE_SIZE;
        u64 alloc_start = align_up_u64(range_start, alignment);
        u64 alloc_bytes = page_count * NTZ_PAGE_SIZE;
        u64 alloc_end;
        u64 before_pages;
        u64 after_pages;
        if (alloc_start < range_start || alloc_bytes / NTZ_PAGE_SIZE != page_count) continue;
        alloc_end = alloc_start + alloc_bytes;
        if (alloc_end < alloc_start || alloc_end > range_end) continue;
        if (alloc_end - 1u > max_address) continue;

        before_pages = (alloc_start - range_start) / NTZ_PAGE_SIZE;
        after_pages = (range_end - alloc_end) / NTZ_PAGE_SIZE;
        if (before_pages != 0u && after_pages != 0u) {
            if (g_range_count >= NTZ_PMM_MAX_RANGES) return STATUS_INSUFFICIENT_RESOURCES;
            {
                u32 j;
                for (j = g_range_count; j > i + 1u; --j) g_ranges[j] = g_ranges[j - 1u];
                g_ranges[i].pages = before_pages;
                g_ranges[i + 1u].base = alloc_end;
                g_ranges[i + 1u].pages = after_pages;
                ++g_range_count;
            }
        } else if (before_pages != 0u) {
            g_ranges[i].pages = before_pages;
        } else if (after_pages != 0u) {
            g_ranges[i].base = alloc_end;
            g_ranges[i].pages = after_pages;
        } else {
            remove_range(i);
        }

        *out_physical = alloc_start;
        g_stats.free_pages -= page_count;
        g_stats.allocated_pages += page_count;
        g_stats.free_ranges = g_range_count;
        return STATUS_SUCCESS;
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS PmmFreePages(u64 physical_address, u64 page_count) {
    NTSTATUS status;
    if ((physical_address & (NTZ_PAGE_SIZE - 1u)) != 0u || page_count == 0u) return STATUS_INVALID_PARAMETER;
    status = insert_range_sorted(physical_address, page_count);
    if (status == STATUS_SUCCESS) {
        g_stats.free_pages += page_count;
        if (g_stats.allocated_pages >= page_count) g_stats.allocated_pages -= page_count;
        g_stats.free_ranges = g_range_count;
    }
    return status;
}

void PmmGetStats(NTZ_MEMORY_STATS *stats) {
    if (stats != (NTZ_MEMORY_STATS *)0) *stats = g_stats;
}

NTSTATUS DmaAllocate(u64 bytes, u64 alignment, u64 max_address, int zero_memory, NTZ_DMA_BUFFER *out_buffer) {
    u64 pages;
    u64 alignment_pages;
    u64 physical;
    NTSTATUS status;
    u64 i;
    volatile u8 *p;
    if (bytes == 0u || out_buffer == (NTZ_DMA_BUFFER *)0) return STATUS_INVALID_PARAMETER;
    if (alignment == 0u) alignment = 1u;
    if (!is_power_of_two(alignment)) return STATUS_INVALID_PARAMETER;
    pages = (bytes + NTZ_PAGE_SIZE - 1u) / NTZ_PAGE_SIZE;
    alignment_pages = alignment <= NTZ_PAGE_SIZE ? 1u : alignment / NTZ_PAGE_SIZE;
    if (alignment > NTZ_PAGE_SIZE && (alignment % NTZ_PAGE_SIZE) != 0u) return STATUS_INVALID_PARAMETER;
    if (!is_power_of_two(alignment_pages)) return STATUS_INVALID_PARAMETER;

    status = PmmAllocatePages(pages, max_address, alignment_pages, &physical);
    if (status != STATUS_SUCCESS) return status;
    out_buffer->physical_address = physical;
    out_buffer->virtual_address = (void *)(usize)physical;
    out_buffer->size = bytes;
    out_buffer->page_count = pages;

    if (zero_memory) {
        p = (volatile u8 *)(usize)physical;
        for (i = 0u; i < pages * NTZ_PAGE_SIZE; ++i) p[i] = 0u;
    }
    return STATUS_SUCCESS;
}

NTSTATUS DmaFree(NTZ_DMA_BUFFER *buffer) {
    NTSTATUS status;
    if (buffer == (NTZ_DMA_BUFFER *)0 || buffer->page_count == 0u) return STATUS_INVALID_PARAMETER;
    status = PmmFreePages(buffer->physical_address, buffer->page_count);
    if (status == STATUS_SUCCESS) {
        buffer->virtual_address = (void *)0;
        buffer->physical_address = 0u;
        buffer->size = 0u;
        buffer->page_count = 0u;
    }
    return status;
}
