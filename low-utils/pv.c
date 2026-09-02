#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include "low.h"

#define BUFFER_SIZE 65536
#define COLOR_RESET "\033[0m"
#define COLOR_PROG  "\033[1;33m"
#define COLOR_SPEED "\033[1;32m"
#define COLOR_BYTES "\033[1;36m"
#define COLOR_TIME  "\033[1;35m"

static double get_time_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static void print_help(void) {
    low_print_banner("pv");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./pv [OPTIONS] [FILE...]\n");
    printf("  cat file | ./pv [OPTIONS] | gzip > out.gz\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Monitor throughput, speed (MB/s), ETA, and progress bar of pipe pipelines.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-s, --size <SIZE>%s    Set estimated total data size (e.g., 500M, 2G, 100K)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-q, --quiet%s          Do not display status to stderr\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s           Display this formatted help guide and exit\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --version%s        Display version and repository information\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %scat /dev/urandom | ./pv -s 50M | head -c 52428800 > /dev/null%s\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./pv arquivo_grande.iso > /dev/null%s\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

static off_t parse_size_unit(const char *str) {
    char *endptr;
    double val = strtod(str, &endptr);
    if (*endptr == 'k' || *endptr == 'K') return (off_t)(val * 1024);
    if (*endptr == 'm' || *endptr == 'M') return (off_t)(val * 1024 * 1024);
    if (*endptr == 'g' || *endptr == 'G') return (off_t)(val * 1024 * 1024 * 1024);
    return (off_t)val;
}

static void format_bytes(unsigned long long bytes, char *buf, size_t sz) {
    const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    int i = 0;
    double d = (double)bytes;
    while (d >= 1024.0 && i < 4) {
        d /= 1024.0;
        i++;
    }
    if (i == 0) snprintf(buf, sz, "%llu B", bytes);
    else snprintf(buf, sz, "%.2f %s", d, units[i]);
}

static void format_duration(int secs, char *buf, size_t sz) {
    int h = secs / 3600;
    int m = (secs % 3600) / 60;
    int s = secs % 60;
    if (h > 0) snprintf(buf, sz, "%02d:%02d:%02d", h, m, s);
    else snprintf(buf, sz, "%02d:%02d", m, s);
}

static int pipe_fd(int fd_in, off_t total_size, int quiet) {
    char buffer[BUFFER_SIZE];
    unsigned long long total_bytes = 0;
    double t_start = get_time_sec();
    double last_draw = 0;

    ssize_t n_read = 0;
    while ((n_read = read(fd_in, buffer, sizeof(buffer))) > 0) {
        ssize_t written = 0;
        while (written < n_read) {
            ssize_t w = write(STDOUT_FILENO, buffer + written, n_read - written);
            if (w < 0) return -1;
            written += w;
        }
        total_bytes += n_read;

        double now = get_time_sec();
        if (!quiet && (now - last_draw >= 0.1 || n_read == 0)) {
            last_draw = now;
            double elapsed = now - t_start;
            double speed = (elapsed > 0.05) ? ((double)total_bytes / (1024.0 * 1024.0 * elapsed)) : 0.0;

            char b_str[32], t_str[32];
            format_bytes(total_bytes, b_str, sizeof(b_str));
            format_duration((int)elapsed, t_str, sizeof(t_str));

            if (total_size > 0) {
                int pct = (int)(((double)total_bytes / (double)total_size) * 100.0);
                if (pct > 100) pct = 100;

                int eta_sec = (speed > 0) ? (int)((total_size - total_bytes) / (speed * 1024.0 * 1024.0)) : 0;
                char eta_str[32];
                format_duration(eta_sec, eta_str, sizeof(eta_str));

                int bar_w = 15;
                int filled = (pct * bar_w) / 100;

                fprintf(stderr, "\r%s%s%s %s%s%s [%s%.2f MB/s%s] %s[",
                        COLOR_BYTES, b_str, COLOR_RESET,
                        COLOR_TIME, t_str, COLOR_RESET,
                        COLOR_SPEED, speed, COLOR_RESET,
                        COLOR_PROG);

                for (int i = 0; i < bar_w; i++) {
                    if (i < filled) fprintf(stderr, "=");
                    else if (i == filled) fprintf(stderr, ">");
                    else fprintf(stderr, " ");
                }

                fprintf(stderr, "] %3d%% (ETA %s)%s   ", pct, eta_str, COLOR_RESET);
            } else {
                fprintf(stderr, "\r%s%s%s %s%s%s [%s%.2f MB/s%s]   ",
                        COLOR_BYTES, b_str, COLOR_RESET,
                        COLOR_TIME, t_str, COLOR_RESET,
                        COLOR_SPEED, speed, COLOR_RESET);
            }
            fflush(stderr);
        }
    }

    if (!quiet) {
        double elapsed = get_time_sec() - t_start;
        double speed = (elapsed > 0) ? ((double)total_bytes / (1024.0 * 1024.0 * elapsed)) : 0.0;
        char b_str[32], t_str[32];
        format_bytes(total_bytes, b_str, sizeof(b_str));
        format_duration((int)elapsed, t_str, sizeof(t_str));
        fprintf(stderr, "\r%s%s%s em %s%s%s (Media: %s%.2f MB/s%s) [Concluido]\n",
                COLOR_BYTES, b_str, COLOR_RESET,
                COLOR_TIME, t_str, COLOR_RESET,
                COLOR_SPEED, speed, COLOR_RESET);
        fflush(stderr);
    }

    return (n_read < 0) ? -1 : 0;
}

int main(int argc, char *argv[]) {
    off_t total_size = 0;
    int quiet = 0;
    const char *files[256];
    int file_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            print_help();
            return 0;
        }

        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--size") == 0) {
            if (i + 1 < argc) total_size = parse_size_unit(argv[++i]);
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            quiet = 1;
        } else {
            if (file_count < 256) files[file_count++] = argv[i];
        }
    }

    if (file_count == 0) {
        // Se foi executado direto no terminal sem nenhum Pipe (|), exibe a ajuda e encerra na hora!
        if (isatty(STDIN_FILENO)) {
            print_help();
            return 0;
        }
        return pipe_fd(STDIN_FILENO, total_size, quiet);
    }

    int has_err = 0;
    for (int i = 0; i < file_count; i++) {
        int fd = open(files[i], O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "pv: %s: %s\n", files[i], strerror(errno));
            has_err = 1;
            continue;
        }

        struct stat st;
        if (total_size == 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
            total_size = st.st_size;
        }

        if (pipe_fd(fd, total_size, quiet) < 0) has_err = 1;
        close(fd);
        total_size = 0;
    }

    return has_err ? 1 : 0;
}
