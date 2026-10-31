#pragma once
#include <stdint.h>
#include <stddef.h>

enum class ThreadState : uint8_t {
    Unused = 0,
    Ready,
    Running,
    Sleeping,
    Terminated,
};

using ThreadEntry = void(*)(void*);

struct SchedulerStats {
    uint64_t totalTicks;
    uint64_t contextSwitches;
    uint64_t preemptions;
    uint64_t yields;
    uint64_t currentTid;
    uint64_t currentPid;
    uint64_t reapedStacks;
    uint64_t stackAllocationFailures;
    uint32_t threadCount;
    uint32_t readyCount;
    uint32_t guardedStacks;
};

void scheduler_init(uint64_t kernelPid);
uint64_t scheduler_create_thread(uint64_t pid, const char* name, ThreadEntry entry, void* arg);
void scheduler_start();
void scheduler_yield();
[[noreturn]] void scheduler_exit_current(int exitCode = 0);
uint32_t scheduler_reap_terminated();

uint64_t* scheduler_on_timer(uint64_t* interruptFrame);
uint64_t* scheduler_on_yield(uint64_t* interruptFrame);

uint64_t scheduler_current_tid();
uint64_t scheduler_current_pid();
SchedulerStats scheduler_stats();
const char* scheduler_state_name(ThreadState state);
