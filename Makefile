PREFIX ?= /usr/local
CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -fPIC
LDFLAGS_IPC = -L. -lutilipc -Wl,-rpath,. -lpthread

LIB_IPC = libutilipc.so
SRC_TOOLS = sysbox calc passgen bigfiles portcheck hashcalc b64 sysinfo org netinfo ffind ipcmon simplehost watchcmd strutils fdup deview cpuplot qrcli bench get-info utils-help netclip snc jsonview speedtest httpget tedit netscan dnsquery diskbench krypt rawcat hwcaps matrix pwr sntp tree pythont chip8 bytebeat disasm asciiray
HEAVY_TOOLS = raycast3d
LOW_TOOLS = chmod cat rmd cp xxd ln stat ls df peekmem pv ps kill whoami ltop ping magic lsh mv mkdir jail which
INSTALL_LOW_TOOLS = chmod cat rmd cp xxd ln stat ls df peekmem pv ps kill whoami ltop ping magic lsh mv mkdir jail which
ALL_TOOLS = $(SRC_TOOLS) $(HEAVY_TOOLS) $(LOW_TOOLS)

all: $(LIB_IPC) $(ALL_TOOLS)

$(LIB_IPC): src/libutilipc/utilipc.c src/libutilipc/utilipc.h
	$(CC) $(CFLAGS) -shared src/libutilipc/utilipc.c -o $(LIB_IPC) -lpthread

# --- HEAVY-UTILS ---
raycast3d: heavy_utils/raycast3d.c $(LIB_IPC)
	$(CC) $(CFLAGS) heavy_utils/raycast3d.c -o raycast3d $(LDFLAGS_IPC) -lm

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

# --- FREESTANDING / OS ---
free: freestanding/kmem.c freestanding/kfixed.c freestanding/kprintf.c freestanding/kgfx.c freestanding/kringbuf.c freestanding/kstring.c freestanding/klist.c freestanding/kspinlock.c freestanding/kvfs.c freestanding/kata.c freestanding/kdiskfs.c freestanding/ksound.c freestanding/kbmp.c freestanding/main_test.c freestanding/kcalc.c
	$(CC) $(CFLAGS) -ffreestanding freestanding/kmem.c freestanding/kfixed.c freestanding/kprintf.c freestanding/kgfx.c freestanding/kringbuf.c freestanding/kstring.c freestanding/klist.c freestanding/kspinlock.c freestanding/kvfs.c freestanding/kata.c freestanding/kdiskfs.c freestanding/ksound.c freestanding/kbmp.c freestanding/main_test.c -o freestanding_test
	$(CC) $(CFLAGS) -ffreestanding freestanding/kmem.c freestanding/kfixed.c freestanding/kprintf.c freestanding/kcalc.c -o kcalc

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
	rm -f $(ALL_TOOLS) $(LIB_IPC) freestanding_test kcalc dummy_target

.PHONY: all install uninstall clean free
