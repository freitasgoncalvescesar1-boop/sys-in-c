#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_VAL     "\033[1;32m"

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void run_cpu_bench(void) {
    printf("  [1/2] Running CPU Integer & Math Benchmark...\n");
    fflush(stdout);

    double start = get_time_sec();
    volatile unsigned long long count = 0;
    double duration = 2.0;

    while (get_time_sec() - start < duration) {
        for (int i = 0; i < 1000000; i++) {
            count += (i * 3) ^ (i >> 2);
        }
    }

    double elapsed = get_time_sec() - start;
    double mops = (double)count / (elapsed * 1e6);

    printf("    • Operations Completed : %llu\n", count);
    printf("    • Elapsed Time         : %.3f seconds\n", elapsed);
    printf("    • CPU Performance      : %s%.2f MOPs/sec%s\n\n", COLOR_VAL, mops, COLOR_RESET);
}

static void run_mem_bench(void) {
    printf("  [2/2] Running RAM / Storage Throughput Benchmark...\n");
    fflush(stdout);

    size_t buf_size = 32 * 1024 * 1024;
    unsigned char *buf = malloc(buf_size);
    if (!buf) {
        printf("    • Memory allocation failed.\n");
        return;
    }

    double start = get_time_sec();
    int passes = 10;

    for (int p = 0; p < passes; p++) {
        memset(buf, (p & 0xFF), buf_size);
        volatile unsigned long sum = 0;
        for (size_t i = 0; i < buf_size; i += 64) {
            sum += buf[i];
        }
    }

    double elapsed = get_time_sec() - start;
    double total_mb = (double)(buf_size * passes) / (1024.0 * 1024.0);
    double mbps = total_mb / elapsed;

    printf("    • Memory Processed     : %.1f MB\n", total_mb);
    printf("    • Elapsed Time         : %.3f seconds\n", elapsed);
    printf("    • RAM/Bus Throughput   : %s%.2f MB/sec%s\n", COLOR_VAL, mbps, COLOR_RESET);

    free(buf);
}

int main(void) {
    utilipc_init();

    /* Output redirection hook - Redirect output here */
    /* Generic output stream */
    printf("%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ bench - Hardware Micro-Benchmark ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);

    run_cpu_bench();
    run_mem_bench();

    printf("%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "bench: completed hardware benchmark");
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
