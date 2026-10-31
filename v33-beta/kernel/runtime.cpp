#include <stddef.h>
#include <stdint.h>

extern "C" void* memset(void* dst, int value, size_t count) {
    auto* p = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < count; ++i) p[i] = static_cast<uint8_t>(value);
    return dst;
}

extern "C" void* memcpy(void* dst, const void* src, size_t count) {
    auto* d = static_cast<uint8_t*>(dst);
    auto* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < count; ++i) d[i] = s[i];
    return dst;
}

extern "C" void* memmove(void* dst, const void* src, size_t count) {
    auto* d = static_cast<uint8_t*>(dst);
    auto* s = static_cast<const uint8_t*>(src);
    if (d < s) {
        for (size_t i = 0; i < count; ++i) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = count; i != 0; --i) d[i-1] = s[i-1];
    }
    return dst;
}
