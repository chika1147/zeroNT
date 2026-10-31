#pragma once
#include <stdint.h>

struct AcpiStats {
    bool rsdp;
    bool xsdt;
    bool fadt;
    bool madt;
    bool hpet;
    bool mcfg;
    uint64_t rsdpAddress;
    uint64_t rootAddress;
    uint32_t tableCount;
    uint8_t resetSpaceId;
    uint64_t resetAddress;
    uint8_t resetValue;
    uint64_t localApicAddress;
    uint64_t ioApicAddress;
    uint32_t ioApicGsiBase;
    uint32_t enabledCpuCount;
    uint32_t interruptOverrides;
    uint64_t hpetAddress;
    uint64_t mcfgAddress;
};

bool acpi_init(uint64_t rsdpAddress);
AcpiStats acpi_stats();
uint64_t acpi_local_apic_address();
uint64_t acpi_io_apic_address();
uint64_t acpi_hpet_address();
[[noreturn]] void acpi_reboot();
[[noreturn]] void acpi_poweroff();
