#include "ntzero/boot.h"
#include "ntzero/serial.h"
#include "ntzero/alpc.h"
#include "ntzero/drivers.h"

static void halt_forever(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void KiSystemStartup(NTZ_BOOT_INFO *boot) {
    NTZ_DRIVER_SUMMARY drivers;

    SerialInitialize();
    SerialWrite("\nNTZero Alpha 0.1\n");
    SerialWrite("KiSystemStartup: x86_64 native kernel entered\n");

    if (boot == (NTZ_BOOT_INFO *)0 || boot->magic != NTZERO_BOOT_MAGIC) {
        SerialWrite("BOOT: invalid boot information\n");
        halt_forever();
    }

    SerialWrite("BOOT: UEFI ExitBootServices complete\n");
    SerialWrite("BOOT: memory map bytes = ");
    SerialWriteDec(boot->uefi_memory_map_size);
    SerialWrite("\nBOOT: descriptor bytes = ");
    SerialWriteDec(boot->uefi_descriptor_size);
    SerialWrite("\n");

    DriversInitializeAll(boot, &drivers);

    AlpcInitialize();
    SerialWrite("ALPC: kernel transport initialized\n");
    SerialWrite("NTZero Alpha 0.1: READY\n");
    halt_forever();
}
