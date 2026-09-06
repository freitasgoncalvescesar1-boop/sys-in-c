#!/bin/bash

echo "Configurando esteira de CI para compilar a ISO do test_os..."

# 1. Cria a pasta do GitHub Actions
mkdir -p .github/workflows

# 2. Cria o workflow do CI do GitHub Actions
cat << 'EOF' > .github/workflows/build-iso.yml
name: Build test_os Bootable ISO

on:
  push:
    paths:
      - 'test_os/**'
      - 'freestanding/**'
      - '.github/workflows/build-iso.yml'
  pull_request:
    paths:
      - 'test_os/**'
      - 'freestanding/**'
  workflow_dispatch: # Permite disparar o build manualmente no GitHub

jobs:
  build-kernel-iso:
    runs-on: ubuntu-latest

    steps:
      - name: Checkout do Repositorio
        uses: actions/checkout@v4

      - name: Instalar Dependencias de Compilacao x86 e GRUB
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            gcc-multilib \
            binutils \
            xorriso \
            grub-pc-bin \
            grub-common \
            mtools \
            qemu-system-x86

      - name: Compilar Kernel Multiboot1 (myos.bin)
        run: |
          mkdir -p build_os
          
          # 1. Montagem dos arquivos Assembly x86
          gcc -m32 -c test_os/boot.s -o build_os/boot.o
          gcc -m32 -c test_os/interrupts.s -o build_os/interrupts.o
          
          # 2. Compilacao dos drivers do test_os
          gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra test_os/kernel.c -o build_os/kernel.o
          gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra test_os/gdt.c -o build_os/gdt.o
          gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra test_os/idt.c -o build_os/idt.o
          gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra test_os/ps2.c -o build_os/ps2.o
          
          # 3. Compilacao dos modulos freestanding
          gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kmem.c -o build_os/kmem.o
          gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kfixed.c -o build_os/kfixed.o
          gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kprintf.c -o build_os/kprintf.o
          gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kgfx.c -o build_os/kgfx.o
          gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kstring.c -o build_os/kstring.o
          gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kvfs.c -o build_os/kvfs.o
          gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kringbuf.c -o build_os/kringbuf.o
          gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/klist.c -o build_os/klist.o
          gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kspinlock.c -o build_os/kspinlock.o
          
          # 4. Linkagem do kernel ELF de 32 bits
          gcc -m32 -T test_os/linker.ld -nostdlib -ffreestanding -no-pie \
            build_os/boot.o build_os/interrupts.o build_os/kernel.o \
            build_os/gdt.o build_os/idt.o build_os/ps2.o \
            build_os/kmem.o build_os/kfixed.o build_os/kprintf.o build_os/kgfx.o \
            build_os/kstring.o build_os/kvfs.o build_os/kringbuf.o build_os/klist.o build_os/kspinlock.o \
            -lgcc -o myos.bin

      - name: Validar Cabecalho Multiboot
        run: |
          grub-file --is-x86-multiboot myos.bin
          echo "Cabecalho Multiboot1 validado com sucesso!"

      - name: Gerar Imagem ISO Bootavel (myos.iso)
        run: |
          mkdir -p isodir/boot/grub
          cp myos.bin isodir/boot/myos.bin
          cp test_os/grub.cfg isodir/boot/grub/grub.cfg
          grub-mkrescue -o myos.iso isodir
          ls -lh myos.iso

      - name: Upload da ISO como Artefato
        uses: actions/upload-artifact@v4
        with:
          name: test_os-bootable-iso
          path: myos.iso
          retention-days: 14
EOF

# 3. Cria o script local para compilar a ISO em maquinas Linux (test_os/build_iso.sh)
cat << 'EOF' > test_os/build_iso.sh
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

# 3. Freestanding
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kmem.c -o build_os/kmem.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kfixed.c -o build_os/kfixed.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kprintf.c -o build_os/kprintf.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kgfx.c -o build_os/kgfx.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kstring.c -o build_os/kstring.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kvfs.c -o build_os/kvfs.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kringbuf.c -o build_os/kringbuf.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/klist.c -o build_os/klist.o
gcc -m32 -c -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra freestanding/kspinlock.c -o build_os/kspinlock.o

# 4. Linkagem
gcc -m32 -T test_os/linker.ld -nostdlib -ffreestanding -no-pie \
  build_os/boot.o build_os/interrupts.o build_os/kernel.o \
  build_os/gdt.o build_os/idt.o build_os/ps2.o \
  build_os/kmem.o build_os/kfixed.o build_os/kprintf.o build_os/kgfx.o \
  build_os/kstring.o build_os/kvfs.o build_os/kringbuf.o build_os/klist.o build_os/kspinlock.o \
  -lgcc -o myos.bin

# 5. Criacao da ISO
cp myos.bin isodir/boot/myos.bin
cp test_os/grub.cfg isodir/boot/grub/grub.cfg
grub-mkrescue -o myos.iso isodir

echo "=== myos.iso gerada com sucesso! ==="
ls -lh myos.iso
EOF
chmod +x test_os/build_iso.sh

echo "Atualizando Makefile com target 'iso'..."
cat << 'EOF' > Makefile
PREFIX ?= /usr/local
CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -fPIC
LDFLAGS_IPC = -L. -lutilipc -Wl,-rpath,. -lpthread

LIB_IPC = libutilipc.so
SRC_TOOLS = sysbox calc passgen bigfiles portcheck hashcalc b64 sysinfo org netinfo ffind ipcmon simplehost watchcmd strutils fdup deview cpuplot qrcli bench get-info utils-help netclip snc jsonview speedtest httpget tedit netscan dnsquery diskbench krypt rawcat hwcaps matrix pwr sntp tree pythont chip8 bytebeat disasm asciiray imgview zpack iotscan vsec
HEAVY_TOOLS = raycast3d dnsserver
LOW_TOOLS = chmod cat rmd cp xxd ln stat ls df peekmem pv ps kill whoami ltop ping magic lsh mv mkdir jail which ptrace rcv printenv
INSTALL_LOW_TOOLS = chmod cat rmd cp xxd ln stat ls df peekmem pv ps kill whoami ltop ping magic lsh mv mkdir jail which ptrace rcv printenv
ALL_TOOLS = $(SRC_TOOLS) $(HEAVY_TOOLS) $(LOW_TOOLS)

all: $(LIB_IPC) $(ALL_TOOLS)

$(LIB_IPC): src/libutilipc/utilipc.c src/libutilipc/utilipc.h
	$(CC) $(CFLAGS) -shared src/libutilipc/utilipc.c -o $(LIB_IPC) -lpthread

# --- HEAVY-UTILS ---
raycast3d: heavy_utils/raycast3d.c $(LIB_IPC)
	$(CC) $(CFLAGS) heavy_utils/raycast3d.c -o raycast3d $(LDFLAGS_IPC) -lm

dnsserver: heavy_utils/dnsserver.c $(LIB_IPC)
	$(CC) $(CFLAGS) heavy_utils/dnsserver.c -o dnsserver $(LDFLAGS_IPC) -lm

# --- FERRAMENTAS SRC ---
sysbox: src/sysbox/sysbox.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/sysbox/sysbox.c -o sysbox $(LDFLAGS_IPC) -lm
calc: src/calc/calc.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/calc/calc.c -o calc $(LDFLAGS_IPC) -lm
passgen: src/passgen/passgen.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/passgen/passgen.c -o passgen $(LDFLAGS_IPC)
bigfiles: src/bigfiles/bigfiles.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/bigfiles/bigfiles.c -o bigfiles $(LDFLAGS_IPC)
portcheck: src/portcheck/portcheck.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/portcheck/portcheck.c -o portcheck $(LDFLAGS_IPC)
hashcalc: src/hashcalc/hashcalc.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/hashcalc/hashcalc.c -o hashcalc $(LDFLAGS_IPC)
b64: src/b64/b64.c
	$(CC) $(CFLAGS) src/b64/b64.c -o b64
sysinfo: src/sysinfo/sysinfo.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/sysinfo/sysinfo.c -o sysinfo $(LDFLAGS_IPC)
org: src/org/org.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/org/org.c -o org $(LDFLAGS_IPC)
netinfo: src/netinfo/netinfo.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/netinfo/netinfo.c -o netinfo $(LDFLAGS_IPC)
ffind: src/ffind/ffind.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/ffind/ffind.c -o ffind $(LDFLAGS_IPC)
ipcmon: src/ipcmon/ipcmon.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/ipcmon/ipcmon.c -o ipcmon $(LDFLAGS_IPC)
simplehost: src/simplehost/simplehost.c src/simplehost/http_pages.c src/simplehost/security.c src/simplehost/server.h $(LIB_IPC)
	$(CC) $(CFLAGS) src/simplehost/simplehost.c src/simplehost/http_pages.c src/simplehost/security.c -o simplehost $(LDFLAGS_IPC)
watchcmd: src/watchcmd/watchcmd.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/watchcmd/watchcmd.c -o watchcmd $(LDFLAGS_IPC)
strutils: src/strutils/strutils.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/strutils/strutils.c -o strutils $(LDFLAGS_IPC)
fdup: src/fdup/fdup.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/fdup/fdup.c -o fdup $(LDFLAGS_IPC)
deview: src/deview/deview.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/deview/deview.c -o deview $(LDFLAGS_IPC)
cpuplot: src/cpuplot/cpuplot.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/cpuplot/cpuplot.c -o cpuplot $(LDFLAGS_IPC)
qrcli: src/qrcli/qrcli.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/qrcli/qrcli.c -o qrcli $(LDFLAGS_IPC)
bench: src/bench/bench.c $(LIB_IPC)
	$(CC) $(CFLAGS) -O0 src/bench/bench.c -o bench $(LDFLAGS_IPC)
get-info: src/get-info/get-info.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/get-info/get-info.c -o get-info $(LDFLAGS_IPC)
utils-help: src/utils-help/utils-help.c
	$(CC) $(CFLAGS) src/utils-help/utils-help.c -o utils-help
netclip: src/netclip/netclip.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/netclip/netclip.c -o netclip $(LDFLAGS_IPC)
snc: src/snc/snc.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/snc/snc.c -o snc $(LDFLAGS_IPC)
jsonview: src/jsonview/jsonview.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/jsonview/jsonview.c -o jsonview $(LDFLAGS_IPC)
speedtest: src/speedtest/speedtest.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/speedtest/speedtest.c -o speedtest $(LDFLAGS_IPC)
httpget: src/httpget/httpget.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/httpget/httpget.c -o httpget $(LDFLAGS_IPC)
tedit: src/tedit/tedit.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/tedit/tedit.c -o tedit $(LDFLAGS_IPC)
netscan: src/netscan/netscan.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/netscan/netscan.c -o netscan $(LDFLAGS_IPC)
dnsquery: src/dnsquery/dnsquery.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/dnsquery/dnsquery.c -o dnsquery $(LDFLAGS_IPC)
diskbench: src/diskbench/diskbench.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/diskbench/diskbench.c -o diskbench $(LDFLAGS_IPC)
krypt: src/krypt/krypt.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/krypt/krypt.c -o krypt $(LDFLAGS_IPC)
rawcat: src/rawcat/rawcat.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/rawcat/rawcat.c -o rawcat $(LDFLAGS_IPC)
hwcaps: src/hwcaps/hwcaps.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/hwcaps/hwcaps.c -o hwcaps $(LDFLAGS_IPC)
matrix: src/matrix/matrix.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/matrix/matrix.c -o matrix $(LDFLAGS_IPC)
pwr: src/pwr/pwr.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/pwr/pwr.c -o pwr $(LDFLAGS_IPC) -lm
sntp: src/sntp/sntp.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/sntp/sntp.c -o sntp $(LDFLAGS_IPC) -lm
tree: src/tree/tree.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/tree/tree.c -o tree $(LDFLAGS_IPC)
pythont: src/pythont/pythont.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/pythont/pythont.c -o pythont $(LDFLAGS_IPC) -lm
chip8: src/chip8/chip8.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/chip8/chip8.c -o chip8 $(LDFLAGS_IPC)
bytebeat: src/bytebeat/bytebeat.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/bytebeat/bytebeat.c -o bytebeat $(LDFLAGS_IPC) -lm
disasm: src/disasm/disasm.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/disasm/disasm.c -o disasm $(LDFLAGS_IPC)
asciiray: src/asciiray/asciiray.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/asciiray/asciiray.c -o asciiray $(LDFLAGS_IPC) -lm
imgview: src/imgview/imgview.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/imgview/imgview.c -o imgview $(LDFLAGS_IPC) -lm
zpack: src/zpack/zpack.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/zpack/zpack.c -o zpack $(LDFLAGS_IPC)
iotscan: src/iotscan/iotscan.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/iotscan/iotscan.c -o iotscan $(LDFLAGS_IPC)
vsec: src/vsec/vsec.c $(LIB_IPC)
	$(CC) $(CFLAGS) src/vsec/vsec.c -o vsec $(LDFLAGS_IPC) -lm

# --- LOW-UTILS ---
chmod: low-utils/chmod.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/chmod.c -o chmod
cat: low-utils/cat.c low-utils/low.h
	$(CC) -Wall -Wextra -O3 low-utils/cat.c -o cat
rmd: low-utils/rmd.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/rmd.c -o rmd
cp: low-utils/cp.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/cp.c -o cp
xxd: low-utils/xxd.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/xxd.c -o xxd
ln: low-utils/ln.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/ln.c -o ln
stat: low-utils/stat.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/stat.c -o stat
ls: low-utils/ls.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/ls.c -o ls
df: low-utils/df.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/df.c -o df
peekmem: low-utils/peekmem.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/peekmem.c -o peekmem
pv: low-utils/pv.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/pv.c -o pv
ps: low-utils/ps.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/ps.c -o ps
kill: low-utils/kill.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/kill.c -o kill
whoami: low-utils/whoami.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/whoami.c -o whoami
ltop: low-utils/ltop.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/ltop.c -o ltop
ping: low-utils/ping.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/ping.c -o ping -lm
magic: low-utils/magic.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/magic.c -o magic
lsh: low-utils/lsh.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/lsh.c -o lsh
mv: low-utils/mv.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/mv.c -o mv
mkdir: low-utils/mkdir.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/mkdir.c -o mkdir
jail: low-utils/jail.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/jail.c -o jail
which: low-utils/which.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/which.c -o which
ptrace: low-utils/ptrace.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/ptrace.c -o ptrace
rcv: low-utils/rcv.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/rcv.c -o rcv
printenv: low-utils/printenv.c low-utils/low.h
	$(CC) $(CFLAGS) low-utils/printenv.c -o printenv

# --- FREESTANDING / OS ---
free: freestanding/kmem.c freestanding/kfixed.c freestanding/kprintf.c freestanding/kgfx.c freestanding/kringbuf.c freestanding/kstring.c freestanding/klist.c freestanding/kspinlock.c freestanding/kvfs.c freestanding/kata.c freestanding/kdiskfs.c freestanding/ksound.c freestanding/kbmp.c freestanding/main_test.c freestanding/kcalc.c
	$(CC) $(CFLAGS) -ffreestanding freestanding/kmem.c freestanding/kfixed.c freestanding/kprintf.c freestanding/kgfx.c freestanding/kringbuf.c freestanding/kstring.c freestanding/klist.c freestanding/kspinlock.c freestanding/kvfs.c freestanding/kata.c freestanding/kdiskfs.c freestanding/ksound.c freestanding/kbmp.c freestanding/main_test.c -o freestanding_test
	$(CC) $(CFLAGS) -ffreestanding freestanding/kmem.c freestanding/kfixed.c freestanding/kprintf.c freestanding/kcalc.c -o kcalc

iso:
	bash test_os/build_iso.sh

install: all
	install -d $(DESTDIR)$(PREFIX)/lib
	install -m 755 $(LIB_IPC) $(DESTDIR)$(PREFIX)/lib/$(LIB_IPC)
	install -d $(DESTDIR)$(PREFIX)/bin
	for tool in $(SRC_TOOLS) $(HEAVY_TOOLS); do install -m 755 $$tool $(DESTDIR)$(PREFIX)/bin/$$tool; done
	for ltool in $(INSTALL_LOW_TOOLS); do install -m 755 $$ltool $(DESTDIR)$(PREFIX)/bin/$$ltool; done

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/$(LIB_IPC)
	for tool in $(SRC_TOOLS) $(HEAVY_TOOLS); do rm -f $(DESTDIR)$(PREFIX)/bin/$$tool; done
	for ltool in $(INSTALL_LOW_TOOLS); do rm -f $(DESTDIR)$(PREFIX)/bin/$$ltool; done

clean:
	rm -f $(ALL_TOOLS) $(LIB_IPC) freestanding_test kcalc dummy_target myos.bin myos.iso
	rm -rf build_os isodir

.PHONY: all install uninstall clean free iso
EOF

echo "CI configurado com sucesso em .github/workflows/build-iso.yml!"
