#pragma once
#include <stdint.h>
#include "../../shared/bootinfo.hpp"

enum PageFlags : uint64_t {
    PagePresent       = 1ULL << 0,
    PageWritable      = 1ULL << 1,
    PageUser          = 1ULL << 2,
    PageWriteThrough  = 1ULL << 3,
    PageCacheDisable  = 1ULL << 4,
    PageGlobal        = 1ULL << 8,
    // Bits 9-11 are software-defined when a PTE is present.  zeroNT uses bit 9
    // to mark PMM pages owned by an address space so teardown can reclaim them.
    PageOwned         = 1ULL << 9,
    PageNoExecute     = 1ULL << 63,
};

struct PagingStats {
    uint64_t cr3;
    uint64_t pageTablePages;
    uint64_t mapped4KPages;
    uint64_t identityMappedBytes;
    uint64_t addressSpaces;
    uint64_t destroyedAddressSpaces;
    uint64_t ownedUserPages;
    uint64_t reclaimedUserPages;
    uint64_t mapFailures;
    uint64_t unmapFailures;
    uint64_t tlbInvalidations;
    bool active;
};

bool vmm_init(const ZeroNTBootInfo* boot);
bool vmm_map_page(uint64_t virtualAddress, uint64_t physicalAddress, uint64_t flags);
bool vmm_unmap_page(uint64_t virtualAddress);
uint64_t vmm_translate(uint64_t virtualAddress);
bool vmm_query_page(uint64_t virtualAddress, uint64_t& physicalAddress, uint64_t& flags);
PagingStats vmm_stats();

struct VmmAddressSpace {
    uint64_t cr3;
};

bool vmm_create_user_space(VmmAddressSpace& out);
bool vmm_destroy_user_space(VmmAddressSpace& space);
bool vmm_map_page_in_space(VmmAddressSpace& space, uint64_t virtualAddress, uint64_t physicalAddress, uint64_t flags);
bool vmm_unmap_page_in_space(VmmAddressSpace& space, uint64_t virtualAddress);
bool vmm_query_page_in_space(const VmmAddressSpace& space, uint64_t virtualAddress, uint64_t& physicalAddress, uint64_t& flags);
bool vmm_activate_space(const VmmAddressSpace& space);
void vmm_activate_kernel_space();
uint64_t vmm_kernel_cr3();
