#include "include/usermode.hpp"
#include "include/syscall.hpp"
#include "include/paging.hpp"
#include "include/memory.hpp"
#include "include/arch.hpp"
#include <stddef.h>

namespace {
constexpr uint64_t PAGE_SIZE = 4096;
// Every process has its own CR3, so all executables may use the same virtual
// layout without sharing writable memory.
constexpr uint64_t USER_CODE = 0x0000008000000000ULL;
constexpr uint64_t USER_STACK = 0x0000008000200000ULL;
constexpr uint64_t USER_STACK_GUARD_LOW = USER_STACK - PAGE_SIZE;
constexpr uint64_t USER_STACK_GUARD_HIGH = USER_STACK + uint64_t(USERMODE_STACK_PAGES) * PAGE_SIZE;
constexpr uint64_t USER_STACK_TOP = USER_STACK_GUARD_HIGH - 16;

static UserModeStats gStats{};
static uint8_t* gSmokeCodePhysical = nullptr;
static uint64_t gCurrentPid = 0;
static UserProgramContext* gActiveContext = nullptr;

static void zero_bytes(void* p, uint64_t n) {
    auto* b=static_cast<uint8_t*>(p);
    for(uint64_t i=0;i<n;++i)b[i]=0;
}
static void copy_frame(SyscallFrame& dst,const SyscallFrame& src) {
    const auto* s=reinterpret_cast<const uint64_t*>(&src);
    auto* d=reinterpret_cast<uint64_t*>(&dst);
    for(size_t i=0;i<sizeof(SyscallFrame)/sizeof(uint64_t);++i)d[i]=s[i];
}

// mov eax,SysGetVersion; int 80h
// mov eax,SysPing; int 80h
// mov eax,SysPing; int 80h
// mov edi,22; mov eax,SysExit; int 80h; ud2
static const uint8_t kSmokeCode[] = {
    0xB8,0x02,0x00,0x00,0x00, 0xCD,0x80,
    0xB8,0x03,0x00,0x00,0x00, 0xCD,0x80,
    0xB8,0x03,0x00,0x00,0x00, 0xCD,0x80,
    0xBF,0x16,0x00,0x00,0x00,
    0xB8,0x01,0x00,0x00,0x00, 0xCD,0x80,
    0x0F,0x0B
};

static UserRunResult finish_run(UserProgramContext& context,uint64_t raw,int64_t& exitCode) {
    gCurrentPid=0;
    gActiveContext=nullptr;
    vmm_activate_kernel_space();
    if(context.suspended) {
        ++gStats.cooperativeYields;
        exitCode=0;
        return UserRunResult::Yielded;
    }
    exitCode=static_cast<int64_t>(raw);
    gStats.exitCode=exitCode;
    return UserRunResult::Exited;
}
}

bool usermode_init() {
    gStats = {};
    syscall_init();
    void* codePage = pmm_alloc_page();
    void* stackPage = pmm_alloc_page();
    if (!codePage || !stackPage) return false;
    auto* c = static_cast<uint8_t*>(codePage);
    auto* s = static_cast<uint8_t*>(stackPage);
    gSmokeCodePhysical = c;
    zero_bytes(c,PAGE_SIZE); zero_bytes(s,PAGE_SIZE);
    for (size_t i=0;i<sizeof(kSmokeCode);++i) c[i]=kSmokeCode[i];
    if (!vmm_map_page(USER_CODE, reinterpret_cast<uint64_t>(codePage), PagePresent | PageUser)) return false;
    if (!vmm_map_page(USER_STACK, reinterpret_cast<uint64_t>(stackPage), PagePresent | PageWritable | PageUser | PageNoExecute)) return false;
    gStats.codeVirtual = USER_CODE;
    gStats.stackVirtual = USER_STACK;
    gStats.stackGuardLow = USER_STACK_GUARD_LOW;
    gStats.stackGuardHigh = USER_STACK_GUARD_HIGH;
    gStats.stackGuardsReady = true;
    gStats.tssReady = true;
    return true;
}

bool usermode_smoke_test() {
    if (!gStats.tssReady) return false;
    gStats.ring3Entered = true;
    const uint64_t result = zeront_enter_user(USER_CODE, USER_STACK + PAGE_SIZE - 16, 0);
    (void)result;
    gStats.syscallReturned = true;
    gStats.syscalls = syscall_count();
    gStats.pings = syscall_ping_count();
    gStats.exitCode = syscall_last_exit_code();
    return gStats.exitCode == 22 && gStats.pings == 2;
}

bool usermode_fault_smoke_test() {
    if (!gStats.tssReady || !gSmokeCodePhysical) return false;
    static const uint8_t crashCode[] = {0x0F,0x0B};
    zero_bytes(gSmokeCodePhysical,PAGE_SIZE);
    for (size_t i=0;i<sizeof(crashCode);++i) gSmokeCodePhysical[i]=crashCode[i];
    const int64_t result=static_cast<int64_t>(zeront_enter_user(USER_CODE,USER_STACK + PAGE_SIZE - 16,0));
    return result < 0;
}

bool usermode_prepare_context(UserProgramContext& context,
                              const void* code,
                              uint32_t codeSize,
                              uint32_t entryOffset,
                              uint64_t pid) {
    context={};
    if(!gStats.tssReady||!code||codeSize==0||entryOffset>=codeSize||!pid)return false;
    const uint32_t pages=(codeSize+uint32_t(PAGE_SIZE)-1u)/uint32_t(PAGE_SIZE);
    if(pages==0||pages>USERMODE_MAX_CODE_PAGES)return false;
    if(!vmm_create_user_space(context.space))return false;

    const auto* src=static_cast<const uint8_t*>(code);
    uint32_t copied=0;
    for(uint32_t i=0;i<pages;++i){
        void* page=pmm_alloc_page();
        if(!page){++gStats.oomFailures;usermode_destroy_context(context);return false;}
        zero_bytes(page,PAGE_SIZE);
        auto* dst=static_cast<uint8_t*>(page);
        const uint32_t remain=codeSize-copied;
        const uint32_t chunk=remain>PAGE_SIZE?uint32_t(PAGE_SIZE):remain;
        for(uint32_t j=0;j<chunk;++j)dst[j]=src[copied+j];
        copied+=chunk;
        if(!vmm_map_page_in_space(context.space,USER_CODE+uint64_t(i)*PAGE_SIZE,
                                  reinterpret_cast<uint64_t>(page),PagePresent|PageUser|PageOwned)){
            pmm_free_page(page);usermode_destroy_context(context);return false;
        }
        context.codePages[context.codePageCount++]=page;
    }

    // One unmapped page below and one above the user stack are permanent guard
    // pages.  Stack overflow/underflow therefore faults before corrupting code
    // or another allocation.
    for(uint32_t i=0;i<USERMODE_STACK_PAGES;++i){
        void* page=pmm_alloc_page();
        if(!page){++gStats.oomFailures;usermode_destroy_context(context);return false;}
        zero_bytes(page,PAGE_SIZE);
        if(!vmm_map_page_in_space(context.space,USER_STACK+uint64_t(i)*PAGE_SIZE,
                                  reinterpret_cast<uint64_t>(page),
                                  PagePresent|PageWritable|PageUser|PageOwned|PageNoExecute)){
            pmm_free_page(page);usermode_destroy_context(context);return false;
        }
        context.stackPages[context.stackPageCount++]=page;
    }
    uint64_t guardPhys=0,guardFlags=0;
    if(vmm_query_page_in_space(context.space,USER_STACK_GUARD_LOW,guardPhys,guardFlags) ||
       vmm_query_page_in_space(context.space,USER_STACK_GUARD_HIGH,guardPhys,guardFlags)){
        usermode_destroy_context(context);return false;
    }

    context.pid=pid;
    context.codeSize=codeSize;
    context.entryOffset=entryOffset;
    context.prepared=true;
    ++gStats.liveContexts;
    return true;
}

UserRunResult usermode_start_context(UserProgramContext& context,int64_t& exitCode) {
    exitCode=-1;
    if(!context.prepared||context.suspended)return UserRunResult::Failed;
    if(!vmm_activate_space(context.space))return UserRunResult::Failed;
    gCurrentPid=context.pid;
    gActiveContext=&context;
    context.suspended=false;
    gStats.ring3Entered=true;
    const uint64_t raw=zeront_enter_user(USER_CODE+context.entryOffset,USER_STACK_TOP,0);
    return finish_run(context,raw,exitCode);
}

UserRunResult usermode_resume_context(UserProgramContext& context,int64_t& exitCode) {
    exitCode=-1;
    if(!context.prepared||!context.suspended)return UserRunResult::Failed;
    if(!vmm_activate_space(context.space))return UserRunResult::Failed;
    gCurrentPid=context.pid;
    gActiveContext=&context;
    context.suspended=false;
    const uint64_t raw=zeront_resume_user(&context.frame);
    return finish_run(context,raw,exitCode);
}

bool usermode_capture_yield_frame(const SyscallFrame* frame) {
    if(!frame||!gActiveContext||!gActiveContext->prepared)return false;
    copy_frame(gActiveContext->frame,*frame);
    // INT 80h has already advanced RIP.  Return value from SysYield is zero.
    gActiveContext->frame.rax=0;
    gActiveContext->suspended=true;
    return true;
}

void usermode_destroy_context(UserProgramContext& context) {
    // PageOwned PTEs are reclaimed by vmm_destroy_user_space().  This makes
    // teardown exception-safe and avoids leaking pages when an app exits after
    // a partial allocation sequence.
    if(context.space.cr3)vmm_destroy_user_space(context.space);
    if(context.prepared&&gStats.liveContexts)--gStats.liveContexts;
    context={};
}

bool usermode_run_code_for_process(const void* code,uint32_t codeSize,uint32_t entryOffset,uint64_t pid,int64_t& exitCode) {
    UserProgramContext context{};
    if(!usermode_prepare_context(context,code,codeSize,entryOffset,pid))return false;
    const UserRunResult r=usermode_start_context(context,exitCode);
    const bool ok=r==UserRunResult::Exited;
    usermode_destroy_context(context);
    gStats.syscalls=syscall_count();
    gStats.pings=syscall_ping_count();
    return ok;
}

bool usermode_run_code(const void* code,uint32_t codeSize,uint32_t entryOffset,int64_t& exitCode) {
    // pid 1 is only used by old smoke/test callers that pre-date process slots.
    return usermode_run_code_for_process(code,codeSize,entryOffset,1,exitCode);
}

uint64_t usermode_current_pid(){return gCurrentPid;}

UserModeStats usermode_stats() {
    gStats.syscalls = syscall_count();
    gStats.pings = syscall_ping_count();
    gStats.exitCode = syscall_last_exit_code();
    return gStats;
}
