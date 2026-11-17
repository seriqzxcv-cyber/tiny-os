SECTIONS
{
    . = 0x7C00;
    .text : { *(.text) }

    /DISCARD/ : {
        *(.note*)
        *(.comment)
        *(.data)
        *(.bss)
        *(.eh_frame)
    }
}
