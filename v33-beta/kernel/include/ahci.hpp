#pragma once
#include <stdint.h>
#include "block_device.hpp"
#include "pci.hpp"

struct AhciStats {
    bool controllerFound;
    bool portFound;
    bool identified;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t port;
    uint16_t vendorId;
    uint16_t deviceId;
    uint64_t abar;
    uint64_t sectors;
    uint64_t readOps;
    uint64_t writeOps;
    uint64_t flushOps;
    uint64_t ioRetries;
    uint64_t ioErrors;
    uint64_t ioTimeouts;
};

bool ahci_init_first(BlockDevice& out);
AhciStats ahci_stats();
