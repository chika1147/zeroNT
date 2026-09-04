#ifndef NTZERO_BOOT_H
#define NTZERO_BOOT_H

#include "types.h"

#define NTZERO_BOOT_MAGIC 0x4F52455A544E544EULL /* "NTNTZERO" marker */

typedef struct NTZ_BOOT_INFO {
    u64 magic;
    u64 version;
    void *uefi_memory_map;
    u64 uefi_memory_map_size;
    u64 uefi_descriptor_size;
    u64 uefi_descriptor_version;
} NTZ_BOOT_INFO;

#endif
