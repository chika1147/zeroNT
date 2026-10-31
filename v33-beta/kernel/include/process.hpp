#pragma once
#include <stdint.h>

enum class ProcessState : uint8_t {
    Unused = 0,
    Running,
    Exited,
};

struct ProcessStats {
    uint32_t processCount;
    uint32_t runningCount;
    uint32_t exitedCount;
    uint64_t nextPid;
    uint64_t reapedCount;
};

struct ProcessInfo {
    uint64_t pid;
    const char* name;
    ProcessState state;
    int exitCode;
    uint32_t threadCount;
};

using ProcessEnumerateCallback = void (*)(const ProcessInfo& info, void* context);

void process_init();
uint64_t process_create(const char* name);
bool process_attach_thread(uint64_t pid, uint64_t tid);
void process_mark_exited(uint64_t pid, int exitCode);
uint32_t process_reap_exited(uint32_t maxToReap=0xFFFFFFFFu);
void process_enumerate(ProcessEnumerateCallback callback, void* context);
ProcessStats process_stats();
const char* process_name(uint64_t pid);
