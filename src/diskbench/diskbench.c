#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_VAL     "\033[1;32m"
#define COLOR_TAG     "\033[1;33m"
#define COLOR_LABEL   "\033[1;36m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_MUTED   "\033[0;90m"

#define TEST_FILE ".diskbench_tmp.bin"
#define SEQ_FILE_SIZE (64 * 1024 * 1024) // 64 MB
#define CHUNK_1MB     (1024 * 1024)
#define RAND_OPS      5000
#define CHUNK_4KB     4096

static double get_time_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static void print_help(void) {
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ diskbench - Storage IOPS & Sequential Throughput ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  diskbench                  (Executa benchmark padrao de 64MB)\n");
    printf("  diskbench --help           (Exibe esta ajuda)\n\n");
    printf("Testes Executados:\n");
    printf("  1. Escrita Sequencial 1MB  (Throughput MB/s com fsync)\n");
    printf("  2. Leitura Sequencial 1MB  (Throughput MB/s)\n");
    printf("  3. Escrita Aleatoria 4KB   (IOPS e Latencia media)\n");
    printf("  4. Leitura Aleatoria 4KB   (IOPS e Latencia media)\n");
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_help();
        utilipc_close();
        return 0;
    }

    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ diskbench - Storage Hardware Benchmark ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s========================================================%s\n\n", COLOR_TITLE, COLOR_RESET);

    // 1. Escrita Sequencial
    printf("  %s[1/4] Testando Escrita Sequencial (64 MB em blocos de 1 MB)...%s\n", COLOR_LABEL, COLOR_RESET);
    fflush(stdout);

    int fd = open(TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        fprintf(stderr, "  %s[ERRO]%s Nao foi possivel criar arquivo temporario: %s\n", COLOR_ERR, COLOR_RESET, strerror(errno));
        utilipc_close();
        return 1;
    }

    char *buf_1mb = malloc(CHUNK_1MB);
    if (!buf_1mb) { close(fd); return 1; }
    memset(buf_1mb, 0xAA, CHUNK_1MB);

    double t_start = get_time_sec();
    for (int i = 0; i < SEQ_FILE_SIZE / CHUNK_1MB; i++) {
        write(fd, buf_1mb, CHUNK_1MB);
    }
    fsync(fd);
    double t_seq_write = get_time_sec() - t_start;
    double seq_w_mbps = (t_seq_write > 0) ? (64.0 / t_seq_write) : 0;
    close(fd);

    printf("    • Velocidade de Escrita : %s%.2f MB/s%s (%.3f s)\n\n", COLOR_VAL, seq_w_mbps, COLOR_RESET, t_seq_write);

    // 2. Leitura Sequencial
    printf("  %s[2/4] Testando Leitura Sequencial (64 MB)...%s\n", COLOR_LABEL, COLOR_RESET);
    fflush(stdout);

    fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0) { free(buf_1mb); return 1; }

    t_start = get_time_sec();
    while (read(fd, buf_1mb, CHUNK_1MB) > 0);
    double t_seq_read = get_time_sec() - t_start;
    double seq_r_mbps = (t_seq_read > 0) ? (64.0 / t_seq_read) : 0;
    close(fd);
    free(buf_1mb);

    printf("    • Velocidade de Leitura : %s%.2f MB/s%s (%.3f s)\n\n", COLOR_VAL, seq_r_mbps, COLOR_RESET, t_seq_read);

    // 3. Escrita Aleatória 4KB (IOPS)
    printf("  %s[3/4] Testando Escrita Aleatória 4KB (%d operações)...%s\n", COLOR_LABEL, RAND_OPS, COLOR_RESET);
    fflush(stdout);

    fd = open(TEST_FILE, O_RDWR);
    if (fd < 0) return 1;

    char buf_4kb[CHUNK_4KB];
    memset(buf_4kb, 0x55, sizeof(buf_4kb));
    srand(time(NULL));

    off_t max_offset = SEQ_FILE_SIZE - CHUNK_4KB;
    t_start = get_time_sec();
    for (int i = 0; i < RAND_OPS; i++) {
        off_t rand_pos = (rand() % (max_offset / CHUNK_4KB)) * CHUNK_4KB;
        lseek(fd, rand_pos, SEEK_SET);
        write(fd, buf_4kb, CHUNK_4KB);
    }
    fsync(fd);
    double t_rand_write = get_time_sec() - t_start;
    double rand_w_iops = (t_rand_write > 0) ? ((double)RAND_OPS / t_rand_write) : 0;
    double rand_w_lat_ms = (rand_w_iops > 0) ? (1000.0 / rand_w_iops) : 0;
    close(fd);

    printf("    • Escrita 4KB (IOPS)    : %s%.1f IOPS%s (Latência: %s%.2f ms%s)\n\n",
           COLOR_VAL, rand_w_iops, COLOR_RESET, COLOR_TAG, rand_w_lat_ms, COLOR_RESET);

    // 4. Leitura Aleatória 4KB (IOPS)
    printf("  %s[4/4] Testando Leitura Aleatória 4KB (%d operações)...%s\n", COLOR_LABEL, RAND_OPS, COLOR_RESET);
    fflush(stdout);

    fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0) return 1;

    t_start = get_time_sec();
    for (int i = 0; i < RAND_OPS; i++) {
        off_t rand_pos = (rand() % (max_offset / CHUNK_4KB)) * CHUNK_4KB;
        lseek(fd, rand_pos, SEEK_SET);
        read(fd, buf_4kb, CHUNK_4KB);
    }
    double t_rand_read = get_time_sec() - t_start;
    double rand_r_iops = (t_rand_read > 0) ? ((double)RAND_OPS / t_rand_read) : 0;
    double rand_r_lat_ms = (rand_r_iops > 0) ? (1000.0 / rand_r_iops) : 0;
    close(fd);

    unlink(TEST_FILE);

    printf("    • Leitura 4KB (IOPS)    : %s%.1f IOPS%s (Latência: %s%.2f ms%s)\n\n",
           COLOR_VAL, rand_r_iops, COLOR_RESET, COLOR_TAG, rand_r_lat_ms, COLOR_RESET);

    printf("%s--------------------------------------------------------%s\n", COLOR_MUTED, COLOR_RESET);
    printf("  %sResumo de Armazenamento:%s\n", COLOR_TITLE, COLOR_RESET);
    printf("    • Seq Read/Write : %s%.1f MB/s%s / %s%.1f MB/s%s\n", COLOR_VAL, seq_r_mbps, COLOR_RESET, COLOR_VAL, seq_w_mbps, COLOR_RESET);
    printf("    • Random 4K IOPS : %s%.0f Read%s / %s%.0f Write%s\n", COLOR_VAL, rand_r_iops, COLOR_RESET, COLOR_VAL, rand_w_iops, COLOR_RESET);
    printf("%s========================================================%s\n\n", COLOR_TITLE, COLOR_RESET);

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "diskbench: Seq W:%.1fMB/s R:%.1fMB/s | 4K: %.0f IOPS", seq_w_mbps, seq_r_mbps, rand_r_iops);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
