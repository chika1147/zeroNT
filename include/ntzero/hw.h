#ifndef NTZERO_HW_H
#define NTZERO_HW_H

#include "types.h"

static inline void IoOut8(u16 port, u8 value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void IoOut16(u16 port, u16 value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void IoOut32(u16 port, u32 value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 IoIn8(u16 port) {
    u8 value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline u16 IoIn16(u16 port) {
    u16 value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline u32 IoIn32(u16 port) {
    u32 value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void IoWait(void) {
    IoOut8(0x80u, 0u);
}

static inline u8 MmioRead8(u64 address) {
    return *(volatile u8 *)(usize)address;
}

static inline u16 MmioRead16(u64 address) {
    return *(volatile u16 *)(usize)address;
}

static inline u32 MmioRead32(u64 address) {
    return *(volatile u32 *)(usize)address;
}

static inline u64 MmioRead64(u64 address) {
    return *(volatile u64 *)(usize)address;
}

static inline void MmioWrite8(u64 address, u8 value) {
    *(volatile u8 *)(usize)address = value;
}

static inline void MmioWrite16(u64 address, u16 value) {
    *(volatile u16 *)(usize)address = value;
}

static inline void MmioWrite32(u64 address, u32 value) {
    *(volatile u32 *)(usize)address = value;
}

static inline void MmioWrite64(u64 address, u64 value) {
    *(volatile u64 *)(usize)address = value;
}

static inline void CpuPause(void) {
    __asm__ volatile ("pause");
}

#endif
