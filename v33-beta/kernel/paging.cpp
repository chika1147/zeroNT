#include "include/paging.hpp"
#include "include/memory.hpp"
#include "include/arch.hpp"
#include <stddef.h>

namespace {
constexpr uint64_t PAGE_SIZE = 4096;
constexpr uint64_t HUGE_PAGE_SIZE = 2ULL * 1024 * 1024;
constexpr uint64_t IDENTITY_LIMIT = 4ULL * 1024 * 1024 * 1024;
constexpr uint64_t ENTRY_ADDR_MASK = 0x000FFFFFFFFFF000ULL;
constexpr uint64_t HUGE_ADDR_MASK = 0x000FFFFFFFE00000ULL;
constexpr uint64_t ENTRY_PRESENT = 1ULL << 0;
constexpr uint64_t ENTRY_WRITABLE = 1ULL << 1;
constexpr uint64_t ENTRY_HUGE = 1ULL << 7;

static uint64_t* gPml4 = nullptr;
static uint64_t* gCurrentPml4 = nullptr;
static PagingStats gStats{};

static void zero_page(void* ptr) {
    auto* p = static_cast<uint8_t*>(ptr);
    for (uint64_t i = 0; i < PAGE_SIZE; ++i) p[i] = 0;
}

static uint64_t* alloc_table() {
    void* page = pmm_alloc_page();
    if (!page) return nullptr;
    zero_page(page);
    ++gStats.pageTablePages;
    return static_cast<uint64_t*>(page);
}

static bool ensure_table(uint64_t* parent, uint16_t index, uint64_t flags, uint64_t*& out) {
    uint64_t entry = parent[index];
    if (entry & ENTRY_PRESENT) {
        if (entry & ENTRY_HUGE) return false;
        // U/S must be set at every level for a Ring-3 mapping.
        // Lower supervisor leaves remain inaccessible, so this does not expose
        // the kernel's 0-4 GiB identity map to user mode.
        if (flags & PageUser) parent[index] = entry | PageUser;
        out = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(entry & ENTRY_ADDR_MASK));
        return true;
    }

    uint64_t* table = alloc_table();
    if (!table) return false;
    parent[index] = (reinterpret_cast<uint64_t>(table) & ENTRY_ADDR_MASK) |
                    ENTRY_PRESENT | ENTRY_WRITABLE | (flags & PageUser);
    out = table;
    return true;
}

static bool map_page_root(uint64_t* root, uint64_t virtualAddress, uint64_t physicalAddress,
                          uint64_t flags, bool countStats, bool invalidate) {
    if (!root) return false;
    virtualAddress &= ~(PAGE_SIZE - 1);
    physicalAddress &= ~(PAGE_SIZE - 1);

    const uint16_t pml4i = static_cast<uint16_t>((virtualAddress >> 39) & 0x1FF);
    const uint16_t pdpti = static_cast<uint16_t>((virtualAddress >> 30) & 0x1FF);
    const uint16_t pdi   = static_cast<uint16_t>((virtualAddress >> 21) & 0x1FF);
    const uint16_t pti   = static_cast<uint16_t>((virtualAddress >> 12) & 0x1FF);

    uint64_t* pdpt = nullptr;
    uint64_t* pd = nullptr;
    uint64_t* pt = nullptr;
    if (!ensure_table(root, pml4i, flags, pdpt)) return false;
    if (!ensure_table(pdpt, pdpti, flags, pd)) return false;
    if (!ensure_table(pd, pdi, flags, pt)) return false;

    const bool existed = (pt[pti] & ENTRY_PRESENT) != 0;
    pt[pti] = (physicalAddress & ENTRY_ADDR_MASK) |
              ENTRY_PRESENT |
              (flags & (PageWritable | PageUser | PageWriteThrough |
                        PageCacheDisable | PageGlobal | PageOwned | PageNoExecute));
    if (countStats && !existed) ++gStats.mapped4KPages;
    if (!existed && (flags & PageOwned)) ++gStats.ownedUserPages;
    if (invalidate && gStats.active) { zeront_invlpg(virtualAddress); ++gStats.tlbInvalidations; }
    return true;
}

static bool unmap_page_root(uint64_t* root, uint64_t virtualAddress,
                            bool countStats, bool invalidate) {
    if (!root) return false;
    virtualAddress &= ~(PAGE_SIZE - 1);

    const uint16_t pml4i = static_cast<uint16_t>((virtualAddress >> 39) & 0x1FF);
    const uint16_t pdpti = static_cast<uint16_t>((virtualAddress >> 30) & 0x1FF);
    const uint16_t pdi   = static_cast<uint16_t>((virtualAddress >> 21) & 0x1FF);
    const uint16_t pti   = static_cast<uint16_t>((virtualAddress >> 12) & 0x1FF);

    const uint64_t pml4e = root[pml4i];
    if (!(pml4e & ENTRY_PRESENT) || (pml4e & ENTRY_HUGE)) return false;
    auto* pdpt = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(pml4e & ENTRY_ADDR_MASK));
    const uint64_t pdpte = pdpt[pdpti];
    if (!(pdpte & ENTRY_PRESENT) || (pdpte & ENTRY_HUGE)) return false;
    auto* pd = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(pdpte & ENTRY_ADDR_MASK));
    const uint64_t pde = pd[pdi];
    if (!(pde & ENTRY_PRESENT) || (pde & ENTRY_HUGE)) return false;
    auto* pt = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(pde & ENTRY_ADDR_MASK));
    if (!(pt[pti] & ENTRY_PRESENT)) return false;

    const uint64_t old = pt[pti];
    pt[pti] = 0;
    if (countStats && gStats.mapped4KPages) --gStats.mapped4KPages;
    if ((old & PageOwned) && gStats.ownedUserPages) --gStats.ownedUserPages;
    if (invalidate && gStats.active) { zeront_invlpg(virtualAddress); ++gStats.tlbInvalidations; }
    return true;
}

static uint64_t translate_root(uint64_t* root, uint64_t virtualAddress) {
    if (!root) return 0;
    const uint16_t pml4i = static_cast<uint16_t>((virtualAddress >> 39) & 0x1FF);
    const uint16_t pdpti = static_cast<uint16_t>((virtualAddress >> 30) & 0x1FF);
    const uint16_t pdi   = static_cast<uint16_t>((virtualAddress >> 21) & 0x1FF);
    const uint16_t pti   = static_cast<uint16_t>((virtualAddress >> 12) & 0x1FF);

    const uint64_t pml4e = root[pml4i];
    if (!(pml4e & ENTRY_PRESENT)) return 0;
    auto* pdpt = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(pml4e & ENTRY_ADDR_MASK));
    const uint64_t pdpte = pdpt[pdpti];
    if (!(pdpte & ENTRY_PRESENT)) return 0;
    auto* pd = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(pdpte & ENTRY_ADDR_MASK));
    const uint64_t pde = pd[pdi];
    if (!(pde & ENTRY_PRESENT)) return 0;
    if (pde & ENTRY_HUGE)
        return (pde & HUGE_ADDR_MASK) | (virtualAddress & (HUGE_PAGE_SIZE - 1));
    auto* pt = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(pde & ENTRY_ADDR_MASK));
    const uint64_t pte = pt[pti];
    if (!(pte & ENTRY_PRESENT)) return 0;
    return (pte & ENTRY_ADDR_MASK) | (virtualAddress & (PAGE_SIZE - 1));
}

static bool query_root(uint64_t* root, uint64_t virtualAddress,
                       uint64_t& physicalAddress, uint64_t& flags) {
    physicalAddress = 0;
    flags = 0;
    if (!root) return false;

    const uint16_t pml4i = static_cast<uint16_t>((virtualAddress >> 39) & 0x1FF);
    const uint16_t pdpti = static_cast<uint16_t>((virtualAddress >> 30) & 0x1FF);
    const uint16_t pdi   = static_cast<uint16_t>((virtualAddress >> 21) & 0x1FF);
    const uint16_t pti   = static_cast<uint16_t>((virtualAddress >> 12) & 0x1FF);

    const uint64_t pml4e = root[pml4i];
    if (!(pml4e & ENTRY_PRESENT)) return false;
    auto* pdpt = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(pml4e & ENTRY_ADDR_MASK));
    const uint64_t pdpte = pdpt[pdpti];
    if (!(pdpte & ENTRY_PRESENT)) return false;
    auto* pd = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(pdpte & ENTRY_ADDR_MASK));
    const uint64_t pde = pd[pdi];
    if (!(pde & ENTRY_PRESENT)) return false;
    if (pde & ENTRY_HUGE) {
        physicalAddress = (pde & HUGE_ADDR_MASK) | (virtualAddress & (HUGE_PAGE_SIZE - 1));
        flags = pde;
        return true;
    }
    auto* pt = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(pde & ENTRY_ADDR_MASK));
    const uint64_t pte = pt[pti];
    if (!(pte & ENTRY_PRESENT)) return false;
    physicalAddress = (pte & ENTRY_ADDR_MASK) | (virtualAddress & (PAGE_SIZE - 1));
    flags = pte;
    return true;
}

static bool map_identity_above_4g(uint64_t start, uint64_t bytes, uint64_t flags) {
    if (!bytes) return true;
    uint64_t end = start + bytes;
    if (end < start) return false;
    if (end <= IDENTITY_LIMIT) return true;
    if (start < IDENTITY_LIMIT) start = IDENTITY_LIMIT;
    start &= ~(PAGE_SIZE - 1);
    end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (uint64_t address = start; address < end; address += PAGE_SIZE) {
        if (!vmm_map_page(address, address, flags)) return false;
    }
    return true;
}

static void free_user_tables_from_pml4_entry(uint64_t entry) {
    if (!(entry & ENTRY_PRESENT) || !(entry & PageUser) || (entry & ENTRY_HUGE)) return;
    auto* pdpt = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(entry & ENTRY_ADDR_MASK));
    for (uint32_t i=0;i<512;++i) {
        uint64_t pdpte = pdpt[i];
        if (!(pdpte & ENTRY_PRESENT) || !(pdpte & PageUser) || (pdpte & ENTRY_HUGE)) continue;
        auto* pd = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(pdpte & ENTRY_ADDR_MASK));
        for (uint32_t j=0;j<512;++j) {
            uint64_t pde = pd[j];
            if (!(pde & ENTRY_PRESENT) || !(pde & PageUser) || (pde & ENTRY_HUGE)) continue;
            auto* pt = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(pde & ENTRY_ADDR_MASK));
            for (uint32_t k=0;k<512;++k) {
                const uint64_t pte = pt[k];
                if (!(pte & ENTRY_PRESENT) || !(pte & PageUser) || !(pte & PageOwned)) continue;
                void* phys = reinterpret_cast<void*>(static_cast<uintptr_t>(pte & ENTRY_ADDR_MASK));
                pmm_free_page(phys);
                ++gStats.reclaimedUserPages;
                if (gStats.ownedUserPages) --gStats.ownedUserPages;
                pt[k]=0;
            }
            pmm_free_page(pt);
            if (gStats.pageTablePages) --gStats.pageTablePages;
        }
        pmm_free_page(pd);
        if (gStats.pageTablePages) --gStats.pageTablePages;
    }
    pmm_free_page(pdpt);
    if (gStats.pageTablePages) --gStats.pageTablePages;
}
}

bool vmm_init(const ZeroNTBootInfo* boot) {
    gStats = {};
    gPml4 = alloc_table();
    if (!gPml4) return false;

    uint64_t* pdpt = alloc_table();
    if (!pdpt) return false;
    gPml4[0] = (reinterpret_cast<uint64_t>(pdpt) & ENTRY_ADDR_MASK) |
               ENTRY_PRESENT | ENTRY_WRITABLE;

    // Identity-map the first 4 GiB with supervisor-only 2 MiB pages.
    for (uint64_t pdptIndex = 0; pdptIndex < 4; ++pdptIndex) {
        uint64_t* pd = alloc_table();
        if (!pd) return false;
        pdpt[pdptIndex] = (reinterpret_cast<uint64_t>(pd) & ENTRY_ADDR_MASK) |
                          ENTRY_PRESENT | ENTRY_WRITABLE;
        for (uint64_t i = 0; i < 512; ++i) {
            const uint64_t physical = (pdptIndex * 512ULL + i) * HUGE_PAGE_SIZE;
            pd[i] = physical | ENTRY_PRESENT | ENTRY_WRITABLE | ENTRY_HUGE;
        }
    }
    gStats.identityMappedBytes = IDENTITY_LIMIT;

    if (boot) {
        if (!map_identity_above_4g(reinterpret_cast<uint64_t>(boot), PAGE_SIZE,
                                   PagePresent | PageWritable)) return false;
        if (!map_identity_above_4g(boot->memoryMap, boot->memoryMapSize,
                                   PagePresent | PageWritable)) return false;
        if (!map_identity_above_4g(boot->framebuffer, boot->framebufferSize,
                                   PagePresent | PageWritable |
                                   PageWriteThrough | PageCacheDisable)) return false;
        if (boot->acpiRsdp &&
            !map_identity_above_4g(boot->acpiRsdp & ~(PAGE_SIZE - 1), PAGE_SIZE,
                                   PagePresent)) return false;
        if (boot->rootfsBase && boot->rootfsSize &&
            !map_identity_above_4g(boot->rootfsBase, boot->rootfsSize,
                                   PagePresent | PageWritable)) return false;
    }

    const uint64_t cr3 = reinterpret_cast<uint64_t>(gPml4);
    zeront_load_cr3(cr3);
    gCurrentPml4 = gPml4;
    gStats.cr3 = cr3;
    gStats.active = true;
    return true;
}

bool vmm_map_page(uint64_t virtualAddress, uint64_t physicalAddress, uint64_t flags) {
    const bool ok=map_page_root(gPml4, virtualAddress, physicalAddress, flags,
                                true, gCurrentPml4 == gPml4);
    if(!ok)++gStats.mapFailures;
    return ok;
}

bool vmm_unmap_page(uint64_t virtualAddress) {
    const bool ok=unmap_page_root(gPml4, virtualAddress, true, gCurrentPml4 == gPml4);
    if(!ok)++gStats.unmapFailures;
    return ok;
}

uint64_t vmm_translate(uint64_t virtualAddress) {
    return translate_root(gCurrentPml4 ? gCurrentPml4 : gPml4, virtualAddress);
}

bool vmm_query_page(uint64_t virtualAddress, uint64_t& physicalAddress, uint64_t& flags) {
    return query_root(gCurrentPml4 ? gCurrentPml4 : gPml4,
                      virtualAddress, physicalAddress, flags);
}

PagingStats vmm_stats() {
    return gStats;
}

bool vmm_create_user_space(VmmAddressSpace& out) {
    out = {};
    if (!gPml4) return false;
    uint64_t* root = alloc_table();
    if (!root) return false;

    // Share all supervisor-only kernel top-level mappings. User top-level
    // mappings (for example the Ring-3 smoke-test slot) are deliberately not
    // inherited, so each process gets a private user address-space subtree.
    for (uint32_t i=0;i<512;++i) {
        const uint64_t e = gPml4[i];
        if ((e & ENTRY_PRESENT) && !(e & PageUser)) root[i] = e;
    }
    out.cr3 = reinterpret_cast<uint64_t>(root);
    ++gStats.addressSpaces;
    return true;
}

bool vmm_destroy_user_space(VmmAddressSpace& space) {
    if (!space.cr3 || space.cr3 == reinterpret_cast<uint64_t>(gPml4)) return false;
    auto* root = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(space.cr3));
    if (gCurrentPml4 == root) vmm_activate_kernel_space();
    for (uint32_t i=0;i<512;++i) {
        const uint64_t e = root[i];
        if ((e & ENTRY_PRESENT) && (e & PageUser))
            free_user_tables_from_pml4_entry(e);
    }
    pmm_free_page(root);
    if (gStats.pageTablePages) --gStats.pageTablePages;
    space.cr3 = 0;
    if(gStats.addressSpaces)--gStats.addressSpaces;
    ++gStats.destroyedAddressSpaces;
    return true;
}

bool vmm_map_page_in_space(VmmAddressSpace& space, uint64_t virtualAddress,
                           uint64_t physicalAddress, uint64_t flags) {
    if (!space.cr3) { ++gStats.mapFailures; return false; }
    auto* root = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(space.cr3));
    const bool ok=map_page_root(root, virtualAddress, physicalAddress, flags,
                                false, gCurrentPml4 == root);
    if(!ok)++gStats.mapFailures;
    return ok;
}

bool vmm_unmap_page_in_space(VmmAddressSpace& space, uint64_t virtualAddress) {
    if (!space.cr3) { ++gStats.unmapFailures; return false; }
    auto* root = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(space.cr3));
    const bool ok=unmap_page_root(root, virtualAddress, false, gCurrentPml4 == root);
    if(!ok)++gStats.unmapFailures;
    return ok;
}

bool vmm_query_page_in_space(const VmmAddressSpace& space,uint64_t virtualAddress,
                             uint64_t& physicalAddress,uint64_t& flags){
    if(!space.cr3){physicalAddress=flags=0;return false;}
    auto* root=reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(space.cr3));
    return query_root(root,virtualAddress,physicalAddress,flags);
}

bool vmm_activate_space(const VmmAddressSpace& space) {
    if (!space.cr3) return false;
    auto* root = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(space.cr3));
    zeront_load_cr3(space.cr3);
    gCurrentPml4 = root;
    return true;
}

void vmm_activate_kernel_space() {
    if (!gPml4) return;
    zeront_load_cr3(reinterpret_cast<uint64_t>(gPml4));
    gCurrentPml4 = gPml4;
}

uint64_t vmm_kernel_cr3() {
    return reinterpret_cast<uint64_t>(gPml4);
}
