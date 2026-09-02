#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_VAL     "\033[1;32m"
#define COLOR_TAG     "\033[1;33m"
#define COLOR_LABEL   "\033[1;36m"

static double get_time_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

// Estrutura para os trabalhadores das threads
typedef struct {
    int thread_id;
    double duration;
    unsigned long long operations;
    volatile int *start_flag;
} ThreadBenchArg;

static void *multi_cpu_worker(void *arg) {
    ThreadBenchArg *targ = (ThreadBenchArg *)arg;

    // Aguarda o sinal de largada sincronizado
    while (!*(targ->start_flag)) {
#if defined(__x86_64__) || defined(__i386__)
        __asm__ __volatile__("pause");
#endif
    }

    double start = get_time_sec();
    unsigned long long ops = 0;
    volatile unsigned long long count = 0;
    volatile double fp_count = 0.0;

    while (get_time_sec() - start < targ->duration) {
        unsigned long long local_count = 0;
        double local_fp = 0.0;

        for (int i = 1; i <= 100000; i++) {
            local_count += (i * 3) ^ (i >> 2);
            local_count ^= (i << 1);
            local_fp += ((double)i * 1.000001);
        }

        count += local_count;
        fp_count += local_fp;
        ops += 100000;
    }

    targ->operations = ops;
    return NULL;
}

static double run_single_cpu_bench(void) {
    printf("  %s[1/3] Running CPU Single-Core Benchmark...%s\n", COLOR_LABEL, COLOR_RESET);
    fflush(stdout);

    double start = get_time_sec();
    if (start == 0.0) {
        printf("    • Error: Failed to retrieve system time.\n");
        return 0.0;
    }

    double duration = 2.0;
    unsigned long long operations = 0;
    volatile unsigned long long count = 0;
    volatile double fp_count = 0.0;

    while (get_time_sec() - start < duration) {
        unsigned long long local_count = 0;
        double local_fp = 0.0;

        for (int i = 1; i <= 100000; i++) {
            local_count += (i * 3) ^ (i >> 2);
            local_count ^= (i << 1);
            local_fp += ((double)i * 1.000001);
        }

        count += local_count;
        fp_count += local_fp;
        operations += 100000;
    }

    double elapsed = get_time_sec() - start;
    double mops = (elapsed > 0.0) ? ((double)operations / (elapsed * 1000000.0)) : 0.0;

    printf("    • Operations Completed : %llu\n", operations);
    printf("    • Elapsed Time         : %.3f seconds\n", elapsed);
    printf("    • Single-Core Score    : %s%.2f MOPs/sec%s\n\n", COLOR_VAL, mops, COLOR_RESET);

    return mops;
}

static double run_multi_cpu_bench(long num_cores, double single_mops) {
    printf("  %s[2/3] Running CPU Multi-Core Benchmark (%ld Cores Active)...%s\n", COLOR_LABEL, num_cores, COLOR_RESET);
    fflush(stdout);

    pthread_t *threads = malloc(num_cores * sizeof(pthread_t));
    ThreadBenchArg *args = malloc(num_cores * sizeof(ThreadBenchArg));
    volatile int start_flag = 0;

    double duration = 2.0;

    for (long i = 0; i < num_cores; i++) {
        args[i].thread_id = i;
        args[i].duration = duration;
        args[i].operations = 0;
        args[i].start_flag = &start_flag;
        pthread_create(&threads[i], NULL, multi_cpu_worker, &args[i]);
    }

    // Sinal de largada para todas as threads começarem no mesmo milissegundo
    double start_time = get_time_sec();
    start_flag = 1;

    unsigned long long total_operations = 0;
    for (long i = 0; i < num_cores; i++) {
        pthread_join(threads[i], NULL);
        total_operations += args[i].operations;
    }
    double elapsed = get_time_sec() - start_time;

    double multi_mops = (elapsed > 0.0) ? ((double)total_operations / (elapsed * 1000000.0)) : 0.0;
    double speedup = (single_mops > 0.0) ? (multi_mops / single_mops) : 1.0;
    double efficiency = (num_cores > 0) ? ((speedup / (double)num_cores) * 100.0) : 100.0;

    printf("    • Total Operations     : %llu (across all cores)\n", total_operations);
    printf("    • Elapsed Time         : %.3f seconds\n", elapsed);
    printf("    • Multi-Core Score     : %s%.2f MOPs/sec%s\n", COLOR_VAL, multi_mops, COLOR_RESET);
    printf("    • Multi-Core Speedup   : %s%.2fx%s (%.1f%% efficiency)\n\n", COLOR_TAG, speedup, COLOR_RESET, efficiency);

    free(threads);
    free(args);
    return multi_mops;
}

static double run_mem_bench(void) {
    printf("  %s[3/3] Running RAM / Storage Throughput Benchmark...%s\n", COLOR_LABEL, COLOR_RESET);
    fflush(stdout);

    size_t buf_size = 32 * 1024 * 1024;
    unsigned char *buf = malloc(buf_size);
    unsigned char *buf_copy = malloc(buf_size);

    if (!buf || !buf_copy) {
        printf("    • Error: Memory allocation failed.\n");
        if (buf) free(buf);
        if (buf_copy) free(buf_copy);
        return 0.0;
    }

    double start = get_time_sec();
    if (start == 0.0) {
        printf("    • Error: Failed to retrieve system time.\n");
        free(buf);
        free(buf_copy);
        return 0.0;
    }

    int passes = 10;

    for (int p = 0; p < passes; p++) {
        memset(buf, (p & 0xFF), buf_size);
        memcpy(buf_copy, buf, buf_size);

        volatile unsigned long sum = 0;
        unsigned long local_sum = 0;
        for (size_t i = 0; i < buf_size; i += 64) {
            local_sum += buf_copy[i];
        }
        sum += local_sum;
    }

    double elapsed = get_time_sec() - start;
    double total_mb = (double)(4ULL * buf_size * passes) / (1024.0 * 1024.0);
    double mbps = (elapsed > 0.0) ? (total_mb / elapsed) : 0.0;

    printf("    • Memory Processed     : %.1f MB\n", total_mb);
    printf("    • Elapsed Time         : %.3f seconds\n", elapsed);
    printf("    • RAM/Bus Throughput   : %s%.2f MB/sec%s\n", COLOR_VAL, mbps, COLOR_RESET);

    free(buf);
    free(buf_copy);
    return mbps;
}

int main(void) {
    if (utilipc_init() != 0) {
        fprintf(stderr, "Warning: Failed to initialize IPC. Continuing without logging.\n");
    }

    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cores <= 0) num_cores = 1;

    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ bench - Hardware Micro-Benchmark (Single + Multi-Core) ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("  • Detectados: %s%ld Núcleos de Processamento Ativos%s\n\n", COLOR_VAL, num_cores, COLOR_RESET);

    double single_mops = run_single_cpu_bench();
    double multi_mops = run_multi_cpu_bench(num_cores, single_mops);
    double ram_mbps = run_mem_bench();

    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "bench: Single %.1f MOPs | Multi %.1f MOPs (%ldc) | RAM %.1f MB/s",
             single_mops, multi_mops, num_cores, ram_mbps);
    utilipc_write_status(-1.0, -1.0, -1.0, log_msg);

    utilipc_close();
    return 0;
}
