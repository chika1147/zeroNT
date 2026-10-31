ENTRY(kernel_entry)
SECTIONS
{
    . = 0;
    .text : ALIGN(4096) { *(.text .text.*) }
    .rodata : ALIGN(4096) { *(.rodata .rodata.*) }
    .data : ALIGN(4096) { *(.data .data.*) }
    .bss : ALIGN(4096) { *(COMMON) *(.bss .bss.*) }
    .rela.dyn : ALIGN(8) { *(.rela.dyn .rela.dyn.*) }
    .dynamic : ALIGN(8) { *(.dynamic) }
    /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
