#include "ntzero/interrupts.h"
#include "ntzero/serial.h"
#include "ntzero/hw.h"
#include "ntzero/drivers.h"

typedef struct NTZ_IDT_GATE {
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 type_attr;
    u16 offset_mid;
    u32 offset_high;
    u32 zero;
} __attribute__((packed)) NTZ_IDT_GATE;

typedef struct NTZ_IDTR {
    u16 limit;
    u64 base;
} __attribute__((packed)) NTZ_IDTR;

typedef struct NTZ_INTERRUPT_FRAME {
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
} NTZ_INTERRUPT_FRAME;

typedef struct NTZ_HANDLER_SLOT {
    NTZ_INTERRUPT_HANDLER handler;
    void *context;
} NTZ_HANDLER_SLOT;

static NTZ_IDT_GATE g_idt[256];
static NTZ_HANDLER_SLOT g_handlers[256];
static u16 g_code_selector;
static u16 g_vector_bitmap;

static void halt_exception(void) {
    for (;;) __asm__ volatile ("cli; hlt");
}

static void dispatch_vector(u8 vector, u64 error_code) {
    NTZ_HANDLER_SLOT *slot = &g_handlers[vector];
    if (slot->handler != (NTZ_INTERRUPT_HANDLER)0) {
        slot->handler(vector, error_code, slot->context);
    } else if (vector < 32u) {
        SerialWrite("EXCEPTION vector=");
        SerialWriteDec(vector);
        SerialWrite(" error=");
        SerialWriteHex(error_code);
        SerialWrite("\n");
        halt_exception();
    }
    if (vector >= 32u) LapicEoi();
}

#define ISR_NOERR(n) \
    __attribute__((interrupt)) static void isr_##n(NTZ_INTERRUPT_FRAME *frame) { \
        (void)frame; dispatch_vector((u8)(n), 0u); \
    }

#define ISR_ERR(n) \
    __attribute__((interrupt)) static void isr_##n(NTZ_INTERRUPT_FRAME *frame, u64 error_code) { \
        (void)frame; dispatch_vector((u8)(n), error_code); \
    }

ISR_NOERR(0)
ISR_NOERR(1)
ISR_NOERR(2)
ISR_NOERR(3)
ISR_NOERR(4)
ISR_NOERR(5)
ISR_NOERR(6)
ISR_NOERR(7)
ISR_ERR(8)
ISR_NOERR(9)
ISR_ERR(10)
ISR_ERR(11)
ISR_ERR(12)
ISR_ERR(13)
ISR_ERR(14)
ISR_NOERR(15)
ISR_NOERR(16)
ISR_ERR(17)
ISR_NOERR(18)
ISR_NOERR(19)
ISR_NOERR(20)
ISR_ERR(21)
ISR_NOERR(22)
ISR_NOERR(23)
ISR_NOERR(24)
ISR_NOERR(25)
ISR_NOERR(26)
ISR_NOERR(27)
ISR_NOERR(28)
ISR_ERR(29)
ISR_ERR(30)
ISR_NOERR(31)
ISR_NOERR(32)
ISR_NOERR(33)
ISR_NOERR(34)
ISR_NOERR(35)
ISR_NOERR(36)
ISR_NOERR(37)
ISR_NOERR(38)
ISR_NOERR(39)
ISR_NOERR(40)
ISR_NOERR(41)
ISR_NOERR(42)
ISR_NOERR(43)
ISR_NOERR(44)
ISR_NOERR(45)
ISR_NOERR(46)
ISR_NOERR(47)
ISR_NOERR(48)
ISR_NOERR(49)
ISR_NOERR(50)
ISR_NOERR(51)
ISR_NOERR(52)
ISR_NOERR(53)
ISR_NOERR(54)
ISR_NOERR(55)
ISR_NOERR(56)
ISR_NOERR(57)
ISR_NOERR(58)
ISR_NOERR(59)
ISR_NOERR(60)
ISR_NOERR(61)
ISR_NOERR(62)
ISR_NOERR(63)
ISR_NOERR(64)
ISR_NOERR(65)
ISR_NOERR(66)
ISR_NOERR(67)
ISR_NOERR(68)
ISR_NOERR(69)
ISR_NOERR(70)
ISR_NOERR(71)
ISR_NOERR(72)
ISR_NOERR(73)
ISR_NOERR(74)
ISR_NOERR(75)
ISR_NOERR(76)
ISR_NOERR(77)
ISR_NOERR(78)
ISR_NOERR(79)
ISR_NOERR(80)
ISR_NOERR(81)
ISR_NOERR(82)
ISR_NOERR(83)
ISR_NOERR(84)
ISR_NOERR(85)
ISR_NOERR(86)
ISR_NOERR(87)
ISR_NOERR(88)
ISR_NOERR(89)
ISR_NOERR(90)
ISR_NOERR(91)
ISR_NOERR(92)
ISR_NOERR(93)
ISR_NOERR(94)
ISR_NOERR(95)

static void default_stub(void) {
    halt_exception();
}

static void set_gate(u8 vector, void *handler) {
    u64 address = (u64)(usize)handler;
    NTZ_IDT_GATE *gate = &g_idt[vector];
    gate->offset_low = (u16)(address & 0xFFFFu);
    gate->selector = g_code_selector;
    gate->ist = 0u;
    gate->type_attr = 0x8Eu;
    gate->offset_mid = (u16)((address >> 16) & 0xFFFFu);
    gate->offset_high = (u32)(address >> 32);
    gate->zero = 0u;
}

NTSTATUS InterruptInitialize(void) {
    NTZ_IDTR idtr;
    u32 i;
    static void *const stubs[96] = {
        (void *)isr_0, (void *)isr_1, (void *)isr_2, (void *)isr_3, (void *)isr_4, (void *)isr_5, (void *)isr_6, (void *)isr_7,
        (void *)isr_8, (void *)isr_9, (void *)isr_10, (void *)isr_11, (void *)isr_12, (void *)isr_13, (void *)isr_14, (void *)isr_15,
        (void *)isr_16, (void *)isr_17, (void *)isr_18, (void *)isr_19, (void *)isr_20, (void *)isr_21, (void *)isr_22, (void *)isr_23,
        (void *)isr_24, (void *)isr_25, (void *)isr_26, (void *)isr_27, (void *)isr_28, (void *)isr_29, (void *)isr_30, (void *)isr_31,
        (void *)isr_32, (void *)isr_33, (void *)isr_34, (void *)isr_35, (void *)isr_36, (void *)isr_37, (void *)isr_38, (void *)isr_39,
        (void *)isr_40, (void *)isr_41, (void *)isr_42, (void *)isr_43, (void *)isr_44, (void *)isr_45, (void *)isr_46, (void *)isr_47,
        (void *)isr_48, (void *)isr_49, (void *)isr_50, (void *)isr_51, (void *)isr_52, (void *)isr_53, (void *)isr_54, (void *)isr_55,
        (void *)isr_56, (void *)isr_57, (void *)isr_58, (void *)isr_59, (void *)isr_60, (void *)isr_61, (void *)isr_62, (void *)isr_63,
        (void *)isr_64, (void *)isr_65, (void *)isr_66, (void *)isr_67, (void *)isr_68, (void *)isr_69, (void *)isr_70, (void *)isr_71,
        (void *)isr_72, (void *)isr_73, (void *)isr_74, (void *)isr_75, (void *)isr_76, (void *)isr_77, (void *)isr_78, (void *)isr_79,
        (void *)isr_80, (void *)isr_81, (void *)isr_82, (void *)isr_83, (void *)isr_84, (void *)isr_85, (void *)isr_86, (void *)isr_87,
        (void *)isr_88, (void *)isr_89, (void *)isr_90, (void *)isr_91, (void *)isr_92, (void *)isr_93, (void *)isr_94, (void *)isr_95
    };

    __asm__ volatile ("mov %%cs, %0" : "=r"(g_code_selector));
    for (i = 0u; i < 256u; ++i) {
        g_handlers[i].handler = (NTZ_INTERRUPT_HANDLER)0;
        g_handlers[i].context = (void *)0;
        set_gate((u8)i, (void *)default_stub);
    }
    for (i = 0u; i < 96u; ++i) set_gate((u8)i, stubs[i]);

    IoOut8(0x21u, 0xFFu);
    IoOut8(0xA1u, 0xFFu);

    idtr.limit = (u16)(sizeof(g_idt) - 1u);
    idtr.base = (u64)(usize)&g_idt[0];
    __asm__ volatile ("lidt %0" : : "m"(idtr));
    g_vector_bitmap = 0u;
    return STATUS_SUCCESS;
}

NTSTATUS InterruptRegister(u8 vector, NTZ_INTERRUPT_HANDLER handler, void *context) {
    if (vector < 32u || handler == (NTZ_INTERRUPT_HANDLER)0) return STATUS_INVALID_PARAMETER;
    g_handlers[vector].handler = handler;
    g_handlers[vector].context = context;
    return STATUS_SUCCESS;
}

void InterruptUnregister(u8 vector) {
    if (vector >= 32u) {
        g_handlers[vector].handler = (NTZ_INTERRUPT_HANDLER)0;
        g_handlers[vector].context = (void *)0;
    }
}

NTSTATUS InterruptAllocateVector(u8 *out_vector) {
    u32 i;
    if (out_vector == (u8 *)0) return STATUS_INVALID_PARAMETER;
    for (i = 0u; i < 16u; ++i) {
        u16 mask = (u16)(1u << i);
        if ((g_vector_bitmap & mask) == 0u) {
            g_vector_bitmap |= mask;
            *out_vector = (u8)(0x50u + i);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_INSUFFICIENT_RESOURCES;
}

void InterruptFreeVector(u8 vector) {
    if (vector >= 0x50u && vector <= 0x5Fu) {
        g_vector_bitmap &= (u16)~(1u << (vector - 0x50u));
        InterruptUnregister(vector);
    }
}

void InterruptEnable(void) { __asm__ volatile ("sti"); }
void InterruptDisable(void) { __asm__ volatile ("cli"); }
