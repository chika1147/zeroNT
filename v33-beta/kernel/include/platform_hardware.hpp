#pragma once
#include <stdint.h>

struct PlatformHardwareStats {
    bool cpuApicSupported;
    bool localApicFound;
    bool localApicMapped;
    bool hpetFound;
    bool hpetRunning;
    uint64_t localApicAddress;
    uint32_t localApicId;
    uint32_t localApicVersion;
    uint64_t hpetAddress;
    uint64_t hpetFrequencyHz;
    uint64_t hpetCounter;
    uint32_t pciMsiDevices;
    uint32_t pciMsixDevices;
    uint32_t pciMalformedCapabilityLists;
};

bool platform_hardware_init();
void platform_hardware_poll();
PlatformHardwareStats platform_hardware_stats();
uint64_t platform_monotonic_counter();
