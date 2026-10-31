#pragma once
#include <stdint.h>
#include <stddef.h>
#include "block_device.hpp"

struct Fat32Stats {
    bool mounted;
    uint32_t bytesPerSector;
    uint32_t sectorsPerCluster;
    uint32_t reservedSectors;
    uint32_t fatCount;
    uint32_t fatSectors;
    uint32_t rootCluster;
    uint64_t totalSectors;
    uint64_t dataClusters;
};

struct Fat32Volume {
    BlockDevice* device;
    Fat32Stats stats;
    uint64_t fatStartLba;
    uint64_t firstDataLba;
    // Next cluster to probe when allocating. This prevents O(n^2) FAT scans
    // while growing large files one cluster at a time.
    uint32_t allocHint;
};

struct Fat32DirEntry {
    // UTF-8 display name. FAT32 LFN permits up to 255 UTF-16 code units;
    // this buffer is intentionally larger than the old 8.3-only 13 bytes.
    char name[256];
    uint8_t attributes;
    uint32_t firstCluster;
    uint32_t size;
    uint16_t createDate;
    uint16_t createTime;
    uint16_t writeDate;
    uint16_t writeTime;
    uint16_t accessDate;
};

struct Fat32CheckStats {
    bool healthy;
    uint64_t scannedClusters;
    uint64_t freeClusters;
    uint64_t allocatedClusters;
    uint64_t badClusters;
    uint64_t invalidLinks;
    uint64_t loops;
};

using Fat32ListCallback = void (*)(const Fat32DirEntry& entry, void* context);

bool fat32_mount(BlockDevice* device, Fat32Volume& out);
bool fat32_list_directory(Fat32Volume& volume, const char* path, Fat32ListCallback callback, void* context);
bool fat32_read_file(Fat32Volume& volume, const char* path, void* out, size_t capacity, size_t& bytesRead);

// FAT32 read/write API. File and directory creation supports VFAT LFN names; 8.3 aliases are generated automatically.
bool fat32_create_file(Fat32Volume& volume, const char* path);
bool fat32_write_file(Fat32Volume& volume, const char* path, const void* data, size_t size);
bool fat32_append_file(Fat32Volume& volume, const char* path, const void* data, size_t size);
bool fat32_delete_file(Fat32Volume& volume, const char* path);
bool fat32_rename(Fat32Volume& volume, const char* oldPath, const char* newPath);
bool fat32_mkdir(Fat32Volume& volume, const char* path);
bool fat32_rmdir(Fat32Volume& volume, const char* path);

// v33 filesystem core: streaming file operations and capacity reporting.
bool fat32_copy_file(Fat32Volume& volume, const char* sourcePath, const char* destinationPath);
bool fat32_move_file(Fat32Volume& volume, const char* sourcePath, const char* destinationPath);
uint64_t fat32_free_bytes(Fat32Volume& volume);

// v33 public-release filesystem helpers.
bool fat32_stat(Fat32Volume& volume, const char* path, Fat32DirEntry& out);
bool fat32_copy_tree(Fat32Volume& volume, const char* sourcePath, const char* destinationPath);
bool fat32_move_tree(Fat32Volume& volume, const char* sourcePath, const char* destinationPath);
bool fat32_delete_tree(Fat32Volume& volume, const char* path);

// Stream a whole block device into one FAT file without buffering the complete
// image in kernel memory. Used by the standalone HDD installer.
bool fat32_write_file_from_block(Fat32Volume& volume, const char* path, BlockDevice* source, uint64_t bytes);

// Full FAT allocation-table sanity scan. This does not modify media.
bool fat32_check_volume(Fat32Volume& volume, Fat32CheckStats& out);
