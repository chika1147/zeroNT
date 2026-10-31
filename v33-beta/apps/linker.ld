ENTRY(_start)
SECTIONS
{
  . = 0;
  .text : ALIGN(16) {
    KEEP(*(.text.entry))
    *(.text*)
    *(.rodata*)
  }
  .data : ALIGN(16) { *(.data*) }
  /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) *(.llvm_addrsig*) }
}
