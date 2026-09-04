#include "ntzero/hid.h"

static int contains_key(const u8 report[8], u8 key) {
    u32 i;
    for (i = 2u; i < 8u; ++i) {
        if (report[i] == key) return 1;
    }
    return 0;
}

u32 HidKeyboardBootDiff(const u8 previous[8], const u8 current[8], NTZ_HID_KEY_EVENT *events, u32 capacity) {
    u32 count = 0u;
    u32 i;
    if (previous == (const u8 *)0 || current == (const u8 *)0 || events == (NTZ_HID_KEY_EVENT *)0) return 0u;

    for (i = 2u; i < 8u; ++i) {
        u8 key = previous[i];
        if (key != 0u && !contains_key(current, key) && count < capacity) {
            events[count].modifier = current[0];
            events[count].keycode = key;
            events[count].pressed = 0;
            ++count;
        }
    }

    for (i = 2u; i < 8u; ++i) {
        u8 key = current[i];
        if (key != 0u && !contains_key(previous, key) && count < capacity) {
            events[count].modifier = current[0];
            events[count].keycode = key;
            events[count].pressed = 1;
            ++count;
        }
    }
    return count;
}

void HidMouseBootDecode(const u8 *report, u32 length, NTZ_HID_MOUSE_STATE *state) {
    if (state == (NTZ_HID_MOUSE_STATE *)0) return;
    state->buttons = 0u;
    state->dx = 0;
    state->dy = 0;
    state->wheel = 0;
    if (report == (const u8 *)0 || length < 3u) return;
    state->buttons = report[0];
    state->dx = (i8)report[1];
    state->dy = (i8)report[2];
    if (length >= 4u) state->wheel = (i8)report[3];
}
