#include "ntzero/drivers.h"
#include "ntzero/hw.h"

static u64 g_lapic_base = 0xFEE00000ULL;
static u64 g_ioapic_base = 0xFEC00000ULL;

void LapicConfigure(u64 base) {
    if ((base & 0xFFFu) == 0u && base != 0u) {
        g_lapic_base = base;
    }
}

void LapicEnable(u8 spurious_vector) {
    u32 svr = MmioRead32(g_lapic_base + 0xF0u);
    svr &= ~0xFFu;
    svr |= (u32)spurious_vector;
    svr |= 0x100u;
    MmioWrite32(g_lapic_base + 0xF0u, svr);
}

void LapicEoi(void) {
    MmioWrite32(g_lapic_base + 0xB0u, 0u);
}

static u32 ioapic_read(u8 reg) {
    MmioWrite32(g_ioapic_base + 0x00u, reg);
    return MmioRead32(g_ioapic_base + 0x10u);
}

static void ioapic_write(u8 reg, u32 value) {
    MmioWrite32(g_ioapic_base + 0x00u, reg);
    MmioWrite32(g_ioapic_base + 0x10u, value);
}

void IoApicConfigure(u64 base) {
    if ((base & 0xFFFu) == 0u && base != 0u) {
        g_ioapic_base = base;
    }
}

void IoApicRoute(u8 irq, u8 vector, u8 destination_apic_id, int masked) {
    u8 reg = (u8)(0x10u + (u8)(irq * 2u));
    u32 low = (u32)vector;
    u32 high = (u32)destination_apic_id << 24;
    if (masked) low |= 1u << 16;
    ioapic_write((u8)(reg + 1u), high);
    ioapic_write(reg, low);
    (void)ioapic_read(0x01u);
}
