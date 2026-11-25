/* =========================================================================
 * tinyos - 16-bit bootloader
 *  - reads the 32-bit kernel from disk (LBA via int 13h extended read)
 *  - switches to VESA 1024x768x32bpp linear framebuffer
 *  - stores framebuffer info at physical 0x0500..0x0509
 *  - enters 32-bit protected mode and jumps to the kernel
 * ========================================================================= */

#ifndef KERNEL_SECTORS
#define KERNEL_SECTORS 64
#endif
#ifndef KERNEL_ENTRY
#define KERNEL_ENTRY 0x10000
#endif
.ifndef KERNEL_BSS_END
.set KERNEL_BSS_END, (0x10000 + KERNEL_SECTORS * 512)
.endif

.code16
.section .text
.global _start

_start:
    cli
    cld
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7C00, %sp

    movb    %dl, boot_drive

    /* --- enable A20 (fast gate at port 0x92) --- */
    inb     $0x92, %al
    orb     $0x02, %al
    andb    $0xFE, %al
    outb    %al, $0x92

    /* --- get the kernel to 0000:1000 (physical 0x10000) --- */
    /* NOTE: use gas-native .ifndef here.  "#ifndef" would just be a comment
     * because as(1) does not run the C preprocessor. */
.ifndef NO_DISK_READ
    /* Booting from a hard disk image: read via int 13h.  Try LBA first,
     * then CHS (used when booting from an emulated floppy). */
    /* Read in 32-sector chunks: large kernels exceed the BIOS limit for
     * a single int 13h request.  All state lives in memory because some
     * BIOS implementations clobber registers during int 13h. */
    movw    $KERNEL_SECTORS, remain
    movw    $0x1000, bufseg
    movl    $1, dap+8
lba_loop:
    movw    remain, %ax
    cmpw    $32, %ax
    jbe     lba_count
    movw    $32, %ax
lba_count:
    movw    %ax, chunk
    movw    %ax, dap+2
    movw    bufseg, %ax
    movw    %ax, dap+6
    xorl    %eax, %eax
    movb    $0x42, %ah
    movb    boot_drive, %dl
    movw    $dap, %si
    int     $0x13
    jc      chs_read
    movw    chunk, %ax
    subw    %ax, remain
    movzwl  %ax, %eax
    addl    %eax, dap+8
    shlw    $5, %ax
    addw    %ax, bufseg
    cmpw    $0, remain
    jne     lba_loop
    jmp     kernel_loaded

chs_read:
    movw    $0x0000, %ax
    movw    %ax, %es
    movw    $0x1000, %bx
    movb    $0x02, %ah
    movb    $KERNEL_SECTORS, %al
    movb    $0x00, %ch
    movb    $0x01, %cl
    movb    $0x00, %dh
    movb    boot_drive, %dl
    int     $0x13
    jc      halt
.else
    /* CD boot: El Torito already loaded the whole image at 0x7C00, and
     * drive 0xE0 cannot be read with int 13h.  Just copy the kernel. */
    movw    $0x1000, %ax
    movw    %ax, %es
    /* destination overlaps the source: copy backwards to avoid corruption */
    std
    movl    $(KERNEL_SECTORS * 512 - 1), %edi
    movl    $(0x7E00 + KERNEL_SECTORS * 512 - 1), %esi
    movl    $(KERNEL_SECTORS * 512), %ecx
    addr32 rep movsb
    cld
.endif

kernel_loaded:

    /* the file image stops before .bss; zero the rest of the kernel */
    xorw    %ax, %ax
    movw    %ax, %es
    cld
    movl    $(0x10000 + KERNEL_SECTORS * 512), %edi
    movl    $KERNEL_BSS_END, %ecx
    subl    %edi, %ecx
    jz      bss_done
    xorl    %eax, %eax
    addr32 rep stosb
bss_done:

    /* --- VESA: set mode 0x118 (1024x768, 32bpp, linear FB) --- */
    movw    $0x4F02, %ax
    movw    $0x4118, %bx
    int     $0x10
    cmpw    $0x004F, %ax
    jne     halt

    /* --- VESA: get mode info into 0800:0000 (below the kernel at 0x10000) --- */
    movw    $0x4F01, %ax
    movw    $0x0118, %cx
    movw    $0x0800, %bx
    movw    %bx, %es
    xorw    %di, %di
    int     $0x10
    cmpw    $0x004F, %ax
    jne     halt

    /* copy framebuffer pointer / pitch / width / height to safe low memory */
    movl    %es:0x28, %eax       /* linear framebuffer physical address */
    movl    %eax, 0x0500
    movw    %es:0x10, %ax        /* bytes per scanline */
    movw    %ax, 0x0504
    movw    %es:0x12, %ax        /* width */
    movw    %ax, 0x0506
    movw    %es:0x14, %ax        /* height */
    movw    %ax, 0x0508
    movb    %es:0x19, %al        /* bits per pixel */
    movb    %al, 0x050A

    /* --- GDT --- */
    lgdt    gdtr

    /* --- protected mode --- */
    movl    %cr0, %eax
    orl     $1, %eax
    movl    %eax, %cr0
    ljmpl   $0x08, $protected_mode

.align 8
gdtr:
    .word   gdt_end - gdt - 1
    .long   gdt

.align 8
gdt:
    .quad 0x0000000000000000         /* null descriptor */
    .quad 0x00CF9A000000FFFF         /* code: base 0, limit 4GB, 32-bit */
    .quad 0x00CF92000000FFFF         /* data: base 0, limit 4GB, 32-bit */
gdt_end:

/* --- Disk Address Packet --- */
.align 8
dap:
    .byte 0x10
    .byte 0
    .word KERNEL_SECTORS
    .word 0x0000
    .word 0x1000
    .quad 1

boot_drive:
    .byte 0
remain:
    .word 0
bufseg:
    .word 0
chunk:
    .word 0

.code32
protected_mode:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    movl    $0x90000, %esp
    cld

    movl    $KERNEL_ENTRY, %eax
    call    *%eax

halt:
    hlt
    jmp     halt

/* boot sector signature: offset 0x1FE from the start of this section (= physical 0x7DFE) */
.org 0x1FE
    .byte 0x55, 0xAA
