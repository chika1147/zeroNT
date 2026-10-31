#pragma once
#include <stdint.h>
#include "../../shared/bootinfo.hpp"

struct PmmStats {
    uint64_t trackedBytes;
    uint64_t freeBytes;
    uint64_t usedBytes;
    uint64_t freePages;
    uint64_t usedPages;
    uint64_t reservedPages;
    uint64_t peakUsedPages;
    uint64_t allocationCalls;
    uint64_t freeCalls;
    uint64_t allocationFailures;
    uint64_t invalidFrees;
    uint64_t doubleFrees;
    uint64_t contiguousAllocations;
};

void pmm_init(const ZeroNTBootInfo* boot);
void* pmm_alloc_page();
void* pmm_alloc_pages(uint32_t pageCount, uint32_t alignmentPages = 1);
void pmm_free_page(void* page);
void pmm_free_pages(void* base, uint32_t pageCount);
bool pmm_reserve_range(uint64_t physicalStart, uint64_t bytes);
bool pmm_is_allocated(const void* page);
bool pmm_validate();
PmmStats pmm_stats();
