#pragma once
#include <stdint.h>
#include "fat32.hpp"

struct BetaStats {
    uint64_t uptimeTicks;
    uint64_t freeMemoryLowWater;
    uint64_t heapFreeLowWater;
    uint64_t samples;
    uint64_t healthWarnings;
    uint64_t fsRecoveryActions;
    uint64_t driverWarnings;
    uint64_t networkWarnings;
    bool filesystemHealthy;
    bool storageHealthy;
    bool networkHealthy;
    bool usbHealthy;
    bool securityHealthy;
    bool readyForSoak;
};

void beta_init(Fat32Volume* volume);
void beta_poll(uint64_t ticks);
BetaStats beta_stats();
