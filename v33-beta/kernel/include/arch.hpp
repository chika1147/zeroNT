#pragma once
#include <stdint.h>

struct __attribute__((packed)) DescriptorTablePtr {
    uint16_t limit;
    uint64_t base;
};

extern "C" void zeront_load_gdt(const DescriptorTablePtr* ptr);
extern "C" void zeront_load_cr3(uint64_t physicalAddress);
extern "C" uint64_t zeront_read_cr3();
extern "C" void zeront_invlpg(uint64_t virtualAddress);

static inline __attribute__((always_inline)) void outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline __attribute__((always_inline)) uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}


static inline __attribute__((always_inline)) void outw(uint16_t port, uint16_t value) {
    __asm__ __volatile__("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline __attribute__((always_inline)) uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ __volatile__("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline __attribute__((always_inline)) void outl(uint16_t port, uint32_t value) {
    __asm__ __volatile__("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline __attribute__((always_inline)) uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ __volatile__("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline __attribute__((always_inline)) void io_wait() {
    outb(0x80, 0);
}

static inline __attribute__((always_inline)) void interrupts_enable() {
    __asm__ __volatile__("sti");
}

static inline __attribute__((always_inline)) void interrupts_disable() {
    __asm__ __volatile__("cli");
}

static inline __attribute__((always_inline)) void cpu_halt() {
    __asm__ __volatile__("hlt");
}

extern "C" void zeront_irq_timer_entry();
extern "C" void zeront_schedule_entry();
extern "C" void zeront_scheduler_yield_interrupt();

extern "C" uint64_t zeront_enter_user(uint64_t userRip, uint64_t userRsp, uint64_t arg0);
extern "C" uint64_t zeront_resume_user(const void* syscallFrame);
extern "C" void zeront_syscall_entry();

extern "C" [[noreturn]] void zeront_abort_user(int64_t exitCode);
