#include <stdio.h>
#include <string.h>

static void print_general_help(void) {
    printf("======================\n");
    printf("[utils-in-c - Suite Overview]\n");
    printf("======================\n");
    printf("  calc       - Math expression evaluator (PEMDAS, variables, hex/bin, history)\n");
    printf("  passgen    - Secure random password generator\n");
    printf("  bigfiles   - Find large files sorted by size with ANSI colors\n");
    printf("  portcheck  - Network host and multi-port connection tester & TUI\n");
    printf("  hashcalc   - Fast CRC32, FNV-1a, and SHA-256 hash calculator\n");
    printf("  b64        - Fast Base64 encoder and decoder\n");
    printf("  sysinfo    - Minimal system info monitor (Android/Linux auto-detect)\n");
    printf("  org        - File organizer (move by ext, change ext, auto-group)\n");
    printf("  netinfo    - Network interfaces, Public IP, and live TUI monitor (-tui)\n");
    printf("  ffind      - Fast recursive file finder (ignores system dirs, depth 15)\n");
    printf("  ipcmon     - Live/snapshot IPC shared memory monitor\n");
    printf("  simplehost - Protected local HTTP web server & uploader (~/simplehost_www)\n");
    printf("  watchcmd   - Command loop runner/monitor\n");
    printf("  strutils   - String case converter, URL encoder, ROT13, and stats\n");
    printf("  fdup       - Fast duplicate file finder using multithreaded SHA-256\n");
    printf("  deview     - TUI Nano-style read-only inspector (Hexadecimal, Binary, Decoded)\n");
    printf("  cpuplot    - Animated live terminal dashboard for CPU, RAM, SWAP & Net\n");
    printf("  qrcli      - ISO standard QR Code generator with Wi-Fi shortcuts\n");
    printf("  bench      - Hardware micro-benchmark for CPU MOPs and RAM MB/s\n");
    printf("  get-info   - Hardware intelligence for phones, PC parts & bottleneck shell\n");
    printf("  netclip    - P2P local network clipboard & file transfer (Termux <-> PC)\n");
    printf("  snc        - IPv4 Subnet Calculator & CIDR analyzer with detailed binary table\n");
    printf("  jsonview   - JSON syntax highlighter, validator & shorthand converter\n");
    printf("  speedtest  - Internet speed (Mbps download) & latency/jitter test via CDN\n");
    printf("  utils-help - Display help guide for commands\n");
    printf("======================\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_general_help();
        return 0;
    }

    printf("Displaying help for: %s\n", argv[1]);
    print_general_help();
    return 0;
}
