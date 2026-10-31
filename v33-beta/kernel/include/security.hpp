#pragma once
#include <stdint.h>
#include <stddef.h>

struct SecurityStats {
    uint64_t deniedSyscalls;
    uint64_t badUserPointers;
    uint64_t loginFailures;
    uint64_t loginSuccesses;
    uint64_t lockouts;
};

void security_init();
bool security_validate_user_range(uint64_t address, size_t bytes, bool writeAccess);
void security_record_denied_syscall();
void security_record_login(bool success);
void security_record_lockout();
SecurityStats security_stats();
