ENTRY(_start)
PHDRS {
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
  stack PT_GNU_STACK FLAGS(6);
  payload PT_NULL;
}
SECTIONS {
  . = 0x200000;
  .text : { *(.text._start) *(.text .text.*) } :text
  .rodata : { *(.rodata .rodata.*) } :text
  . = ALIGN(4096);
  .data : { *(.data .data.*) KEEP(*(.apctx)) KEEP(*(.apembed)) } :data
  .bss : { *(.bss .bss.*) *(COMMON) } :data
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
