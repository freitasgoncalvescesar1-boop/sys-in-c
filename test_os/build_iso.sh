#!/bin/bash
set -e

echo "=== Compilando Kernel e Gerando myos.iso ==="

mkdir -p build_os
mkdir -p isodir/boot/grub

# 1. Assembly
gcc -m32 -c test_os/boot.s -o build_os/boot.o
gcc -m32 -c test_os/interrupts.s -o build_os/interrupts.o

# 2. Kernel C & Drivers
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra test_os/kernel.c -o build_os/kernel.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra test_os/gdt.c -o build_os/gdt.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra test_os/idt.c -o build_os/idt.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra test_os/ps2.c -o build_os/ps2.o

# 3. Freestanding Core (incluindo as rotinas de divisao kdiv64)
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kdiv64.c -o build_os/kdiv64.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kmem.c -o build_os/kmem.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kfixed.c -o build_os/kfixed.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kprintf.c -o build_os/kprintf.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kgfx.c -o build_os/kgfx.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kstring.c -o build_os/kstring.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kvfs.c -o build_os/kvfs.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kringbuf.c -o build_os/kringbuf.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/klist.c -o build_os/klist.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kspinlock.c -o build_os/kspinlock.o

# 4. Linkagem direta
LIBGCC_PATH=$(gcc -m32 -print-libgcc-file-name 2>/dev/null || true)

ld -m elf_i386 -T test_os/linker.ld \
  build_os/boot.o build_os/interrupts.o build_os/kernel.o \
  build_os/gdt.o build_os/idt.o build_os/ps2.o \
  build_os/kdiv64.o build_os/kmem.o build_os/kfixed.o build_os/kprintf.o build_os/kgfx.o \
  build_os/kstring.o build_os/kvfs.o build_os/kringbuf.o build_os/klist.o build_os/kspinlock.o \
  $LIBGCC_PATH \
  -o myos.bin

# 5. Validacao Multiboot
if command -v grub-file >/dev/null 2>&1; then
    grub-file --is-x86-multiboot myos.bin
    echo "Cabecalho Multiboot validado com sucesso!"
fi

# 6. Criacao da ISO com GRUB
if command -v grub-mkrescue >/dev/null 2>&1; then
    cp myos.bin isodir/boot/myos.bin
    cp test_os/grub.cfg isodir/boot/grub/grub.cfg
    grub-mkrescue -o myos.iso isodir
    echo "=== myos.iso gerada com sucesso! ==="
    ls -lh myos.iso
fi
