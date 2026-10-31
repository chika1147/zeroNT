#pragma once
#include <stdint.h>
#include <stddef.h>
#include "fat32.hpp"

struct SafeFsStats {
    uint64_t atomicWrites;
    uint64_t verifiedWrites;
    uint64_t rollbacks;
    uint64_t failures;
    uint64_t flushes;
    uint64_t journalWrites;
    uint64_t recoveredTransactions;
    uint64_t bootChecks;
    uint64_t treeCopies;
    uint64_t treeMoves;
    uint64_t treeDeletes;
    bool volumeHealthy;
};

void safe_fs_init(Fat32Volume* volume);
bool safe_fs_check_volume();
bool safe_fs_recover_pending();
bool safe_fs_atomic_write(const char* path,const void* data,size_t size);
bool safe_fs_copy_file(const char* src,const char* dst,bool removeSource=false);
bool safe_fs_copy_tree(const char* src,const char* dst,bool removeSource=false);
bool safe_fs_delete_tree(const char* path);
SafeFsStats safe_fs_stats();
