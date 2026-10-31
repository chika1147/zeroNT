#include "include/block_device.hpp"

namespace {
struct RamDiskContext {
    uint8_t* base;
    uint64_t bytes;
};

static RamDiskContext gRamDisk{};

static bool copy_bytes(void* dst, const void* src, uint64_t bytes) {
    if (!dst || !src) return false;
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    for (uint64_t i = 0; i < bytes; ++i) d[i] = s[i];
    return true;
}

static bool ramdisk_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* out) {
    if (!dev || !dev->context || !out || !count || !dev->sectorSize) return false;
    auto* ctx = static_cast<RamDiskContext*>(dev->context);
    const uint64_t offset = lba * static_cast<uint64_t>(dev->sectorSize);
    const uint64_t bytes = static_cast<uint64_t>(count) * dev->sectorSize;
    if (offset > ctx->bytes || bytes > ctx->bytes - offset) return false;
    return copy_bytes(out, ctx->base + offset, bytes);
}

static bool ramdisk_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* data) {
    if (!dev || !dev->context || !data || !count || !dev->sectorSize) return false;
    auto* ctx = static_cast<RamDiskContext*>(dev->context);
    const uint64_t offset = lba * static_cast<uint64_t>(dev->sectorSize);
    const uint64_t bytes = static_cast<uint64_t>(count) * dev->sectorSize;
    if (offset > ctx->bytes || bytes > ctx->bytes - offset) return false;
    return copy_bytes(ctx->base + offset, data, bytes);
}

static bool ramdisk_flush(BlockDevice*) { return true; }
}

bool block_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* out) {
    if (!dev || !dev->read || !count) return false;
    if (lba >= dev->sectorCount || count > dev->sectorCount - lba) return false;
    return dev->read(dev, lba, count, out);
}

bool block_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* data) {
    if (!dev || !dev->write || !count) return false;
    if (lba >= dev->sectorCount || count > dev->sectorCount - lba) return false;
    return dev->write(dev, lba, count, data);
}

bool block_flush(BlockDevice* dev) {
    if (!dev) return false;
    return dev->flush ? dev->flush(dev) : true;
}

bool ramdisk_init(BlockDevice& out, void* base, uint64_t bytes, uint32_t sectorSize) {
    if (!base || !bytes || !sectorSize || (bytes % sectorSize) != 0) return false;
    gRamDisk.base = static_cast<uint8_t*>(base);
    gRamDisk.bytes = bytes;
    out.name = "ZERONT RAMDISK";
    out.sectorSize = sectorSize;
    out.sectorCount = bytes / sectorSize;
    out.context = &gRamDisk;
    out.read = ramdisk_read;
    out.write = ramdisk_write;
    out.flush = ramdisk_flush;
    return true;
}
