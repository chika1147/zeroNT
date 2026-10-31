#include "include/beta.hpp"
#include "include/memory.hpp"
#include "include/heap.hpp"
#include "include/safe_fs.hpp"
#include "include/ahci.hpp"
#include "include/nvme.hpp"
#include "include/usb.hpp"
#include "include/network.hpp"
#include "include/security.hpp"
#include "include/quality.hpp"
#include "include/event_log.hpp"

namespace {
static BetaStats g{};
static uint64_t gLastSample=0;
static uint64_t gPrevAhciErrors=0,gPrevNvmeErrors=0,gPrevNvmeTimeouts=0,gPrevXhciTimeouts=0;
static uint64_t gPrevRxDrops=0,gPrevTxFail=0,gPrevDenied=0;
}

void beta_init(Fat32Volume* volume){
    g={};g.freeMemoryLowWater=~0ULL;g.heapFreeLowWater=~0ULL;
    g.filesystemHealthy=volume&&safe_fs_check_volume();
    if(!g.filesystemHealthy){++g.healthWarnings;event_log_write(EventLevel::Error,"FS","BOOT FILESYSTEM CHECK FAILED");}
    else event_log_write(EventLevel::Info,"FS","BOOT FILESYSTEM CHECK PASSED");
    auto a=ahci_stats();gPrevAhciErrors=a.ioErrors;
    auto nv=nvme_stats();gPrevNvmeErrors=nv.commandErrors;gPrevNvmeTimeouts=nv.timeouts;
    auto x=xhci_stats();gPrevXhciTimeouts=x.timeouts;
    auto n=network_stats();gPrevRxDrops=n.rxDrops;gPrevTxFail=n.txFailures;
    gPrevDenied=security_stats().deniedSyscalls;
}

void beta_poll(uint64_t ticks){
    g.uptimeTicks=ticks;if(ticks-gLastSample<100)return;gLastSample=ticks;++g.samples;
    const auto m=pmm_stats();const auto h=heap_stats();if(m.freeBytes<g.freeMemoryLowWater)g.freeMemoryLowWater=m.freeBytes;if(h.freeBytes<g.heapFreeLowWater)g.heapFreeLowWater=h.freeBytes;
    const auto fs=safe_fs_stats();g.filesystemHealthy=fs.volumeHealthy;
    const auto a=ahci_stats();const auto nv=nvme_stats();g.storageHealthy=(a.controllerFound&&a.portFound&&a.identified)||(nv.controllerReady&&nv.namespaceReady&&nv.ioQueueReady);
    if(a.ioErrors>gPrevAhciErrors){++g.driverWarnings;g.healthWarnings+=a.ioErrors-gPrevAhciErrors;event_log_write(EventLevel::Warning,"AHCI","I/O ERROR OR RETRY OBSERVED",a.ioErrors);gPrevAhciErrors=a.ioErrors;}
    if(nv.commandErrors>gPrevNvmeErrors||nv.timeouts>gPrevNvmeTimeouts){++g.driverWarnings;++g.healthWarnings;event_log_write(EventLevel::Warning,"NVME","COMMAND ERROR OR TIMEOUT",nv.commandErrors+nv.timeouts);gPrevNvmeErrors=nv.commandErrors;gPrevNvmeTimeouts=nv.timeouts;}
    const auto x=xhci_stats();g.usbHealthy=!x.found||x.running;
    if(x.timeouts>gPrevXhciTimeouts){++g.driverWarnings;++g.healthWarnings;event_log_write(EventLevel::Warning,"XHCI","CONTROLLER TIMEOUT",x.timeouts);gPrevXhciTimeouts=x.timeouts;}
    const auto n=network_stats();g.networkHealthy=!n.nicFound||(n.initialized&&n.linkUp);
    if(n.rxDrops>gPrevRxDrops||n.txFailures>gPrevTxFail){++g.networkWarnings;++g.healthWarnings;event_log_write(EventLevel::Warning,"NET","PACKET DROP OR TX FAILURE",n.rxDrops+n.txFailures);gPrevRxDrops=n.rxDrops;gPrevTxFail=n.txFailures;}
    const auto s=security_stats();g.securityHealthy=true;if(s.deniedSyscalls>gPrevDenied){event_log_write(EventLevel::Warning,"SECURITY","DENIED SYSCALL",s.deniedSyscalls);gPrevDenied=s.deniedSyscalls;}
    const auto q=quality_stats();if(q.kernelFaults){g.securityHealthy=false;}
    g.readyForSoak=g.filesystemHealthy&&g.storageHealthy&&g.networkHealthy&&g.usbHealthy&&g.securityHealthy&&q.kernelFaults==0;
}
BetaStats beta_stats(){return g;}
