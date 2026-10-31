#pragma once
#include <stdint.h>
#include <stddef.h>

struct BlockDevice;
using BlockReadFn = bool (*)(BlockDevice* dev, uint64_t lba, uint32_t count, void* out);
using BlockWriteFn = bool (*)(BlockDevice* dev, uint64_t lba, uint32_t count, const void* data);
using BlockFlushFn = bool (*)(BlockDevice* dev);

struct BlockDevice {
    const char* name;
    uint32_t sectorSize;
    uint64_t sectorCount;
    void* context;
    BlockReadFn read;
    BlockWriteFn write;
    BlockFlushFn flush;
};

bool block_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* out);
bool block_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* data);
bool block_flush(BlockDevice* dev);
bool ramdisk_init(BlockDevice& out, void* base, uint64_t bytes, uint32_t sectorSize);
