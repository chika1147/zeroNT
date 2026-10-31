#include "include/interrupts.hpp"
#include "include/arch.hpp"
#include "include/scheduler.hpp"
#include "include/quality.hpp"
#include <stddef.h>

namespace {
struct __attribute__((packed)) IdtEntry {
    uint16_t offsetLow;
    uint16_t selector;
    uint8_t ist;
    uint8_t typeAttr;
    uint16_t offsetMid;
    uint32_t offsetHigh;
    uint32_t zero;
};

struct __attribute__((packed)) IdtPtr {
    uint16_t limit;
    uint64_t base;
};

struct InterruptFrame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

struct __attribute__((packed)) Tss64 {
    uint32_t reserved0; uint64_t rsp0; uint64_t rsp1; uint64_t rsp2; uint64_t reserved1;
    uint64_t ist1; uint64_t ist2; uint64_t ist3; uint64_t ist4; uint64_t ist5; uint64_t ist6; uint64_t ist7;
    uint64_t reserved2; uint16_t reserved3; uint16_t iomapBase;
};
alignas(16) static uint64_t gGdt[7];
alignas(16) static Tss64 gTss{};
alignas(16) static uint8_t gTssStack[32768]{};
alignas(16) static IdtEntry gIdt[256];
static volatile uint64_t gTicks = 0;
static volatile bool gFatalException = false;

constexpr uint32_t KEYBUF_SIZE = 128;
static volatile uint8_t gKeyBuf[KEYBUF_SIZE];
static volatile uint32_t gKeyHead = 0;
static volatile uint32_t gKeyTail = 0;

constexpr uint32_t MOUSEBUF_SIZE = 256;
static volatile uint8_t gMouseBuf[MOUSEBUF_SIZE];
static volatile uint32_t gMouseHead = 0;
static uint8_t gMousePacketSize = 3;
static volatile uint32_t gMouseTail = 0;

static inline __attribute__((always_inline)) void pic_eoi_master() { outb(0x20, 0x20); }
static inline __attribute__((always_inline)) void pic_eoi_slave() { outb(0xA0, 0x20); outb(0x20, 0x20); }

static inline __attribute__((always_inline)) void ring_push_key(uint8_t v) {
    const uint32_t next = (gKeyHead + 1) & (KEYBUF_SIZE - 1);
    if (next == gKeyTail) return;
    gKeyBuf[gKeyHead] = v;
    gKeyHead = next;
}

static inline __attribute__((always_inline)) void ring_push_mouse(uint8_t v) {
    const uint32_t next = (gMouseHead + 1) & (MOUSEBUF_SIZE - 1);
    if (next == gMouseTail) return;
    gMouseBuf[gMouseHead] = v;
    gMouseHead = next;
}

[[noreturn]] static void fatal_fault(FaultKind kind, InterruptFrame* frame, uint64_t error, uint64_t cr2) {
    const bool user = frame && ((frame->cs & 3u) == 3u);
    quality_record_fault(kind, frame ? frame->rip : 0, error, cr2, user);
    if (user) {
        // Ring-3 faults are isolated: return to the kernel-side executable loader.
        zeront_abort_user(-1000 - static_cast<int64_t>(kind));
    }
    gFatalException = true;
    interrupts_disable();
    for (;;) cpu_halt();
}

extern "C" __attribute__((interrupt)) void exception_no_error(InterruptFrame* f) { fatal_fault(FaultKind::Unknown,f,0,0); }
extern "C" __attribute__((interrupt)) void exception_with_error(InterruptFrame* f, uint64_t e) { fatal_fault(FaultKind::Unknown,f,e,0); }
extern "C" __attribute__((interrupt)) void exception_divide(InterruptFrame* f) { fatal_fault(FaultKind::DivideError,f,0,0); }
extern "C" __attribute__((interrupt)) void exception_invalid_opcode(InterruptFrame* f) { fatal_fault(FaultKind::InvalidOpcode,f,0,0); }
extern "C" __attribute__((interrupt)) void exception_double_fault(InterruptFrame* f, uint64_t e) { fatal_fault(FaultKind::DoubleFault,f,e,0); }
extern "C" __attribute__((interrupt)) void exception_gpf(InterruptFrame* f, uint64_t e) { fatal_fault(FaultKind::GeneralProtection,f,e,0); }
extern "C" __attribute__((interrupt)) void exception_page_fault(InterruptFrame* f, uint64_t e) { uint64_t cr2=0; __asm__ __volatile__("mov %%cr2,%0":"=r"(cr2)); fatal_fault(FaultKind::PageFault,f,e,cr2); }

extern "C" uint64_t* zeront_timer_dispatch(uint64_t* frame) {
    ++gTicks;
    pic_eoi_master();
    return scheduler_on_timer(frame);
}

extern "C" uint64_t* zeront_schedule_dispatch(uint64_t* frame) {
    return scheduler_on_yield(frame);
}

extern "C" __attribute__((interrupt)) void irq_keyboard(InterruptFrame*) {
    // The i8042 data port is shared.  Never consume an AUX byte from IRQ1;
    // doing so shifts the mouse packet and produces sudden/jagged movement.
    const uint8_t status = inb(0x64);
    if ((status & 0x01) != 0 && (status & 0x20) == 0) ring_push_key(inb(0x60));
    pic_eoi_master();
}

extern "C" __attribute__((interrupt)) void irq_mouse(InterruptFrame*) {
    // Likewise, IRQ12 only owns bytes tagged AUX by the controller.
    const uint8_t status = inb(0x64);
    if ((status & 0x01) != 0 && (status & 0x20) != 0) ring_push_mouse(inb(0x60));
    pic_eoi_slave();
}

extern "C" __attribute__((interrupt)) void irq_spurious(InterruptFrame*) {
    // Safe generic handler for currently unused PIC vectors.
    pic_eoi_slave();
}

static void idt_set(uint8_t vector, void (*handler)(), uint8_t flags = 0x8E) {
    const uint64_t addr = reinterpret_cast<uint64_t>(handler);
    IdtEntry& e = gIdt[vector];
    e.offsetLow = static_cast<uint16_t>(addr & 0xFFFF);
    e.selector = 0x08;
    e.ist = 0;
    e.typeAttr = flags;
    e.offsetMid = static_cast<uint16_t>((addr >> 16) & 0xFFFF);
    e.offsetHigh = static_cast<uint32_t>((addr >> 32) & 0xFFFFFFFF);
    e.zero = 0;
}

static bool ps2_wait_write() {
    for (uint32_t i = 0; i < 100000; ++i) if ((inb(0x64) & 0x02) == 0) return true;
    return false;
}

static bool ps2_wait_read() {
    for (uint32_t i = 0; i < 100000; ++i) if ((inb(0x64) & 0x01) != 0) return true;
    return false;
}

static void ps2_command(uint8_t command) {
    if (!ps2_wait_write()) return;
    outb(0x64, command);
}

static void ps2_data(uint8_t data) {
    if (!ps2_wait_write()) return;
    outb(0x60, data);
}

static uint8_t ps2_read() {
    if (!ps2_wait_read()) return 0xFF;
    return inb(0x60);
}

static void mouse_write(uint8_t value) {
    ps2_command(0xD4);
    ps2_data(value);
    (void)ps2_read(); // ACK when available
}
}

void gdt_init() {
    gGdt[0] = 0;
    gGdt[1] = 0x00AF9A000000FFFFULL; // 0x08 kernel code
    gGdt[2] = 0x00CF92000000FFFFULL; // 0x10 kernel data
    gGdt[3] = 0x00AFFA000000FFFFULL; // 0x18 user code, selector 0x1B
    gGdt[4] = 0x00CFF2000000FFFFULL; // 0x20 user data, selector 0x23
    gTss = {};
    gTss.rsp0 = reinterpret_cast<uint64_t>(gTssStack + sizeof(gTssStack));
    gTss.iomapBase = sizeof(Tss64);
    const uint64_t base = reinterpret_cast<uint64_t>(&gTss);
    const uint64_t limit = sizeof(Tss64) - 1;
    uint64_t low = 0;
    low |= (limit & 0xFFFFULL);
    low |= (base & 0xFFFFFFULL) << 16;
    low |= 0x89ULL << 40; // present, available 64-bit TSS
    low |= ((limit >> 16) & 0xFULL) << 48;
    low |= ((base >> 24) & 0xFFULL) << 56;
    gGdt[5] = low;
    gGdt[6] = (base >> 32) & 0xFFFFFFFFULL;
    DescriptorTablePtr ptr{};
    ptr.limit = static_cast<uint16_t>(sizeof(gGdt) - 1);
    ptr.base = reinterpret_cast<uint64_t>(&gGdt[0]);
    zeront_load_gdt(&ptr);
    const uint16_t tssSelector = 0x28;
    __asm__ __volatile__("ltr %0" : : "r"(tssSelector));
}

void idt_init() {
    for (size_t i = 0; i < 256; ++i) gIdt[i] = {};

    // x86 exceptions that push an error code: 8,10,11,12,13,14,17,21,29,30.
    for (uint8_t v = 0; v < 32; ++v) {
        const bool hasError = v == 8 || v == 10 || v == 11 || v == 12 || v == 13 || v == 14 ||
                              v == 17 || v == 21 || v == 29 || v == 30;
        if (hasError)
            idt_set(v, reinterpret_cast<void(*)()>(exception_with_error));
        else
            idt_set(v, reinterpret_cast<void(*)()>(exception_no_error));
    }
    // High-value faults get dedicated handlers so crash diagnostics are meaningful.
    idt_set(0,  reinterpret_cast<void(*)()>(exception_divide));
    idt_set(6,  reinterpret_cast<void(*)()>(exception_invalid_opcode));
    idt_set(8,  reinterpret_cast<void(*)()>(exception_double_fault));
    idt_set(13, reinterpret_cast<void(*)()>(exception_gpf));
    idt_set(14, reinterpret_cast<void(*)()>(exception_page_fault));

    for (uint16_t v = 32; v < 256; ++v)
        idt_set(static_cast<uint8_t>(v), reinterpret_cast<void(*)()>(irq_spurious));
    idt_set(32, reinterpret_cast<void(*)()>(zeront_irq_timer_entry));
    idt_set(33, reinterpret_cast<void(*)()>(irq_keyboard));
    idt_set(44, reinterpret_cast<void(*)()>(irq_mouse));
    idt_set(0x30, reinterpret_cast<void(*)()>(zeront_schedule_entry));
    idt_set(0x80, reinterpret_cast<void(*)()>(zeront_syscall_entry), 0xEE); // DPL3 interrupt gate

    IdtPtr ptr{};
    ptr.limit = static_cast<uint16_t>(sizeof(gIdt) - 1);
    ptr.base = reinterpret_cast<uint64_t>(&gIdt[0]);
    __asm__ __volatile__("lidt %0" : : "m"(ptr));
}

void pic_init() {
    // ICW1: start initialization.
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    // ICW2: map master to 0x20 and slave to 0x28.
    outb(0x21, 0x20); io_wait();
    outb(0xA1, 0x28); io_wait();
    // ICW3: slave on IRQ2.
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    // ICW4: 8086 mode.
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();

    // Enable timer, keyboard, cascade and mouse only.
    outb(0x21, static_cast<uint8_t>(0xFF & ~(1u << 0) & ~(1u << 1) & ~(1u << 2)));
    outb(0xA1, static_cast<uint8_t>(0xFF & ~(1u << 4)));
}

void pit_init(uint32_t hz) {
    if (hz < 18) hz = 18;
    if (hz > 1000) hz = 1000;
    const uint32_t divisor = 1193182u / hz;
    outb(0x43, 0x36);
    outb(0x40, static_cast<uint8_t>(divisor & 0xFF));
    outb(0x40, static_cast<uint8_t>((divisor >> 8) & 0xFF));
}

void ps2_init() {
    interrupts_disable();

    // Disable devices while updating the controller configuration byte.
    ps2_command(0xAD);
    ps2_command(0xA7);
    while (inb(0x64) & 0x01) (void)inb(0x60);

    ps2_command(0x20);
    uint8_t cfg = ps2_read();
    cfg |= 0x03;               // IRQ1 + IRQ12
    cfg &= static_cast<uint8_t>(~0x30); // clocks enabled
    ps2_command(0x60);
    ps2_data(cfg);

    ps2_command(0xAE);
    ps2_command(0xA8);

    // Keyboard: enable scanning. Ignore ACK details for this first native driver.
    ps2_data(0xF4);
    (void)ps2_read();

    // Mouse: defaults, then try the IntelliMouse sample-rate handshake for wheel packets.
    mouse_write(0xF6);
    mouse_write(0xF3); mouse_write(200);
    mouse_write(0xF3); mouse_write(100);
    mouse_write(0xF3); mouse_write(80);
    ps2_command(0xD4);
    ps2_data(0xF2);
    (void)ps2_read(); // ACK
    const uint8_t mouseId = ps2_read();
    gMousePacketSize = (mouseId == 3 || mouseId == 4) ? 4 : 3;
    mouse_write(0xF4);
}

uint64_t timer_ticks() { return gTicks; }

bool keyboard_pop(uint8_t& scancode) {
    if (gKeyTail == gKeyHead) return false;
    scancode = gKeyBuf[gKeyTail];
    gKeyTail = (gKeyTail + 1) & (KEYBUF_SIZE - 1);
    return true;
}

bool mouse_pop(uint8_t& data) {
    if (gMouseTail == gMouseHead) return false;
    data = gMouseBuf[gMouseTail];
    gMouseTail = (gMouseTail + 1) & (MOUSEBUF_SIZE - 1);
    return true;
}

uint8_t mouse_packet_size() { return gMousePacketSize; }
