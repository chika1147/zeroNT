#pragma once
#include <stdint.h>
#include <stddef.h>

struct ZeroNTBootInfo {
    uint64_t magic;
    uint32_t version;
    uint32_t reserved0;

    uint64_t framebuffer;
    uint64_t framebufferSize;
    uint32_t width;
    uint32_t height;
    uint32_t pixelsPerScanLine;
    uint32_t pixelFormat;

    uint64_t memoryMap;
    uint64_t memoryMapSize;
    uint64_t memoryDescriptorSize;
    uint32_t memoryDescriptorVersion;
    uint32_t reserved1;

    uint64_t acpiRsdp;
    uint64_t kernelBase;
    uint64_t kernelSize;
    uint64_t kernelMemorySize;

    // v19 fallback: root filesystem image loaded by the boot loader before ExitBootServices.
    // The kernel treats this memory as a raw block device and parses FAT32 itself.
    uint64_t rootfsBase;
    uint64_t rootfsSize;
    uint32_t rootfsSectorSize;
    uint32_t reserved2;

    // v33 public-release loader diagnostics / boot policy.
    // bit0 = safe mode, bit1 = recovery mode, bit2 = kernel CRC verified,
    // bit3 = rootfs CRC verified.
    uint32_t bootFlags;
    uint32_t kernelCrc32;
    uint32_t rootfsCrc32;
    uint32_t reserved3;
};

static constexpr uint64_t ZERONT_BOOTINFO_MAGIC = 0x544E4F52455A4249ULL;
static constexpr uint32_t ZERONT_BOOTINFO_VERSION = 5;
static constexpr uint32_t ZERONT_BOOT_SAFE_MODE = 1u << 0;
static constexpr uint32_t ZERONT_BOOT_RECOVERY_MODE = 1u << 1;
static constexpr uint32_t ZERONT_BOOT_KERNEL_CRC_OK = 1u << 2;
static constexpr uint32_t ZERONT_BOOT_ROOTFS_CRC_OK = 1u << 3;
static constexpr uint64_t ZERONT_KERNEL_BASE = 0x04000000ULL;
static constexpr uint64_t ZERONT_KERNEL_MEMORY_SIZE = 0x00200000ULL;
