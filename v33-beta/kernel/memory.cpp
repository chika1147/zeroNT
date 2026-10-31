#include "include/memory.hpp"
#include <stddef.h>

namespace {
constexpr uint64_t PAGE_SIZE = 4096;
constexpr uint64_t MAX_TRACKED_MEMORY = 4ULL * 1024 * 1024 * 1024;
constexpr uint64_t MAX_PAGES = MAX_TRACKED_MEMORY / PAGE_SIZE;
constexpr uint64_t BITMAP_BYTES = MAX_PAGES / 8;
constexpr uint64_t LOW_RESERVED_PAGES = 256; // first 1 MiB

// 128 KiB tracks the first 4 GiB.  zeroNT currently identity maps this range,
// which also makes these pages safe for early DMA and page-table allocation.
alignas(4096) static uint8_t gBitmap[BITMAP_BYTES];
static uint64_t gTrackedPages = 0;
static uint64_t gFreePages = 0;
static uint64_t gSearchStart = LOW_RESERVED_PAGES;
static PmmStats gStats{};

struct UefiMemoryDescriptor {
    uint32_t type;
    uint32_t pad;
    uint64_t physicalStart;
    uint64_t virtualStart;
    uint64_t numberOfPages;
    uint64_t attribute;
};

static inline void bit_set(uint64_t page) {
    if (page >= MAX_PAGES) return;
    gBitmap[page >> 3] |= static_cast<uint8_t>(1u << (page & 7));
}

static inline void bit_clear(uint64_t page) {
    if (page >= MAX_PAGES) return;
    gBitmap[page >> 3] &= static_cast<uint8_t>(~(1u << (page & 7)));
}

static inline bool bit_test(uint64_t page) {
    if (page >= MAX_PAGES) return true;
    return (gBitmap[page >> 3] & static_cast<uint8_t>(1u << (page & 7))) != 0;
}

static void memzero(void* ptr, uint64_t bytes) {
    auto* p = static_cast<uint8_t*>(ptr);
    for (uint64_t i = 0; i < bytes; ++i) p[i] = 0;
}

static uint64_t current_used_pages() {
    return gTrackedPages > gFreePages ? gTrackedPages - gFreePages : 0;
}

static void refresh_usage_stats() {
    gStats.freePages = gFreePages;
    gStats.usedPages = current_used_pages();
    gStats.trackedBytes = gTrackedPages * PAGE_SIZE;
    gStats.freeBytes = gFreePages * PAGE_SIZE;
    gStats.usedBytes = gStats.usedPages * PAGE_SIZE;
    if (gStats.usedPages > gStats.peakUsedPages) gStats.peakUsedPages = gStats.usedPages;
}

static bool valid_alignment(uint32_t alignmentPages) {
    return alignmentPages != 0 && (alignmentPages & (alignmentPages - 1u)) == 0;
}

static bool run_free(uint64_t first, uint32_t count) {
    if (!count || first < LOW_RESERVED_PAGES || first + count < first || first + count > gTrackedPages) return false;
    for (uint32_t i = 0; i < count; ++i) if (bit_test(first + i)) return false;
    return true;
}

static void mark_run_used(uint64_t first, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        bit_set(first + i);
        if (gFreePages) --gFreePages;
    }
}
}

void pmm_init(const ZeroNTBootInfo* boot) {
    for (uint64_t i = 0; i < BITMAP_BYTES; ++i) gBitmap[i] = 0xFF;
    gTrackedPages = 0;
    gFreePages = 0;
    gSearchStart = LOW_RESERVED_PAGES;
    gStats = {};

    if (!boot || !boot->memoryMap || !boot->memoryDescriptorSize) return;

    const uint64_t count = boot->memoryMapSize / boot->memoryDescriptorSize;
    const auto* base = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(boot->memoryMap));

    for (uint64_t i = 0; i < count; ++i) {
        const auto* d = reinterpret_cast<const UefiMemoryDescriptor*>(base + i * boot->memoryDescriptorSize);
        uint64_t end = d->physicalStart + d->numberOfPages * PAGE_SIZE;
        if (end < d->physicalStart) continue;
        if (end > MAX_TRACKED_MEMORY) end = MAX_TRACKED_MEMORY;
        const uint64_t start = d->physicalStart;
        if (start >= MAX_TRACKED_MEMORY || end <= start) continue;

        const uint64_t firstPage = start / PAGE_SIZE;
        const uint64_t lastPage = end / PAGE_SIZE;
        if (lastPage > gTrackedPages) gTrackedPages = lastPage;

        // UEFI ConventionalMemory is reusable after ExitBootServices().
        if (d->type != 7) continue;
        for (uint64_t page = firstPage; page < lastPage; ++page) {
            if (page < LOW_RESERVED_PAGES) continue;
            if (bit_test(page)) {
                bit_clear(page);
                ++gFreePages;
            }
        }
    }

    if (gTrackedPages > MAX_PAGES) gTrackedPages = MAX_PAGES;
    gStats.reservedPages = current_used_pages();
    refresh_usage_stats();
}

void* pmm_alloc_pages(uint32_t pageCount, uint32_t alignmentPages) {
    ++gStats.allocationCalls;
    if (!pageCount || !gFreePages || pageCount > gFreePages || !valid_alignment(alignmentPages)) {
        ++gStats.allocationFailures;
        return nullptr;
    }
    if (gTrackedPages > MAX_PAGES) gTrackedPages = MAX_PAGES;

    for (uint32_t pass = 0; pass < 2; ++pass) {
        uint64_t begin = pass == 0 ? gSearchStart : LOW_RESERVED_PAGES;
        uint64_t end = pass == 0 ? gTrackedPages : gSearchStart;
        if (begin < LOW_RESERVED_PAGES) begin = LOW_RESERVED_PAGES;
        if (end > gTrackedPages) end = gTrackedPages;

        uint64_t page = (begin + alignmentPages - 1u) & ~(static_cast<uint64_t>(alignmentPages) - 1u);
        while (page < end) {
            if (page + pageCount > end || page + pageCount < page) break;
            if (run_free(page, pageCount)) {
                mark_run_used(page, pageCount);
                gSearchStart = page + pageCount;
                if (gSearchStart >= gTrackedPages) gSearchStart = LOW_RESERVED_PAGES;
                if (pageCount > 1) ++gStats.contiguousAllocations;
                auto* result = reinterpret_cast<void*>(static_cast<uintptr_t>(page * PAGE_SIZE));
                memzero(result, static_cast<uint64_t>(pageCount) * PAGE_SIZE);
                refresh_usage_stats();
                return result;
            }
            page += alignmentPages;
        }
    }

    ++gStats.allocationFailures;
    refresh_usage_stats();
    return nullptr;
}

void* pmm_alloc_page() {
    return pmm_alloc_pages(1, 1);
}

void pmm_free_pages(void* ptr, uint32_t pageCount) {
    ++gStats.freeCalls;
    const uint64_t address = reinterpret_cast<uint64_t>(ptr);
    if (!ptr || !pageCount || (address & (PAGE_SIZE - 1)) != 0) {
        ++gStats.invalidFrees;
        return;
    }
    const uint64_t first = address / PAGE_SIZE;
    if (first < LOW_RESERVED_PAGES || first >= gTrackedPages || first + pageCount < first || first + pageCount > gTrackedPages) {
        ++gStats.invalidFrees;
        return;
    }

    bool anyFreed = false;
    for (uint32_t i = 0; i < pageCount; ++i) {
        const uint64_t page = first + i;
        if (!bit_test(page)) {
            ++gStats.doubleFrees;
            continue;
        }
        bit_clear(page);
        ++gFreePages;
        anyFreed = true;
    }
    if (anyFreed && first < gSearchStart) gSearchStart = first;
    refresh_usage_stats();
}

void pmm_free_page(void* ptr) {
    pmm_free_pages(ptr, 1);
}

bool pmm_reserve_range(uint64_t physicalStart, uint64_t bytes) {
    if (!bytes) return true;
    uint64_t end = physicalStart + bytes;
    if (end < physicalStart) return false;
    uint64_t first = physicalStart / PAGE_SIZE;
    uint64_t last = (end + PAGE_SIZE - 1) / PAGE_SIZE;
    if (first >= gTrackedPages) return false;
    if (last > gTrackedPages) last = gTrackedPages;
    for (uint64_t page = first; page < last; ++page) {
        if (!bit_test(page)) {
            bit_set(page);
            if (gFreePages) --gFreePages;
            ++gStats.reservedPages;
        }
    }
    refresh_usage_stats();
    return true;
}

bool pmm_is_allocated(const void* ptr) {
    const uint64_t address = reinterpret_cast<uint64_t>(ptr);
    if (!ptr || (address & (PAGE_SIZE - 1)) != 0) return false;
    const uint64_t page = address / PAGE_SIZE;
    return page < gTrackedPages && bit_test(page);
}

bool pmm_validate() {
    if (gTrackedPages > MAX_PAGES || gFreePages > gTrackedPages) return false;
    uint64_t countedFree = 0;
    for (uint64_t page = 0; page < gTrackedPages; ++page) {
        if (!bit_test(page)) ++countedFree;
        if (page < LOW_RESERVED_PAGES && !bit_test(page)) return false;
    }
    return countedFree == gFreePages;
}

PmmStats pmm_stats() {
    refresh_usage_stats();
    return gStats;
}
