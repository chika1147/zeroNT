#pragma once
#include <stdint.h>
#include "paging.hpp"
#include "syscall.hpp"

struct UserModeStats {
    bool tssReady;
    bool ring3Entered;
    bool syscallReturned;
    uint64_t codeVirtual;
    uint64_t stackVirtual;
    uint64_t syscalls;
    uint64_t pings;
    int64_t exitCode;
    uint32_t cooperativeYields;
    uint32_t liveContexts;
    uint64_t stackGuardLow;
    uint64_t stackGuardHigh;
    uint64_t oomFailures;
    bool stackGuardsReady;
};

enum class UserRunResult : uint8_t {
    Failed = 0,
    Exited,
    Yielded,
};

constexpr uint32_t USERMODE_MAX_CODE_PAGES = 32;
constexpr uint32_t USERMODE_STACK_PAGES = 8;

struct UserProgramContext {
    bool prepared;
    bool suspended;
    uint64_t pid;
    VmmAddressSpace space;
    void* codePages[USERMODE_MAX_CODE_PAGES];
    uint32_t codePageCount;
    void* stackPages[USERMODE_STACK_PAGES];
    uint32_t stackPageCount;
    uint32_t codeSize;
    uint32_t entryOffset;
    SyscallFrame frame;
};

bool usermode_init();
bool usermode_smoke_test();
bool usermode_fault_smoke_test();

bool usermode_prepare_context(UserProgramContext& context,
                              const void* code,
                              uint32_t codeSize,
                              uint32_t entryOffset,
                              uint64_t pid);
UserRunResult usermode_start_context(UserProgramContext& context, int64_t& exitCode);
UserRunResult usermode_resume_context(UserProgramContext& context, int64_t& exitCode);
void usermode_destroy_context(UserProgramContext& context);

// Called only from the SysYield syscall while a cooperative Ring-3 executable
// is active.  Copies the complete interrupt frame so the process can resume at
// the instruction immediately following INT 80h.
bool usermode_capture_yield_frame(const SyscallFrame* frame);

bool usermode_run_code(const void* code, uint32_t codeSize, uint32_t entryOffset, int64_t& exitCode);
bool usermode_run_code_for_process(const void* code, uint32_t codeSize, uint32_t entryOffset, uint64_t pid, int64_t& exitCode);
uint64_t usermode_current_pid();
UserModeStats usermode_stats();
