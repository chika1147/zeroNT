#include "include/scheduler.hpp"
#include "include/arch.hpp"
#include "include/process.hpp"
#include "include/memory.hpp"
#include "include/paging.hpp"
#include <stdint.h>
#include <stddef.h>

namespace {
constexpr uint32_t MAX_THREADS = 32;
constexpr uint32_t THREAD_STACK_PAGES = 8;
constexpr uint64_t PAGE_SIZE = 4096;
constexpr uint64_t THREAD_STACK_SIZE = uint64_t(THREAD_STACK_PAGES) * PAGE_SIZE;
constexpr uint64_t STACK_SLOT_PAGES = THREAD_STACK_PAGES + 2; // low + high guard
constexpr uint64_t STACK_SLOT_BYTES = STACK_SLOT_PAGES * PAGE_SIZE;
constexpr uint64_t STACK_ARENA_BASE = 0xFFFF900000000000ULL;
constexpr uint64_t DEFAULT_QUANTUM_TICKS = 5;

struct InterruptContext {
    uint64_t r15; uint64_t r14; uint64_t r13; uint64_t r12;
    uint64_t r11; uint64_t r10; uint64_t r9;  uint64_t r8;
    uint64_t rdi; uint64_t rsi; uint64_t rbp; uint64_t rdx;
    uint64_t rcx; uint64_t rbx; uint64_t rax;
    uint64_t rip; uint64_t cs; uint64_t rflags;
};

struct Thread {
    uint64_t tid;
    uint64_t pid;
    char name[32];
    ThreadState state;
    uint64_t* savedRsp;
    uint64_t stackVirtual;
    void* stackPages[THREAD_STACK_PAGES];
    uint64_t stackBytes;
    ThreadEntry entry;
    void* arg;
    uint64_t cpuTicks;
    uint64_t switches;
    int exitCode;
};

static Thread gThreads[MAX_THREADS]{};
static int gCurrent = -1;
static uint64_t gNextTid = 1;
static bool gInitialized = false;
static bool gStarted = false;
static uint64_t gSchedulerTicks = 0;
static uint64_t gContextSwitches = 0;
static uint64_t gPreemptions = 0;
static uint64_t gYields = 0;
static uint64_t gReapedStacks = 0;
static uint64_t gStackAllocationFailures = 0;

static void copy_name(char* out, size_t cap, const char* in) {
    if (!out || cap == 0) return;
    size_t i = 0;
    if (in) while (i + 1 < cap && in[i]) { out[i] = in[i]; ++i; }
    out[i] = 0;
}

static int active_thread_count() {
    int n = 0;
    for (uint32_t i = 0; i < MAX_THREADS; ++i)
        if (gThreads[i].state != ThreadState::Unused && gThreads[i].state != ThreadState::Terminated) ++n;
    return n;
}

static int pick_next_ready() {
    if (gCurrent < 0) return -1;
    for (uint32_t step = 1; step <= MAX_THREADS; ++step) {
        const uint32_t idx = (static_cast<uint32_t>(gCurrent) + step) % MAX_THREADS;
        if (gThreads[idx].state == ThreadState::Ready) return static_cast<int>(idx);
    }
    return -1;
}

static uint64_t stack_slot_base(uint32_t slot) {
    return STACK_ARENA_BASE + uint64_t(slot) * STACK_SLOT_BYTES;
}

static void release_stack(uint32_t slot) {
    if (slot == 0 || slot >= MAX_THREADS) return;
    Thread& t = gThreads[slot];
    if (!t.stackVirtual) return;
    const uint64_t mappedBase = stack_slot_base(slot) + PAGE_SIZE;
    for (uint32_t i=0;i<THREAD_STACK_PAGES;++i) {
        (void)vmm_unmap_page(mappedBase + uint64_t(i)*PAGE_SIZE);
        if (t.stackPages[i]) pmm_free_page(t.stackPages[i]);
        t.stackPages[i]=nullptr;
    }
    t.stackVirtual=0;
    t.stackBytes=0;
    ++gReapedStacks;
}

static bool allocate_guarded_stack(uint32_t slot, Thread& t, uint64_t& stackTop) {
    const uint64_t slotBase = stack_slot_base(slot);
    const uint64_t mappedBase = slotBase + PAGE_SIZE;
    // slotBase and slotBase + (THREAD_STACK_PAGES + 1)*PAGE_SIZE stay unmapped.
    for (uint32_t i=0;i<THREAD_STACK_PAGES;++i) {
        void* phys=pmm_alloc_page();
        if(!phys){++gStackAllocationFailures;release_stack(slot);return false;}
        if(!vmm_map_page(mappedBase + uint64_t(i)*PAGE_SIZE,reinterpret_cast<uint64_t>(phys),
                         PagePresent|PageWritable|PageNoExecute)){
            pmm_free_page(phys);++gStackAllocationFailures;release_stack(slot);return false;
        }
        t.stackPages[i]=phys;
        t.stackVirtual=mappedBase;
        t.stackBytes=THREAD_STACK_SIZE;
    }
    uint64_t phys=0,flags=0;
    if(vmm_query_page(slotBase,phys,flags) ||
       vmm_query_page(slotBase + uint64_t(THREAD_STACK_PAGES+1)*PAGE_SIZE,phys,flags)){
        ++gStackAllocationFailures;release_stack(slot);return false;
    }
    stackTop=mappedBase+THREAD_STACK_SIZE;
    return true;
}

static uint32_t reap_terminated_except(int except) {
    uint32_t n=0;
    for(uint32_t i=1;i<MAX_THREADS;++i){
        if(static_cast<int>(i)==except || gThreads[i].state!=ThreadState::Terminated)continue;
        release_stack(i);
        gThreads[i]={};
        ++n;
    }
    return n;
}

[[noreturn]] static void thread_trampoline() {
    Thread* t = (gCurrent >= 0) ? &gThreads[gCurrent] : nullptr;
    if (!t || !t->entry) scheduler_exit_current(-1);
    t->entry(t->arg);
    scheduler_exit_current(0);
}

static uint64_t* schedule(uint64_t* currentFrame, bool forced, bool preempt) {
    if (!gInitialized || !gStarted || gCurrent < 0) return currentFrame;
    // Safe here: anything reaped is from a previous context, never the stack
    // currently executing this scheduler frame.
    (void)reap_terminated_except(gCurrent);

    Thread& current = gThreads[gCurrent];
    current.savedRsp = currentFrame;
    if (preempt) ++current.cpuTicks;
    if (!forced && preempt && (gSchedulerTicks % DEFAULT_QUANTUM_TICKS) != 0) return currentFrame;
    if (current.state == ThreadState::Running) current.state = ThreadState::Ready;

    const int next = pick_next_ready();
    if (next < 0) {
        if (current.state == ThreadState::Ready) current.state = ThreadState::Running;
        return currentFrame;
    }
    if (next == gCurrent) {
        gThreads[next].state = ThreadState::Running;
        return currentFrame;
    }

    gCurrent = next;
    Thread& target = gThreads[gCurrent];
    target.state = ThreadState::Running;
    ++target.switches;
    ++gContextSwitches;
    if (preempt) ++gPreemptions;
    return target.savedRsp ? target.savedRsp : currentFrame;
}
}

void scheduler_init(uint64_t kernelPid) {
    for (uint32_t i = 0; i < MAX_THREADS; ++i) gThreads[i] = {};
    gCurrent = 0; gNextTid = 1; gSchedulerTicks = 0; gContextSwitches = 0;
    gPreemptions = 0; gYields = 0; gReapedStacks=0; gStackAllocationFailures=0;
    Thread& boot = gThreads[0];
    boot.tid = gNextTid++;
    boot.pid = kernelPid;
    copy_name(boot.name, sizeof(boot.name), "kernel-main");
    boot.state = ThreadState::Running;
    boot.savedRsp = nullptr;
    gInitialized = true;
    gStarted = false;
}

uint64_t scheduler_create_thread(uint64_t pid, const char* name, ThreadEntry entry, void* arg) {
    if (!gInitialized || !entry) return 0;
    (void)reap_terminated_except(gCurrent);
    int slot = -1;
    for (uint32_t i = 1; i < MAX_THREADS; ++i) if (gThreads[i].state == ThreadState::Unused) { slot = static_cast<int>(i); break; }
    if (slot < 0) return 0;

    Thread& t = gThreads[slot];
    t = {};
    uint64_t stackTop=0;
    if(!allocate_guarded_stack(static_cast<uint32_t>(slot),t,stackTop)){t={};return 0;}

    t.tid = gNextTid++;
    t.pid = pid;
    copy_name(t.name, sizeof(t.name), name ? name : "thread");
    t.state = ThreadState::Ready;
    t.entry = entry;
    t.arg = arg;

    stackTop &= ~static_cast<uintptr_t>(0xFULL);
    stackTop -= 8;
    auto* ctx = reinterpret_cast<InterruptContext*>(stackTop - sizeof(InterruptContext));
    auto* bytes = reinterpret_cast<uint8_t*>(ctx);
    for (size_t i = 0; i < sizeof(InterruptContext); ++i) bytes[i] = 0;
    ctx->rip = reinterpret_cast<uint64_t>(&thread_trampoline);
    ctx->cs = 0x08;
    ctx->rflags = 0x202;
    t.savedRsp = reinterpret_cast<uint64_t*>(ctx);
    return t.tid;
}

void scheduler_start() { gStarted = true; }
void scheduler_yield() { if (gStarted) zeront_scheduler_yield_interrupt(); }

[[noreturn]] void scheduler_exit_current(int exitCode) {
    interrupts_disable();
    if (gCurrent >= 0) { Thread& t = gThreads[gCurrent]; t.exitCode = exitCode; t.state = ThreadState::Terminated; }
    ++gYields;
    zeront_scheduler_yield_interrupt();
    for (;;) cpu_halt();
}

uint32_t scheduler_reap_terminated(){return reap_terminated_except(gCurrent);}
uint64_t* scheduler_on_timer(uint64_t* interruptFrame) { ++gSchedulerTicks; return schedule(interruptFrame, false, true); }
uint64_t* scheduler_on_yield(uint64_t* interruptFrame) { ++gYields; return schedule(interruptFrame, true, false); }
uint64_t scheduler_current_tid() { return (gCurrent >= 0) ? gThreads[gCurrent].tid : 0; }
uint64_t scheduler_current_pid() { return (gCurrent >= 0) ? gThreads[gCurrent].pid : 0; }

SchedulerStats scheduler_stats() {
    SchedulerStats s{};
    s.totalTicks = gSchedulerTicks; s.contextSwitches = gContextSwitches; s.preemptions = gPreemptions;
    s.yields = gYields; s.currentTid = scheduler_current_tid(); s.currentPid = scheduler_current_pid();
    s.reapedStacks=gReapedStacks; s.stackAllocationFailures=gStackAllocationFailures;
    s.threadCount = static_cast<uint32_t>(active_thread_count());
    for (uint32_t i = 0; i < MAX_THREADS; ++i) {
        if (gThreads[i].state == ThreadState::Ready) ++s.readyCount;
        if (i>0 && gThreads[i].stackVirtual) ++s.guardedStacks;
    }
    return s;
}

const char* scheduler_state_name(ThreadState state) {
    switch (state) {
        case ThreadState::Ready: return "READY";
        case ThreadState::Running: return "RUNNING";
        case ThreadState::Sleeping: return "SLEEPING";
        case ThreadState::Terminated: return "TERMINATED";
        default: return "UNUSED";
    }
}
