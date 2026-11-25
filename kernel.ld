ENTRY(kernel_main)

SECTIONS
{
    . = 0x10000;                 /* kernel is loaded here by the bootloader */

    .text :   { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data :   { *(.data .data.*) }
    .bss :    { *(.bss .bss.*) *(COMMON) }
    _end = .;

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note*)
    }
}
