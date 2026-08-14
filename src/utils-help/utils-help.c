#include <stdio.h>
#include <string.h>

static void print_general_help(void) {
    printf("======================\n");
    printf("[utils-in-c - Suite Overview]\n");
    printf("======================\n");
    printf("  calc       - Math expression evaluator (PEMDAS, sin, cos, log, abs, REPL)\n");
    printf("  passgen    - Secure random password generator\n");
    printf("  bigfiles   - Find large files sorted by size with ANSI colors\n");
    printf("  portcheck  - Network host and port connection tester\n");
    printf("  hashcalc   - Fast CRC32, FNV-1a, and SHA-256 hash calculator\n");
    printf("  b64        - Fast Base64 encoder and decoder\n");
    printf("  sysinfo    - Minimal system info monitor (Android/Linux auto-detect)\n");
    printf("  org        - File organizer (move by ext, change ext, auto-group)\n");
    printf("  netinfo    - Network interfaces, Public IP, and port test (-p)\n");
    printf("  ffind      - Fast recursive file finder (ignores system dirs, depth 15)\n");
    printf("  ipcmon     - Live/snapshot IPC shared memory monitor\n");
    printf("  simplehost - Simple protected local HTTP web server (~/simplehost_www)\n");
    printf("  watchcmd   - Command loop runner/monitor\n");
    printf("  strutils   - String case converter, URL encoder, ROT13, and stats\n");
    printf("  fdup       - Fast duplicate file finder using 2-stage size & SHA256 hashing\n");
    printf("  deview     - TUI Nano-style read-only inspector (Hexadecimal, Binary, Decoded)\n");
    printf("  cpuplot    - Animated live terminal graph for CPU and RAM load\n");
    printf("  qrcli      - ASCII QR Code generator for URLs, strings, and Wi-Fi\n");
    printf("  bench      - Hardware micro-benchmark for CPU MOPs and RAM MB/s\n");
    printf("  get-info   - Hardware intelligence for phones, PC parts & bottleneck shell\n");
    printf("======================\n");
    printf("Type 'utils-help <command>' for detailed help on a command.\n");
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
