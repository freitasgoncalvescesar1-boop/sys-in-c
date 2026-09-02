#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_OFFSET  "\033[1;33m"
#define COLOR_NULL    "\033[0;90m" // Cinza escuro para 0x00
#define COLOR_ASCII   "\033[1;32m" // Verde para ASCII legível
#define COLOR_CTRL    "\033[1;33m" // Amarelo para controle (\r, \n, \t)
#define COLOR_BIN     "\033[1;36m" // Ciano para bytes binários
#define COLOR_ERR     "\033[1;31m"

static void print_help(void) {
    low_print_banner("xxd");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./xxd [OPTIONS] [FILE...]\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Semantic colored 16-column hexadecimal and ASCII byte inspector with seek and limits.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-s, --seek <OFFSET>%s   Start reading at byte OFFSET (supports dec or hex: 0x100)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-l, --length <LEN>%s    Stop after reading LEN bytes\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-c, --cols <COLS>%s     Format COLS octets per line [Default: 16]\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s            Display this formatted help guide and exit\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --version%s         Display version and repository information\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sSEMANTIC COLOR HIGHLIGHTING:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s00%s (Cinza)   -> Null bytes (blocos vazios)\n", COLOR_NULL, COLOR_RESET);
    printf("  • %sASCII%s (Verde) -> Caracteres de texto legiveis\n", COLOR_ASCII, COLOR_RESET);
    printf("  • %sCtrl%s (Amarelo) -> Caracteres de controle (CR, LF, TAB)\n", COLOR_CTRL, COLOR_RESET);
    printf("  • %sBin%s (Ciano)   -> Bytes de alta entropia / instrucoes de maquina\n\n", COLOR_BIN, COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./xxd -l 64 /bin/ls%s            (Inspeciona os primeiros 64 bytes)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./xxd -s 0x1000 firmware.bin%s   (Pula para o offset 0x1000)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

static off_t parse_offset(const char *str) {
    if (str[0] == '+') str++;
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        return (off_t)strtoull(str, NULL, 16);
    }
    return (off_t)strtoull(str, NULL, 10);
}

static void dump_fd(int fd, const char *name, off_t seek_offset, size_t max_len, int cols) {
    if (seek_offset > 0) {
        lseek(fd, seek_offset, SEEK_SET);
    }

    unsigned char buffer[64];
    size_t offset = seek_offset;
    ssize_t n = 0;
    size_t total_read = 0;

    if (cols <= 0 || cols > 32) cols = 16;

    while (1) {
        size_t to_read = (size_t)cols;
        if (max_len > 0 && total_read + to_read > max_len) {
            to_read = max_len - total_read;
            if (to_read == 0) break;
        }

        n = read(fd, buffer, to_read);
        if (n <= 0) break;

        printf("%s%08zx:%s ", COLOR_OFFSET, offset, COLOR_RESET);

        for (int i = 0; i < cols; i++) {
            if (i < n) {
                unsigned char c = buffer[i];
                if (c == 0x00) printf("%s%02x%s", COLOR_NULL, c, COLOR_RESET);
                else if (c >= 32 && c <= 126) printf("%s%02x%s", COLOR_ASCII, c, COLOR_RESET);
                else if (c == '\r' || c == '\n' || c == '\t') printf("%s%02x%s", COLOR_CTRL, c, COLOR_RESET);
                else printf("%s%02x%s", COLOR_BIN, c, COLOR_RESET);
            } else {
                printf("  ");
            }
            if (i % 2 == 1) printf(" ");
        }
        printf(" ");

        printf("%s|", COLOR_RESET);
        for (int i = 0; i < n; i++) {
            unsigned char c = buffer[i];
            if (c >= 32 && c <= 126) printf("%s%c%s", COLOR_ASCII, c, COLOR_RESET);
            else printf("%s.%s", COLOR_NULL, COLOR_RESET);
        }
        printf("|\n");

        offset += n;
        total_read += n;
    }

    if (n < 0) {
        fprintf(stderr, "%s[ERRO]%s Falha ao ler '%s': %s\n", COLOR_ERR, COLOR_RESET, name, strerror(errno));
    }
}

int main(int argc, char *argv[]) {
    off_t seek_offset = 0;
    size_t max_len = 0;
    int cols = 16;
    const char *files[256];
    int file_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            print_help();
            return 0;
        }

        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--seek") == 0) {
            if (i + 1 < argc) seek_offset = parse_offset(argv[++i]);
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--length") == 0) {
            if (i + 1 < argc) max_len = (size_t)parse_offset(argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--cols") == 0) {
            if (i + 1 < argc) cols = atoi(argv[++i]);
        } else {
            if (file_count < 256) files[file_count++] = argv[i];
        }
    }

    if (file_count == 0) {
        dump_fd(STDIN_FILENO, "stdin", seek_offset, max_len, cols);
        return 0;
    }

    for (int i = 0; i < file_count; i++) {
        if (strcmp(files[i], "-") == 0) {
            dump_fd(STDIN_FILENO, "stdin", seek_offset, max_len, cols);
        } else {
            int fd = open(files[i], O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "%s[ERRO]%s %s: %s\n", COLOR_ERR, COLOR_RESET, files[i], strerror(errno));
                continue;
            }
            dump_fd(fd, files[i], seek_offset, max_len, cols);
            close(fd);
        }
    }
    return 0;
}
