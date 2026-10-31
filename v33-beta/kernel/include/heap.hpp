#pragma once
#include <stddef.h>
#include <stdint.h>

struct HeapStats {
    uint64_t totalBytes;
    uint64_t usedBytes;
    uint64_t freeBytes;
    uint64_t allocations;
    uint64_t totalAllocCalls;
    uint64_t totalFreeCalls;
    uint64_t allocationFailures;
    uint64_t invalidFrees;
    uint64_t doubleFrees;
    uint64_t peakUsedBytes;
    uint64_t largestFreeBlock;
    bool corruptionDetected;
};

bool heap_init();
void* kmalloc(size_t size);
void* kcalloc(size_t count, size_t size);
void* krealloc(void* ptr, size_t size);
void kfree(void* ptr);
bool heap_validate();
HeapStats heap_stats();
uint64_t heap_base();
