#include "include/security.hpp"
#include "include/paging.hpp"
#include "include/quality.hpp"
#include "include/event_log.hpp"

namespace {
static SecurityStats g{};
constexpr uint64_t USER_LOW  = 0x0000000000010000ULL;
constexpr uint64_t USER_HIGH = 0x00007FFFFFFFFFFFULL;
}

void security_init(){ g = {}; }
bool security_validate_user_range(uint64_t address,size_t bytes,bool writeAccess){
    if(bytes==0) return true;
    if(address<USER_LOW || address>USER_HIGH || bytes-1>USER_HIGH-address){ ++g.badUserPointers; quality_record_denied(); return false; }
    uint64_t start=address&~0xFFFULL, end=(address+bytes-1)&~0xFFFULL;
    for(uint64_t p=start;;p+=4096){
        uint64_t phys=0,flags=0;
        if(!vmm_query_page(p,phys,flags) || !(flags&PagePresent) || !(flags&PageUser) || (writeAccess && !(flags&PageWritable))){ ++g.badUserPointers; quality_record_denied(); return false; }
        if(p==end) break;
    }
    return true;
}
void security_record_denied_syscall(){ ++g.deniedSyscalls; quality_record_denied(); event_log_write(EventLevel::Warning,"SYSCALL","DENIED OR UNKNOWN SYSCALL",g.deniedSyscalls); }
void security_record_login(bool success){ if(success)++g.loginSuccesses;else ++g.loginFailures; }
void security_record_lockout(){ ++g.lockouts; }
SecurityStats security_stats(){ return g; }
