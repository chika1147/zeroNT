#include <stdio.h>
#include "ntzero/hid.h"
#include "ntzero/usb_mass.h"

int main(void) {
    const u8 prev[8] = {0,0,0,0,0,0,0,0};
    const u8 cur[8]  = {0x02,0,0x04,0,0,0,0,0};
    NTZ_HID_KEY_EVENT events[4];
    NTZ_HID_MOUSE_STATE mouse;
    NTZ_USB_MASS_CBW cbw;
    NTZ_USB_MASS_CSW csw;
    const u8 mouse_report[4] = {1u, 5u, (u8)-3, 1u};
    u32 count;

    count = HidKeyboardBootDiff(prev, cur, events, 4u);
    if (count != 1u || events[0].keycode != 0x04u || !events[0].pressed || events[0].modifier != 0x02u) return 1;

    HidMouseBootDecode(mouse_report, 4u, &mouse);
    if (mouse.buttons != 1u || mouse.dx != 5 || mouse.dy != -3 || mouse.wheel != 1) return 2;

    UsbMassBuildRead10(&cbw, 0x12345678u, 0x10203040u, 8u, 4096u);
    if (cbw.signature != 0x43425355u || cbw.tag != 0x12345678u || cbw.cdb[0] != 0x28u || cbw.transfer_length != 4096u) return 3;
    if (cbw.cdb[2] != 0x10u || cbw.cdb[3] != 0x20u || cbw.cdb[4] != 0x30u || cbw.cdb[5] != 0x40u) return 4;
    if (cbw.cdb[7] != 0x00u || cbw.cdb[8] != 0x08u) return 5;

    csw.signature = 0x53425355u;
    csw.tag = 0x12345678u;
    csw.residue = 0u;
    csw.status = 0u;
    if (!UsbMassValidateCsw(&csw, 0x12345678u)) return 6;

    printf("HID/USB PASS\n");
    return 0;
}
