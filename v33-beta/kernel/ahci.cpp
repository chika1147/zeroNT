#include "include/ahci.hpp"
#include "include/memory.hpp"
#include "include/paging.hpp"
#include <stddef.h>

namespace {
constexpr uint32_t SATA_SIG_ATA = 0x00000101;
constexpr uint8_t FIS_TYPE_REG_H2D = 0x27;
constexpr uint8_t ATA_CMD_IDENTIFY = 0xEC;
constexpr uint8_t ATA_CMD_READ_DMA_EXT = 0x25;
constexpr uint8_t ATA_CMD_WRITE_DMA_EXT = 0x35;
constexpr uint8_t ATA_CMD_FLUSH_CACHE_EXT = 0xEA;
constexpr uint32_t HBA_PxCMD_ST = 1u << 0;
constexpr uint32_t HBA_PxCMD_FRE = 1u << 4;
constexpr uint32_t HBA_PxCMD_FR = 1u << 14;
constexpr uint32_t HBA_PxCMD_CR = 1u << 15;
constexpr uint32_t HBA_PxIS_TFES = 1u << 30;
constexpr uint32_t HBA_GHC_AE = 1u << 31;
constexpr uint32_t SATA_DET_PRESENT = 3;
constexpr uint32_t SATA_IPM_ACTIVE = 1;
constexpr uint32_t TIMEOUT = 2000000;

struct __attribute__((packed)) HbaPort {
    uint32_t clb; uint32_t clbu; uint32_t fb; uint32_t fbu;
    uint32_t is; uint32_t ie; uint32_t cmd; uint32_t rsv0;
    uint32_t tfd; uint32_t sig; uint32_t ssts; uint32_t sctl;
    uint32_t serr; uint32_t sact; uint32_t ci; uint32_t sntf;
    uint32_t fbs; uint32_t rsv1[11]; uint32_t vendor[4];
};

struct __attribute__((packed)) HbaMem {
    uint32_t cap; uint32_t ghc; uint32_t is; uint32_t pi; uint32_t vs;
    uint32_t ccc_ctl; uint32_t ccc_pts; uint32_t em_loc; uint32_t em_ctl;
    uint32_t cap2; uint32_t bohc;
    uint8_t rsv[0xA0 - 0x2C];
    uint8_t vendor[0x100 - 0xA0];
    HbaPort ports[32];
};

struct __attribute__((packed)) HbaCmdHeader {
    uint16_t flags;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv1[4];
};

struct __attribute__((packed)) HbaPrdtEntry {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;
    uint32_t dbc;
};

struct __attribute__((packed)) HbaCmdTable {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t rsv[48];
    HbaPrdtEntry prdt[1];
};

struct __attribute__((packed)) FisRegH2D {
    uint8_t fisType;
    uint8_t pmportC;
    uint8_t command;
    uint8_t featureLow;
    uint8_t lba0; uint8_t lba1; uint8_t lba2; uint8_t device;
    uint8_t lba3; uint8_t lba4; uint8_t lba5; uint8_t featureHigh;
    uint8_t countLow; uint8_t countHigh; uint8_t icc; uint8_t control;
    uint8_t rsv1[4];
};

struct AhciContext {
    volatile HbaMem* hba;
    volatile HbaPort* port;
    HbaCmdHeader* commandList;
    HbaCmdTable* commandTable;
    void* commandListPage;
    void* commandTablePage;
    void* fisArea;
    uint64_t sectorCount;
    uint8_t portIndex;
};

static AhciContext gCtx{};
static AhciStats gStats{};
static bool gLastCommandTimedOut=false;

static void zero_bytes(void* ptr, size_t n) {
    auto* p = static_cast<uint8_t*>(ptr);
    for (size_t i=0;i<n;++i) p[i]=0;
}

static void release_port_pages() {
    if (gCtx.commandListPage) { pmm_free_page(gCtx.commandListPage); gCtx.commandListPage=nullptr; }
    if (gCtx.commandTablePage) { pmm_free_page(gCtx.commandTablePage); gCtx.commandTablePage=nullptr; }
    if (gCtx.fisArea) { pmm_free_page(gCtx.fisArea); gCtx.fisArea=nullptr; }
    gCtx.commandList=nullptr; gCtx.commandTable=nullptr; gCtx.port=nullptr;
}

static bool wait_clear(volatile uint32_t& reg, uint32_t mask) {
    for (uint32_t i=0;i<TIMEOUT;++i) if ((reg & mask)==0) return true;
    return false;
}

static bool stop_port(volatile HbaPort* port) {
    port->cmd &= ~HBA_PxCMD_ST;
    if (!wait_clear(port->cmd, HBA_PxCMD_CR)) return false;
    port->cmd &= ~HBA_PxCMD_FRE;
    return wait_clear(port->cmd, HBA_PxCMD_FR);
}

static bool start_port(volatile HbaPort* port) {
    if (!wait_clear(port->cmd, HBA_PxCMD_CR)) return false;
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
    return true;
}

static bool port_present(volatile HbaPort* port) {
    const uint32_t ssts = port->ssts;
    const uint32_t det = ssts & 0x0F;
    const uint32_t ipm = (ssts >> 8) & 0x0F;
    return det == SATA_DET_PRESENT && ipm == SATA_IPM_ACTIVE && port->sig == SATA_SIG_ATA;
}

static bool setup_port(uint8_t index) {
    volatile HbaPort* port = &gCtx.hba->ports[index];
    if (!stop_port(port)) return false;

    void* clbPage = pmm_alloc_page();
    void* fisPage = pmm_alloc_page();
    void* tablePage = pmm_alloc_page();
    if (!clbPage || !fisPage || !tablePage) { if(clbPage)pmm_free_page(clbPage); if(fisPage)pmm_free_page(fisPage); if(tablePage)pmm_free_page(tablePage); return false; }
    zero_bytes(clbPage,4096); zero_bytes(fisPage,4096); zero_bytes(tablePage,4096);

    const uint64_t clbPhys = vmm_translate(reinterpret_cast<uint64_t>(clbPage));
    const uint64_t fisPhys = vmm_translate(reinterpret_cast<uint64_t>(fisPage));
    const uint64_t tablePhys = vmm_translate(reinterpret_cast<uint64_t>(tablePage));
    if (!clbPhys || !fisPhys || !tablePhys) { pmm_free_page(clbPage);pmm_free_page(fisPage);pmm_free_page(tablePage);return false; }

    port->clb = static_cast<uint32_t>(clbPhys);
    port->clbu = static_cast<uint32_t>(clbPhys >> 32);
    port->fb = static_cast<uint32_t>(fisPhys);
    port->fbu = static_cast<uint32_t>(fisPhys >> 32);

    auto* headers = static_cast<HbaCmdHeader*>(clbPage);
    headers[0].prdtl = 1;
    headers[0].ctba = static_cast<uint32_t>(tablePhys);
    headers[0].ctbau = static_cast<uint32_t>(tablePhys >> 32);

    port->serr = 0xFFFFFFFFu;
    port->is = 0xFFFFFFFFu;
    port->ie = 0;

    gCtx.port = port;
    gCtx.commandList = headers;
    gCtx.commandTable = static_cast<HbaCmdTable*>(tablePage);
    gCtx.commandListPage = clbPage;
    gCtx.commandTablePage = tablePage;
    gCtx.fisArea = fisPage;
    gCtx.portIndex = index;
    if(!start_port(port)){release_port_pages();return false;}
    return true;
}

static bool issue_command_once(uint8_t command, uint64_t lba, uint16_t count, void* buffer, uint32_t bytes, bool write) {
    gLastCommandTimedOut=false;
    if (!gCtx.port || !gCtx.commandList || !gCtx.commandTable) return false;
    if ((bytes && !buffer) || (!bytes && buffer)) return false;
    if (!wait_clear(gCtx.port->tfd, 0x88u)) { gLastCommandTimedOut=true; return false; } // BSY | DRQ
    if ((gCtx.port->ci | gCtx.port->sact) & 1u) { gLastCommandTimedOut=true; return false; }

    uint64_t bufferPhys = 0;
    if (bytes) {
        bufferPhys = vmm_translate(reinterpret_cast<uint64_t>(buffer));
        if (!bufferPhys) return false;
    }

    HbaCmdHeader& header = gCtx.commandList[0];
    header.flags = static_cast<uint16_t>(sizeof(FisRegH2D) / 4);
    if (write) header.flags |= static_cast<uint16_t>(1u << 6); // W = host-to-device
    header.prdtl = bytes ? 1 : 0;
    header.prdbc = 0;

    zero_bytes(gCtx.commandTable, sizeof(HbaCmdTable));
    if (bytes) {
        gCtx.commandTable->prdt[0].dba = static_cast<uint32_t>(bufferPhys);
        gCtx.commandTable->prdt[0].dbau = static_cast<uint32_t>(bufferPhys >> 32);
        gCtx.commandTable->prdt[0].dbc = (bytes - 1u) | (1u << 31);
    }

    auto* fis = reinterpret_cast<FisRegH2D*>(gCtx.commandTable->cfis);
    fis->fisType = FIS_TYPE_REG_H2D;
    fis->pmportC = 1u << 7;
    fis->command = command;
    fis->device = 1u << 6;
    fis->lba0 = static_cast<uint8_t>(lba);
    fis->lba1 = static_cast<uint8_t>(lba >> 8);
    fis->lba2 = static_cast<uint8_t>(lba >> 16);
    fis->lba3 = static_cast<uint8_t>(lba >> 24);
    fis->lba4 = static_cast<uint8_t>(lba >> 32);
    fis->lba5 = static_cast<uint8_t>(lba >> 40);
    fis->countLow = static_cast<uint8_t>(count);
    fis->countHigh = static_cast<uint8_t>(count >> 8);

    gCtx.port->is = 0xFFFFFFFFu;
    gCtx.port->ci = 1u;
    for (uint32_t spin=0; spin<TIMEOUT; ++spin) {
        if ((gCtx.port->ci & 1u)==0) {
            return (gCtx.port->is & HBA_PxIS_TFES)==0;
        }
        if (gCtx.port->is & HBA_PxIS_TFES) return false;
    }
    gLastCommandTimedOut=true;
    return false;
}

static bool issue_command(uint8_t command,uint64_t lba,uint16_t count,void*buffer,uint32_t bytes,bool write){
    for(uint32_t attempt=0;attempt<3;++attempt){
        if(issue_command_once(command,lba,count,buffer,bytes,write))return true;
        ++gStats.ioErrors;if(gLastCommandTimedOut)++gStats.ioTimeouts;
        if(attempt+1<3){++gStats.ioRetries;if(gCtx.port){gCtx.port->serr=0xFFFFFFFFu;gCtx.port->is=0xFFFFFFFFu;}}
    }
    return false;
}

static bool identify_disk() {
    alignas(4096) static uint16_t identify[256];
    zero_bytes(identify,sizeof(identify));
    if (!issue_command(ATA_CMD_IDENTIFY,0,0,identify,512,false)) return false;
    uint64_t sectors = static_cast<uint64_t>(identify[100]) |
                       (static_cast<uint64_t>(identify[101]) << 16) |
                       (static_cast<uint64_t>(identify[102]) << 32) |
                       (static_cast<uint64_t>(identify[103]) << 48);
    if (!sectors) sectors = static_cast<uint64_t>(identify[60]) | (static_cast<uint64_t>(identify[61]) << 16);
    gCtx.sectorCount = sectors;
    return sectors != 0;
}

static bool ahci_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* out) {
    if (!dev || !out || !count || dev->sectorSize != 512) return false;
    ++gStats.readOps;
    auto* dst = static_cast<uint8_t*>(out);
    // Keep DMA transfers inside one physically contiguous 4 KiB page.  This is
    // substantially faster than issuing one command per sector during install.
    alignas(4096) static uint8_t dmaBuffer[4096];
    uint32_t done = 0;
    while (done < count) {
        const uint16_t chunk = static_cast<uint16_t>((count - done) > 8 ? 8 : (count - done));
        const uint32_t bytes = static_cast<uint32_t>(chunk) * 512u;
        if (!issue_command(ATA_CMD_READ_DMA_EXT, lba + done, chunk, dmaBuffer, bytes, false)) return false;
        for (uint32_t j=0;j<bytes;++j) dst[static_cast<uint64_t>(done)*512u + j] = dmaBuffer[j];
        done += chunk;
    }
    return true;
}

static bool ahci_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* data) {
    if (!dev || !data || !count || dev->sectorSize != 512) return false;
    ++gStats.writeOps;
    const auto* src = static_cast<const uint8_t*>(data);
    alignas(4096) static uint8_t dmaBuffer[4096];
    uint32_t done = 0;
    while (done < count) {
        const uint16_t chunk = static_cast<uint16_t>((count - done) > 8 ? 8 : (count - done));
        const uint32_t bytes = static_cast<uint32_t>(chunk) * 512u;
        for (uint32_t j=0;j<bytes;++j) dmaBuffer[j] = src[static_cast<uint64_t>(done)*512u + j];
        if (!issue_command(ATA_CMD_WRITE_DMA_EXT, lba + done, chunk, dmaBuffer, bytes, true)) return false;
        done += chunk;
    }
    return true;
}

static bool ahci_flush(BlockDevice* dev) {
    if (!dev) return false;
    ++gStats.flushOps;
    return issue_command(ATA_CMD_FLUSH_CACHE_EXT, 0, 0, nullptr, 0, false);
}
}

bool ahci_init_first(BlockDevice& out) {
    out = {};
    gCtx = {};
    gStats = {};

    PciDeviceInfo pci{};
    if (!pci_find_ahci(pci)) return false;
    gStats.controllerFound = true;
    gStats.bus = pci.bus; gStats.device = pci.device; gStats.function = pci.function;
    gStats.vendorId = pci.vendorId; gStats.deviceId = pci.deviceId;

    pci_enable_mmio_busmaster(pci);
    const uint32_t bar5 = pci.bar[5];
    if ((bar5 & 1u) != 0) return false;
    const uint64_t abar = static_cast<uint64_t>(bar5 & ~0x0Fu);
    if (!abar) return false;
    gStats.abar = abar;
    gCtx.hba = reinterpret_cast<volatile HbaMem*>(static_cast<uintptr_t>(abar));
    gCtx.hba->ghc |= HBA_GHC_AE;

    const uint32_t implemented = gCtx.hba->pi;
    for (uint8_t i=0;i<32;++i) {
        if (!(implemented & (1u<<i))) continue;
        if (!port_present(&gCtx.hba->ports[i])) continue;
        gStats.portFound = true;
        gStats.port = i;
        if (!setup_port(i)) continue;
        if (!identify_disk()) { release_port_pages(); continue; }
        gStats.identified = true;
        gStats.sectors = gCtx.sectorCount;
        out.name = "ZERONT AHCI SATA";
        out.sectorSize = 512;
        out.sectorCount = gCtx.sectorCount;
        out.context = &gCtx;
        out.read = ahci_read;
        out.write = ahci_write;
        out.flush = ahci_flush;
        return true;
    }
    return false;
}

AhciStats ahci_stats() { return gStats; }
