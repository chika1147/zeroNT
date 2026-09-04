#include "ntzero/serial.h"

#define COM1 0x3F8u

static inline void out8(u16 port, u8 value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 in8(u16 port) {
    u8 value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void SerialInitialize(void) {
    out8(COM1 + 1u, 0x00u);
    out8(COM1 + 3u, 0x80u);
    out8(COM1 + 0u, 0x01u);
    out8(COM1 + 1u, 0x00u);
    out8(COM1 + 3u, 0x03u);
    out8(COM1 + 2u, 0xC7u);
    out8(COM1 + 4u, 0x0Bu);
}

static void SerialPutChar(char c) {
    while ((in8(COM1 + 5u) & 0x20u) == 0u) {
        __asm__ volatile ("pause");
    }
    out8(COM1, (u8)c);
}

void SerialWrite(const char *text) {
    if (text == (const char *)0) {
        return;
    }
    while (*text != '\0') {
        if (*text == '\n') {
            SerialPutChar('\r');
        }
        SerialPutChar(*text++);
    }
}

void SerialWriteHex(u64 value) {
    static const char hex[] = "0123456789ABCDEF";
    int shift;
    SerialWrite("0x");
    for (shift = 60; shift >= 0; shift -= 4) {
        SerialPutChar(hex[(value >> (u32)shift) & 0xFULL]);
    }
}

void SerialWriteDec(u64 value) {
    char buf[32];
    u32 n = 0u;
    if (value == 0u) {
        SerialPutChar('0');
        return;
    }
    while (value != 0u && n < 31u) {
        buf[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n != 0u) {
        SerialPutChar(buf[--n]);
    }
}
