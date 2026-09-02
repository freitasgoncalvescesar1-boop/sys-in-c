#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <strings.h>
#include <dirent.h>
#include "low.h"

#define BUFFER_SIZE 65536

#define COLOR_RESET   "\033[0m"
#define COLOR_NUM     "\033[0;90m"
#define COLOR_END     "\033[1;35m"
#define COLOR_SIZE    "\033[1;36m"
#define COLOR_BORDER  "\033[1;34m"

static void get_term_size(int *cols, int *rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    } else {
        *cols = 80;
        *rows = 24;
    }
}

static void format_bytes_human(off_t bytes, char *buf, size_t sz) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    double d = (double)bytes;
    while (d >= 1024.0 && i < 4) {
        d /= 1024.0;
        i++;
    }
    if (i == 0) snprintf(buf, sz, "%lld B", (long long)bytes);
    else snprintf(buf, sz, "%.2f %s", d, units[i]);
}

static void print_help(void) {
    low_print_banner("cat");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./cat [OPTIONS] [FILE...]\n");
    printf("  cat file.txt | ./cat [OPTIONS]\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  High-performance byte stream concatenator with line numbering and extension inspector.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-n, --number%s          Number all output lines\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-b, --number-nonblank%s Number non-empty output lines\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-E, --show-ends%s       Display '$' at the end of each line\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-s, --squeeze-blank%s   Suppress repeated empty output lines\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-z, --size%s            Show formatted file size badges and fit line limits\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-sz%s                   Squeeze empty lines + display formatted size badges\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-l, --list <NAME>%s     List files matching a name fragment\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s            Display this formatted help guide and exit\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --version%s         Display version and repository information\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./cat -n arquivo.c%s             (Exibe codigo com numeros de linha)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./cat -sz texto.txt%s            (Comprime linhas vazias com badges de tamanho)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./cat -s -E texto.txt%s          (Comprime linhas vazias e exibe '$')\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./cat imagem.png%s               (Chama automaticamente o xxd por extensao)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./cat -l c%s                     (Lista arquivos cujo nome contem 'c')\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

static const char *binary_extensions[] = {
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".ico",
    ".bin", ".exe", ".so", ".o", ".dll", ".dylib", ".elf",
    ".pdf", ".zip", ".tar", ".gz", ".7z", ".bz2", ".xz",
    ".mp3", ".mp4", ".mkv", ".avi", ".wav", ".flac",
    ".iso", ".img", ".dat", ".class", ".pyc", NULL
};

static int is_binary_by_extension(const char *path) {
    size_t path_len = strlen(path);
    for (int i = 0; binary_extensions[i] != NULL; i++) {
        size_t ext_len = strlen(binary_extensions[i]);
        if (path_len >= ext_len) {
            if (strcasecmp(path + path_len - ext_len, binary_extensions[i]) == 0) return 1;
        }
    }
    return 0;
}

static int run_xxd(const char *filepath) {
    printf("\033[1;33m[cat: Extensao binaria detectada ('%s'). Redirecionando para o xxd...]\033[0m\n\n", filepath);
    pid_t pid = fork();
    if (pid == 0) {
        execl("./xxd", "xxd", filepath, NULL);
        execl("low-utils/xxd", "xxd", filepath, NULL);
        execlp("xxd", "xxd", filepath, NULL);
        _exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return WEXITSTATUS(status);
    }
    return -1;
}

static int cat_fast_fd(int fd, const char *filename) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read, bytes_written, total_written;
    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        total_written = 0;
        while (total_written < bytes_read) {
            bytes_written = write(STDOUT_FILENO, buffer + total_written, bytes_read - total_written);
            if (bytes_written < 0) {
                fprintf(stderr, "cat: erro de escrita em %s: %s\n", filename, strerror(errno));
                return -1;
            }
            total_written += bytes_written;
        }
    }
    return (bytes_read < 0) ? -1 : 0;
}

static int cat_formatted_fd(int fd, const char *display_name, int opt_n, int opt_b, int opt_e, int opt_s, int opt_z) {
    char buffer[BUFFER_SIZE];
    ssize_t n = 0;
    int line_num = 1;
    int at_line_start = 1;
    int consecutive_empty = 0;
    int term_cols = 80, term_rows = 24;
    get_term_size(&term_cols, &term_rows);

    if (opt_z && display_name && strcmp(display_name, "-") != 0) {
        struct stat st;
        if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
            char sz_buf[32];
            format_bytes_human(st.st_size, sz_buf, sizeof(sz_buf));
            printf("%s┌── [ %s%s%s ] [%s%s%s]%s\n",
                   COLOR_BORDER, LOW_COLOR_BIN, display_name, COLOR_BORDER,
                   COLOR_SIZE, sz_buf, COLOR_BORDER, COLOR_RESET);
        }
    }

    int current_line_chars = 0;
    int max_line_width = term_cols > 10 ? term_cols - 2 : 78;

    while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            char c = buffer[i];

            if (at_line_start) {
                if (c == '\n') {
                    consecutive_empty++;
                    if (opt_s && consecutive_empty > 1) continue;
                    if (opt_n && !opt_b) {
                        printf("%s%6d%s  ", COLOR_NUM, line_num++, COLOR_RESET);
                        current_line_chars += 8;
                    }
                } else {
                    consecutive_empty = 0;
                    if (opt_n || opt_b) {
                        printf("%s%6d%s  ", COLOR_NUM, line_num++, COLOR_RESET);
                        current_line_chars += 8;
                    }
                }
                at_line_start = 0;
            }

            if (c == '\n') {
                if (opt_e) printf("%s$%s", COLOR_END, COLOR_RESET);
                putchar('\n');
                at_line_start = 1;
                current_line_chars = 0;
            } else {
                if (opt_z && current_line_chars >= max_line_width) {
                    putchar('\n');
                    if (opt_n && !opt_b) {
                        printf("%s%6s%s  ", COLOR_NUM, "->", COLOR_RESET);
                        current_line_chars = 8;
                    } else {
                        current_line_chars = 0;
                    }
                }
                putchar(c);
                current_line_chars++;
            }
        }
    }

    if (opt_z && display_name && strcmp(display_name, "-") != 0) {
        printf("%s└── [ Fim do arquivo: %s ]%s\n", COLOR_BORDER, display_name, COLOR_RESET);
    }

    return (n < 0) ? -1 : 0;
}

static int list_files(const char *fragment) {
    DIR *dir = opendir(".");
    if (dir == NULL) {
        fprintf(stderr, "cat: cannot open current directory: %s\n", strerror(errno));
        return 1;
    }
    struct dirent *entry;
    int found = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (strstr(entry->d_name, fragment) != NULL) {
            puts(entry->d_name);
            found = 1;
        }
    }
    closedir(dir);
    if (!found) {
        fprintf(stderr, "cat: no files matching '%s' in current directory\n", fragment);
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int opt_n = 0, opt_b = 0, opt_e = 0, opt_s = 0, opt_z = 0;
    const char *files[256];
    int file_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "cat: option '-l' requires a name fragment\n");
                return 1;
            }
            return list_files(argv[++i]);
        }
        if (strcmp(argv[i], "--size") == 0) {
            opt_z = 1;
            continue;
        }
        if (argv[i][0] == '-' && argv[i][1] != '\0' && strcmp(argv[i], "-") != 0) {
            if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--number") == 0) opt_n = 1;
            else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--number-nonblank") == 0) { opt_b = 1; opt_n = 1; }
            else if (strcmp(argv[i], "-E") == 0 || strcmp(argv[i], "--show-ends") == 0) opt_e = 1;
            else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--squeeze-blank") == 0) opt_s = 1;
            else if (strcmp(argv[i], "-z") == 0) opt_z = 1;
            else if (strcmp(argv[i], "-sz") == 0 || strcmp(argv[i], "-zs") == 0) { opt_s = 1; opt_z = 1; }
            else {
                for (size_t j = 1; j < strlen(argv[i]); j++) {
                    if (argv[i][j] == 'n') opt_n = 1;
                    else if (argv[i][j] == 'b') { opt_b = 1; opt_n = 1; }
                    else if (argv[i][j] == 'E') opt_e = 1;
                    else if (argv[i][j] == 's') opt_s = 1;
                    else if (argv[i][j] == 'z') opt_z = 1;
                    else {
                        fprintf(stderr, "cat: opcao desconhecida '-%c'\n", argv[i][j]);
                        return 1;
                    }
                }
            }
        } else if (file_count < 256) files[file_count++] = argv[i];
    }

    int has_formatting = (opt_n || opt_b || opt_e || opt_s || opt_z);
    int has_errors = 0;

    if (file_count == 0) {
        if (isatty(STDIN_FILENO)) {
            print_help();
            return 0;
        }
        if (has_formatting) cat_formatted_fd(STDIN_FILENO, "-", opt_n, opt_b, opt_e, opt_s, opt_z);
        else cat_fast_fd(STDIN_FILENO, "-");
        return 0;
    }

    for (int i = 0; i < file_count; i++) {
        if (strcmp(files[i], "-") == 0) {
            if (has_formatting) cat_formatted_fd(STDIN_FILENO, "-", opt_n, opt_b, opt_e, opt_s, opt_z);
            else cat_fast_fd(STDIN_FILENO, "-");
            continue;
        }
        if (!has_formatting && is_binary_by_extension(files[i])) {
            if (run_xxd(files[i]) == 0) continue;
        }
        int fd = open(files[i], O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "cat: %s: %s\n", files[i], strerror(errno));
            has_errors = 1;
            continue;
        }
        if (has_formatting) {
            if (cat_formatted_fd(fd, files[i], opt_n, opt_b, opt_e, opt_s, opt_z) < 0) has_errors = 1;
        } else if (cat_fast_fd(fd, files[i]) < 0) has_errors = 1;
        close(fd);
    }
    return has_errors ? 1 : 0;
}
