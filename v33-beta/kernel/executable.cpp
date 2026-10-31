#include "include/executable.hpp"
#include "include/usermode.hpp"
#include "include/heap.hpp"
#include "include/process.hpp"
#include "include/gui.hpp"
#include <stddef.h>

namespace {
constexpr uint32_t MAX_RUNNING=16;
constexpr size_t MAX_IMAGE = sizeof(ZeroNTExeHeader) + USERMODE_MAX_CODE_PAGES * 4096u;
struct RunningExe {
    bool used;
    uint64_t pid;
    char path[64];
    UserProgramContext user;
};
static RunningExe gRunning[MAX_RUNNING]{};

static bool magic_ok(const ZeroNTExeHeader& h) {
    return h.magic[0]=='Z' && h.magic[1]=='E' && h.magic[2]=='X' && h.magic[3]=='E' && h.version==1;
}
static void copy_text(char* dst,size_t cap,const char* src) {
    if(!dst||!cap)return;size_t i=0;if(src)while(i+1<cap&&src[i]){dst[i]=src[i];++i;}dst[i]=0;
}
static RunningExe* alloc_slot() {
    for(auto& r:gRunning)if(!r.used){r={};r.used=true;return &r;}
    return nullptr;
}
static void finish_slot(RunningExe& r,int64_t code) {
    if(!r.used)return;
    gui_close_owner(r.pid);
    process_mark_exited(r.pid,static_cast<int>(code));
    usermode_destroy_context(r.user);
    r={};
}
}

bool executable_run(Fat32Volume& volume,const char* path,ExecutableStats& out) {
    out={};copy_text(out.path,sizeof(out.path),path);
    auto* image=static_cast<uint8_t*>(kmalloc(MAX_IMAGE));
    if(!image)return false;
    size_t got=0;
    const bool readOk=fat32_read_file(volume,path,image,MAX_IMAGE,got);
    if(!readOk||got<sizeof(ZeroNTExeHeader)){kfree(image);return false;}
    const auto* h=reinterpret_cast<const ZeroNTExeHeader*>(image);
    if(!magic_ok(*h)||h->codeSize==0||h->entryOffset>=h->codeSize||
       h->codeSize>USERMODE_MAX_CODE_PAGES*4096u||sizeof(ZeroNTExeHeader)+h->codeSize>got){
        kfree(image);return false;
    }

    out.loaded=true;out.bytes=h->codeSize;
    RunningExe* slot=alloc_slot();
    if(!slot){process_reap_exited(4);slot=alloc_slot();}
    if(!slot){kfree(image);return false;}

    const uint64_t pid=process_create(path);
    if(!pid){slot->used=false;process_reap_exited(8);const uint64_t retry=process_create(path);if(!retry){kfree(image);return false;}slot->used=true;slot->pid=retry;}
    else slot->pid=pid;
    copy_text(slot->path,sizeof(slot->path),path);
    out.pid=slot->pid;

    if(!usermode_prepare_context(slot->user,image+sizeof(ZeroNTExeHeader),h->codeSize,h->entryOffset,slot->pid)){
        process_mark_exited(slot->pid,-1);slot->used=false;kfree(image);return false;
    }
    kfree(image);

    int64_t exitCode=-1;
    const UserRunResult r=usermode_start_context(slot->user,exitCode);
    out.ran=r!=UserRunResult::Failed;
    if(r==UserRunResult::Yielded){
        out.running=true;out.exitCode=0;return true;
    }
    out.exitCode=exitCode;
    finish_slot(*slot,exitCode);
    process_reap_exited(2);
    return r==UserRunResult::Exited;
}

void executable_poll() {
    for(auto& r:gRunning){
        if(!r.used||!r.user.prepared||!r.user.suspended)continue;
        int64_t code=-1;
        const UserRunResult state=usermode_resume_context(r.user,code);
        if(state==UserRunResult::Exited||state==UserRunResult::Failed)finish_slot(r,state==UserRunResult::Exited?code:-2);
    }
    process_reap_exited(2);
}

void executable_shutdown_all() {
    for(auto& r:gRunning)if(r.used)finish_slot(r,-3);
    process_reap_exited();
}

uint32_t executable_running_count() {
    uint32_t n=0;for(auto&r:gRunning)if(r.used)++n;return n;
}
