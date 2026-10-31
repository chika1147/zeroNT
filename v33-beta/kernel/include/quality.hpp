#pragma once
#include <stdint.h>

enum class FaultKind : uint8_t {
    Unknown = 0,
    DivideError,
    InvalidOpcode,
    DoubleFault,
    GeneralProtection,
    PageFault,
};

struct QualityStats {
    uint64_t kernelFaults;
    uint64_t userFaultsRecovered;
    uint64_t lastRip;
    uint64_t lastError;
    uint64_t lastCr2;
    uint64_t deniedOperations;
    uint64_t recoverableErrors;
    FaultKind lastFault;
    bool lastFaultWasUser;
};

void quality_init();
void quality_record_fault(FaultKind kind, uint64_t rip, uint64_t error, uint64_t cr2, bool userMode);
void quality_record_denied();
void quality_record_recoverable_error();
QualityStats quality_stats();
const char* quality_fault_name(FaultKind kind);
