#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

CC=${CC:-gcc}
LD=${LD:-ld}
AS=${AS:-as}
OBJCOPY=${OBJCOPY:-objcopy}

CFLAGS="-m32 -ffreestanding -fno-pic -fno-stack-protector -fno-builtin -std=gnu99 -O2 -Wall -Wextra -Wno-array-bounds -g"

echo "[1/7] generating font..."
python3 tools/genfont.py

echo "[2/7] compiling kernel..."
$CC $CFLAGS -c kernel.c -o kernel.o

echo "[3/7] linking kernel..."
$LD -m elf_i386 -T kernel.ld kernel.o -o kernel.elf
$OBJCOPY -O binary kernel.elf kernel.bin

KSIZE=$(stat -c%s kernel.bin)
KSECT=$(( (KSIZE + 511) / 512 ))
KENTRY=$(nm -n kernel.elf | grep -w kernel_main | awk '{print "0x"$1}')
KBSS=$(nm -n kernel.elf | grep -w _end | awk '{print "0x"$1}')
echo "      kernel size: $KSIZE bytes ($KSECT sectors), entry $KENTRY"

echo "[4/7] assembling bootloader (KERNEL_SECTORS=$KSECT)..."
$AS --32 --defsym KERNEL_SECTORS=$KSECT --defsym KERNEL_ENTRY=$KENTRY --defsym KERNEL_BSS_END=$KBSS boot.S -o boot.o
$LD -m elf_i386 -T boot.ld --oformat binary boot.o -o boot.bin

BSIZE=$(stat -c%s boot.bin)
if [ "$BSIZE" -ne 512 ]; then
    echo "ERROR: boot.bin is $BSIZE bytes, expected 512" >&2
    exit 1
fi

echo "[5/7] building disk image..."
TOTAL=$(( 1 + KSECT ))
dd if=/dev/zero of=os.img bs=512 count=$TOTAL status=none
dd if=boot.bin of=os.img bs=512 conv=notrunc status=none
dd if=kernel.bin of=os.img bs=512 seek=1 conv=notrunc status=none

echo "[6/7] building bootable CD image..."
# El Torito loads the whole image at 0x7C00; the CD-specific bootloader
# copies the kernel into place instead of calling int 13h (drive 0xE0).
$AS --32 --defsym KERNEL_SECTORS=$KSECT --defsym KERNEL_ENTRY=$KENTRY \
    --defsym KERNEL_BSS_END=$KBSS --defsym NO_DISK_READ=1 boot.S -o boot_iso.o
$LD -m elf_i386 -T boot.ld --oformat binary boot_iso.o -o boot_iso.bin
ISO_SIZE=$(stat -c%s boot_iso.bin)
if [ "$ISO_SIZE" -ne 512 ]; then
    echo "ERROR: boot_iso.bin is $ISO_SIZE bytes, expected 512" >&2
    exit 1
fi
dd if=/dev/zero of=os-cd.img bs=512 count=$TOTAL status=none
dd if=boot_iso.bin of=os-cd.img bs=512 conv=notrunc status=none
dd if=kernel.bin of=os-cd.img bs=512 seek=1 conv=notrunc status=none

echo "[7/7] building ISO..."
if command -v genisoimage >/dev/null 2>&1; then
    rm -rf .iso-root
    mkdir -p .iso-root
    cp os-cd.img .iso-root/boot.img
    cp README.md .iso-root/README.txt
    genisoimage -quiet -o tinyos.iso \
        -V TINY_OS -A "TINY OS" -P "TINY OS" \
        -b boot.img -c boot.cat -no-emul-boot \
        -boot-load-size $TOTAL -J -r .iso-root
    rm -rf .iso-root
    echo "      tinyos.iso: $(stat -c%s tinyos.iso) bytes"
else
    echo "      genisoimage не найден - ISO пропущен"
fi

echo "done."
echo "      os.img: $(stat -c%s os.img) bytes"
echo "      boot signature: $(xxd -s 510 -l 2 -p os.img 2>/dev/null || echo '?')"
