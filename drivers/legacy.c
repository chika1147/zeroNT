#include "ntzero/drivers.h"
#include "ntzero/hw.h"

#define PIT_CH0 0x40u
#define PIT_CMD 0x43u
#define RTC_INDEX 0x70u
#define RTC_DATA 0x71u
#define PS2_STATUS 0x64u
#define PS2_CMD 0x64u
#define PS2_DATA 0x60u

NTSTATUS PitInitialize(u32 hz) {
    u32 divisor;
    if (hz == 0u || hz > 1193182u) {
        return STATUS_INVALID_PARAMETER;
    }
    divisor = 1193182u / hz;
    if (divisor == 0u || divisor > 0xFFFFu) {
        return STATUS_INVALID_PARAMETER;
    }
    IoOut8(PIT_CMD, 0x36u);
    IoOut8(PIT_CH0, (u8)(divisor & 0xFFu));
    IoOut8(PIT_CH0, (u8)((divisor >> 8) & 0xFFu));
    return STATUS_SUCCESS;
}

u8 RtcReadRegister(u8 reg) {
    IoOut8(RTC_INDEX, (u8)(reg | 0x80u));
    return IoIn8(RTC_DATA);
}

static int ps2_wait_input_clear(void) {
    u32 i;
    for (i = 0u; i < 100000u; ++i) {
        if ((IoIn8(PS2_STATUS) & 0x02u) == 0u) {
            return 1;
        }
        CpuPause();
    }
    return 0;
}

static int ps2_wait_output_full(void) {
    u32 i;
    for (i = 0u; i < 100000u; ++i) {
        if ((IoIn8(PS2_STATUS) & 0x01u) != 0u) {
            return 1;
        }
        CpuPause();
    }
    return 0;
}

NTSTATUS Ps2Initialize(void) {
    u8 config;
    u32 drain = 32u;

    if (!ps2_wait_input_clear()) {
        return STATUS_NOT_FOUND;
    }
    IoOut8(PS2_CMD, 0xADu);
    if (!ps2_wait_input_clear()) return STATUS_NOT_FOUND;
    IoOut8(PS2_CMD, 0xA7u);

    while (drain-- != 0u && (IoIn8(PS2_STATUS) & 0x01u) != 0u) {
        (void)IoIn8(PS2_DATA);
    }

    if (!ps2_wait_input_clear()) return STATUS_NOT_FOUND;
    IoOut8(PS2_CMD, 0x20u);
    if (!ps2_wait_output_full()) return STATUS_NOT_FOUND;
    config = IoIn8(PS2_DATA);
    config = (u8)((config | 0x03u) & (u8)~0x30u);

    if (!ps2_wait_input_clear()) return STATUS_NOT_FOUND;
    IoOut8(PS2_CMD, 0x60u);
    if (!ps2_wait_input_clear()) return STATUS_NOT_FOUND;
    IoOut8(PS2_DATA, config);

    if (!ps2_wait_input_clear()) return STATUS_NOT_FOUND;
    IoOut8(PS2_CMD, 0xAEu);
    if (!ps2_wait_input_clear()) return STATUS_NOT_FOUND;
    IoOut8(PS2_CMD, 0xA8u);
    return STATUS_SUCCESS;
}

int Ps2PollKeyboard(u8 *scan_code) {
    u8 status;
    if (scan_code == (u8 *)0) return 0;
    status = IoIn8(PS2_STATUS);
    if ((status & 0x01u) == 0u || (status & 0x20u) != 0u) return 0;
    *scan_code = IoIn8(PS2_DATA);
    return 1;
}

int Ps2PollMouse(u8 packet[3]) {
    u32 i;
    if (packet == (u8 *)0) return 0;
    for (i = 0u; i < 3u; ++i) {
        u32 spin;
        for (spin = 0u; spin < 10000u; ++spin) {
            u8 status = IoIn8(PS2_STATUS);
            if ((status & 0x21u) == 0x21u) {
                packet[i] = IoIn8(PS2_DATA);
                break;
            }
            CpuPause();
        }
        if (spin == 10000u) return 0;
    }
    return 1;
}
