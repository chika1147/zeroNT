#include "include/process.hpp"
#include <stddef.h>

namespace {
constexpr uint32_t MAX_PROCESSES = 32;
constexpr uint32_t MAX_PROCESS_THREADS = 8;

struct Process {
    uint64_t pid;
    char name[32];
    ProcessState state;
    int exitCode;
    uint64_t tids[MAX_PROCESS_THREADS];
    uint32_t threadCount;
};

static Process gProcesses[MAX_PROCESSES]{};
static uint64_t gNextPid = 1;
static uint64_t gReaped = 0;

static void copy_name(char* out, size_t cap, const char* in) {
    if (!out || cap == 0) return;
    size_t i = 0;
    if (in) {
        while (i + 1 < cap && in[i]) { out[i] = in[i]; ++i; }
    }
    out[i] = 0;
}

static Process* find_process(uint64_t pid) {
    for (uint32_t i = 0; i < MAX_PROCESSES; ++i)
        if (gProcesses[i].state != ProcessState::Unused && gProcesses[i].pid == pid)
            return &gProcesses[i];
    return nullptr;
}
}

void process_init() {
    for (uint32_t i = 0; i < MAX_PROCESSES; ++i) gProcesses[i] = {};
    gNextPid = 1;
    gReaped = 0;
}

uint64_t process_create(const char* name) {
    for (uint32_t i = 0; i < MAX_PROCESSES; ++i) {
        if (gProcesses[i].state != ProcessState::Unused) continue;
        Process& p = gProcesses[i];
        p = {};
        p.pid = gNextPid++;
        p.state = ProcessState::Running;
        copy_name(p.name, sizeof(p.name), name ? name : "process");
        return p.pid;
    }
    return 0;
}

bool process_attach_thread(uint64_t pid, uint64_t tid) {
    Process* p = find_process(pid);
    if (!p || p->threadCount >= MAX_PROCESS_THREADS) return false;
    p->tids[p->threadCount++] = tid;
    return true;
}

void process_mark_exited(uint64_t pid, int exitCode) {
    Process* p = find_process(pid);
    if (!p) return;
    p->exitCode = exitCode;
    p->state = ProcessState::Exited;
}

uint32_t process_reap_exited(uint32_t maxToReap) {
    uint32_t reaped=0;
    for(uint32_t i=0;i<MAX_PROCESSES && reaped<maxToReap;++i){
        if(gProcesses[i].state!=ProcessState::Exited) continue;
        gProcesses[i] = {};
        ++reaped; ++gReaped;
    }
    return reaped;
}

void process_enumerate(ProcessEnumerateCallback callback, void* context) {
    if(!callback) return;
    for(uint32_t i=0;i<MAX_PROCESSES;++i){
        Process& p=gProcesses[i];
        if(p.state==ProcessState::Unused) continue;
        ProcessInfo info{p.pid,p.name,p.state,p.exitCode,p.threadCount};
        callback(info,context);
    }
}

ProcessStats process_stats() {
    ProcessStats s{};
    s.nextPid = gNextPid;
    s.reapedCount = gReaped;
    for (uint32_t i = 0; i < MAX_PROCESSES; ++i) {
        if (gProcesses[i].state == ProcessState::Unused) continue;
        ++s.processCount;
        if (gProcesses[i].state == ProcessState::Running) ++s.runningCount;
        if (gProcesses[i].state == ProcessState::Exited) ++s.exitedCount;
    }
    return s;
}

const char* process_name(uint64_t pid) {
    Process* p = find_process(pid);
    return p ? p->name : "?";
}
