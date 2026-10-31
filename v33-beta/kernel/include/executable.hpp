#pragma once
#include <stdint.h>
#include "fat32.hpp"

#pragma pack(push,1)
struct ZeroNTExeHeader {
    char magic[4];      // ZEXE
    uint16_t version;   // 1
    uint16_t flags;
    uint32_t entryOffset;
    uint32_t codeSize;
    uint32_t reserved;
};
#pragma pack(pop)

struct ExecutableStats {
    bool loaded;
    bool ran;
    bool running;
    uint32_t bytes;
    int64_t exitCode;
    uint64_t pid;
    char path[64];
};

// Starts a native zeroNT Ring-3 executable.  A cooperative app that calls
// SysYield remains alive and is resumed by executable_poll().
bool executable_run(Fat32Volume& volume, const char* path, ExecutableStats& out);
void executable_poll();
void executable_shutdown_all();
uint32_t executable_running_count();
