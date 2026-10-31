#include "include/quality.hpp"
#include "include/event_log.hpp"

namespace { static QualityStats g{}; }

void quality_init(){ g = {}; }
void quality_record_fault(FaultKind kind,uint64_t rip,uint64_t error,uint64_t cr2,bool userMode){
    g.lastFault=kind; g.lastRip=rip; g.lastError=error; g.lastCr2=cr2; g.lastFaultWasUser=userMode;
    if(userMode){ ++g.userFaultsRecovered; event_log_write(EventLevel::Error,"USER","USER-MODE FAULT ISOLATED",static_cast<uint64_t>(kind)); } else { ++g.kernelFaults; event_log_write(EventLevel::Critical,"KERNEL","KERNEL FAULT",static_cast<uint64_t>(kind)); }
}
void quality_record_denied(){ ++g.deniedOperations; }
void quality_record_recoverable_error(){ ++g.recoverableErrors; }
QualityStats quality_stats(){ return g; }
const char* quality_fault_name(FaultKind k){
    switch(k){
        case FaultKind::DivideError:return "DIVIDE ERROR";
        case FaultKind::InvalidOpcode:return "INVALID OPCODE";
        case FaultKind::DoubleFault:return "DOUBLE FAULT";
        case FaultKind::GeneralProtection:return "GENERAL PROTECTION";
        case FaultKind::PageFault:return "PAGE FAULT";
        default:return "UNKNOWN FAULT";
    }
}
