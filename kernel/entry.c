#include "ntzero/boot.h"
#include "ntzero/serial.h"
#include "ntzero/alpc.h"
#include "ntzero/drivers.h"
#include "ntzero/memory.h"
#include "ntzero/interrupts.h"

static void halt_forever(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void KiSystemStartup(NTZ_BOOT_INFO *boot) {
    NTZ_DRIVER_SUMMARY drivers;
    NTZ_MEMORY_STATS memory;
    NTSTATUS status;

    InterruptDisable();
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

    status = InterruptInitialize();
    if (status != STATUS_SUCCESS) {
        SerialWrite("IDT: initialization failed\n");
        halt_forever();
    }
    SerialWrite("IDT: exceptions + IRQ/MSI vector table installed (interrupts masked)\n");

    status = PmmInitializeFromBoot(boot);
    if (status != STATUS_SUCCESS) {
        SerialWrite("PMM: initialization failed\n");
        halt_forever();
    }
    PmmGetStats(&memory);
    SerialWrite("PMM: free pages = ");
    SerialWriteDec(memory.free_pages);
    SerialWrite(" (~");
    SerialWriteDec(memory.free_pages / 256u);
    SerialWrite(" MiB) ranges = ");
    SerialWriteDec(memory.free_ranges);
    SerialWrite("\nDMA: contiguous physical allocator ready (identity-map baseline)\n");

    DriversInitializeAll(boot, &drivers);

    AlpcInitialize();
    SerialWrite("ALPC: kernel transport initialized\n");
    SerialWrite("NTZero Alpha 0.1: READY\n");
    halt_forever();
}
