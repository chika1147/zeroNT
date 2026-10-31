#include "include/heap.hpp"
#include "include/paging.hpp"
#include "include/memory.hpp"

namespace {
constexpr uint64_t PAGE_SIZE = 4096;
constexpr uint64_t HEAP_BASE = 0xFFFF800000000000ULL;
constexpr uint64_t HEAP_SIZE = 16ULL * 1024ULL * 1024ULL;
constexpr uint64_t ALIGNMENT = 16;
constexpr uint64_t BLOCK_MAGIC = 0x5A4E544845415042ULL; // "ZNTHEAPB"

struct alignas(16) Block {
    uint64_t magic;
    uint64_t size;
    Block* next;
    bool free;
    uint8_t padding[7];
};

static Block* gHead = nullptr;
static HeapStats gStats{};

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static void memzero(void* ptr, uint64_t bytes) {
    auto* p = static_cast<uint8_t*>(ptr);
    for (uint64_t i = 0; i < bytes; ++i) p[i] = 0;
}

static void memcpy8(void* dst,const void* src,uint64_t n){
    auto*d=static_cast<uint8_t*>(dst);auto*s=static_cast<const uint8_t*>(src);
    for(uint64_t i=0;i<n;++i)d[i]=s[i];
}

static bool in_heap(uint64_t a) {
    return a >= HEAP_BASE && a < HEAP_BASE + HEAP_SIZE;
}

static bool block_valid(const Block* b) {
    if (!b) return false;
    const uint64_t addr = reinterpret_cast<uint64_t>(b);
    if (!in_heap(addr) || (addr & (ALIGNMENT - 1)) != 0 || b->magic != BLOCK_MAGIC) return false;
    const uint64_t data = reinterpret_cast<uint64_t>(b + 1);
    if (data < HEAP_BASE || data > HEAP_BASE + HEAP_SIZE) return false;
    if (b->size > HEAP_BASE + HEAP_SIZE - data) return false;
    if (b->next) {
        const uint64_t next = reinterpret_cast<uint64_t>(b->next);
        if (!in_heap(next) || next <= addr || (next & (ALIGNMENT - 1)) != 0) return false;
    }
    return true;
}

static void recalc_stats() {
    uint64_t used = 0, free = 0, largest = 0;
    bool corrupt = false;
    uint32_t guard = 0;
    for (Block* b = gHead; b && guard++ < 100000; b = b->next) {
        if (!block_valid(b)) { corrupt = true; break; }
        if (b->free) { free += b->size; if (b->size > largest) largest = b->size; }
        else used += b->size;
    }
    if (guard >= 100000) corrupt = true;
    gStats.usedBytes = used;
    gStats.freeBytes = free;
    gStats.largestFreeBlock = largest;
    gStats.corruptionDetected = corrupt;
    if (used > gStats.peakUsedBytes) gStats.peakUsedBytes = used;
}

static void split_block(Block* b, uint64_t wanted) {
    const uint64_t minTail = sizeof(Block) + ALIGNMENT;
    if (!block_valid(b) || b->size < wanted + minTail) return;
    auto* next = reinterpret_cast<Block*>(reinterpret_cast<uint8_t*>(b + 1) + wanted);
    next->magic = BLOCK_MAGIC;
    next->size = b->size - wanted - sizeof(Block);
    next->next = b->next;
    next->free = true;
    b->size = wanted;
    b->next = next;
}

static void coalesce() {
    for (Block* b = gHead; b && b->next; ) {
        if (!block_valid(b) || !block_valid(b->next)) { gStats.corruptionDetected = true; return; }
        const uint64_t expected = reinterpret_cast<uint64_t>(b + 1) + b->size;
        if (b->free && b->next->free && expected == reinterpret_cast<uint64_t>(b->next)) {
            Block* dead = b->next;
            b->size += sizeof(Block) + dead->size;
            b->next = dead->next;
            dead->magic = 0;
        } else b = b->next;
    }
}
}

bool heap_init() {
    gHead = nullptr;
    gStats = {};

    for (uint64_t offset = 0; offset < HEAP_SIZE; offset += PAGE_SIZE) {
        void* physical = pmm_alloc_page();
        if (!physical) return false;
        if (!vmm_map_page(HEAP_BASE + offset, reinterpret_cast<uint64_t>(physical), PagePresent | PageWritable | PageNoExecute)) {
            pmm_free_page(physical);
            return false;
        }
    }

    gHead = reinterpret_cast<Block*>(HEAP_BASE);
    gHead->magic = BLOCK_MAGIC;
    gHead->size = HEAP_SIZE - sizeof(Block);
    gHead->next = nullptr;
    gHead->free = true;
    gStats.totalBytes = HEAP_SIZE - sizeof(Block);
    recalc_stats();
    return true;
}

void* kmalloc(size_t size) {
    ++gStats.totalAllocCalls;
    if (!gHead || size == 0 || gStats.corruptionDetected) { ++gStats.allocationFailures; return nullptr; }
    const uint64_t wanted = align_up(static_cast<uint64_t>(size), ALIGNMENT);
    if (wanted < size) { ++gStats.allocationFailures; return nullptr; }
    for (Block* b = gHead; b; b = b->next) {
        if (!block_valid(b)) { gStats.corruptionDetected = true; ++gStats.allocationFailures; return nullptr; }
        if (!b->free || b->size < wanted) continue;
        split_block(b, wanted);
        b->free = false;
        ++gStats.allocations;
        recalc_stats();
        return static_cast<void*>(b + 1);
    }
    ++gStats.allocationFailures;
    recalc_stats();
    return nullptr;
}

void* kcalloc(size_t count, size_t size) {
    if (!count || !size) return nullptr;
    const uint64_t total = static_cast<uint64_t>(count) * static_cast<uint64_t>(size);
    if (total / count != size) { ++gStats.allocationFailures; return nullptr; }
    void* p = kmalloc(static_cast<size_t>(total));
    if (p) memzero(p, total);
    return p;
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (!size) { kfree(ptr); return nullptr; }
    auto* b = reinterpret_cast<Block*>(ptr) - 1;
    if (!block_valid(b) || b->free) { ++gStats.invalidFrees; return nullptr; }
    const uint64_t wanted = align_up(static_cast<uint64_t>(size), ALIGNMENT);
    if (wanted <= b->size) { split_block(b, wanted); recalc_stats(); return ptr; }
    void* replacement = kmalloc(size);
    if (!replacement) return nullptr;
    memcpy8(replacement, ptr, b->size < wanted ? b->size : wanted);
    kfree(ptr);
    return replacement;
}

void kfree(void* ptr) {
    ++gStats.totalFreeCalls;
    if (!ptr || !gHead) { ++gStats.invalidFrees; return; }
    const uint64_t p = reinterpret_cast<uint64_t>(ptr);
    if (!in_heap(p) || p < HEAP_BASE + sizeof(Block)) { ++gStats.invalidFrees; return; }
    auto* b = reinterpret_cast<Block*>(ptr) - 1;
    if (!block_valid(b)) { ++gStats.invalidFrees; gStats.corruptionDetected = true; return; }
    if (b->free) { ++gStats.doubleFrees; return; }
    b->free = true;
    if (gStats.allocations) --gStats.allocations;
    coalesce();
    recalc_stats();
}

bool heap_validate() {
    recalc_stats();
    return !gStats.corruptionDetected;
}

HeapStats heap_stats() {
    recalc_stats();
    return gStats;
}

uint64_t heap_base() { return HEAP_BASE; }
