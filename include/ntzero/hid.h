#ifndef NTZERO_HID_H
#define NTZERO_HID_H

#include "types.h"

typedef struct NTZ_HID_KEY_EVENT {
    u8 modifier;
    u8 keycode;
    int pressed;
} NTZ_HID_KEY_EVENT;

typedef struct NTZ_HID_MOUSE_STATE {
    u8 buttons;
    i8 dx;
    i8 dy;
    i8 wheel;
} NTZ_HID_MOUSE_STATE;

u32 HidKeyboardBootDiff(const u8 previous[8], const u8 current[8], NTZ_HID_KEY_EVENT *events, u32 capacity);
void HidMouseBootDecode(const u8 *report, u32 length, NTZ_HID_MOUSE_STATE *state);

#endif
