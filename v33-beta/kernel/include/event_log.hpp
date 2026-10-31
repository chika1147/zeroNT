#pragma once
#include <stdint.h>
#include "fat32.hpp"

enum class EventLevel : uint8_t { Info=0, Warning=1, Error=2, Critical=3 };

struct EventLogStats {
    uint64_t events;
    uint64_t warnings;
    uint64_t errors;
    uint64_t critical;
    uint64_t persisted;
    uint64_t persistFailures;
    uint32_t buffered;
};

void event_log_init(Fat32Volume* volume);
void event_log_write(EventLevel level, const char* source, const char* message, uint64_t code=0);
void event_log_poll(uint64_t ticks);
EventLogStats event_log_stats();
const char* event_log_last_message();
