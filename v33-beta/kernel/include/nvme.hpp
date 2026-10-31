#pragma once
#include <stdint.h>
#include "pci.hpp"
#include "block_device.hpp"

struct NvmeProbeStats {
    bool found;
    uint8_t bus,device,function;
    uint64_t bar0;
    uint32_t version;
};

struct NvmeStats {
    bool found;
    bool mmioMapped;
    bool controllerReady;
    bool namespaceReady;
    bool ioQueueReady;
    uint8_t bus,device,function;
    uint16_t vendorId,deviceId;
    uint64_t bar0;
    uint32_t version;
    uint32_t queueDepth;
    uint32_t sectorSize;
    uint64_t sectorCount;
    uint64_t readCommands;
    uint64_t writeCommands;
    uint64_t flushCommands;
    uint64_t timeouts;
    uint64_t commandErrors;
    uint64_t retries;
};

void nvme_probe();
NvmeProbeStats nvme_probe_stats();

// v33 Phase 2 polling NVMe driver. The first namespace is exposed as a normal
// zeroNT BlockDevice. It intentionally uses a one-page bounce buffer so DMA is
// always physically contiguous and safe before a full IOMMU/scatter-gather layer.
bool nvme_init_first(BlockDevice& out);
NvmeStats nvme_stats();
