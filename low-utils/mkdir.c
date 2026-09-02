#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET "\033[0m"
#define COLOR_OK    "\033[1;32m"
#define COLOR_ERR   "\033[1;31m"
#define COLOR_DIR   "\033[1;34m"
#define COLOR_TAG   "\033[1;33m"

static int opt_parents = 0;
static int opt_verbose = 0;
static mode_t custom_mode = 0777;
static int has_custom_mode = 0;

static void print_help(void) {
    low_print_banner("mkdir");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./mkdir [OPTIONS] <DIRECTORY...>\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Create directories with parent hierarchy creation (-p), mode setting (-m), and verbose logs.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-p, --parents%s        Make parent directories as needed, no error if existing\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-m, --mode <MODE>%s    Set file mode (permissions octal like 755 or 700)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --verbose%s        Print a message for each created directory\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s           Display this formatted help guide and exit\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --version%s        Display version and repository information\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./mkdir -p projeto/src/modulos%s          (Cria toda a arvore de pastas)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./mkdir -m 700 pasta_privada%s            (Cria com permissao restrita rwx------)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./mkdir -pv ~/backups/2026/08%s           (Cria com verbose e expansao de ~)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

static void expand_tilde(const char *in, char *out, size_t out_len) {
    if (in[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) home = ".";
        if (in[1] == '/' || in[1] == '\0') {
            snprintf(out, out_len, "%s%s", home, in + 1);
            return;
        }
    }
    strncpy(out, in, out_len - 1);
    out[out_len - 1] = '\0';
}

static int make_parents(const char *path, mode_t mode, int verbose) {
    char temp[2048];
    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    size_t len = strlen(temp);
    while (len > 1 && temp[len - 1] == '/') {
        temp[--len] = '\0';
    }

    for (char *p = temp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            struct stat st;
            if (stat(temp, &st) != 0) {
                if (mkdir(temp, 0777) != 0 && errno != EEXIST) {
                    fprintf(stderr, "  %s[ERRO]%s falha ao criar diretorio pai '%s': %s\n", COLOR_ERR, COLOR_RESET, temp, strerror(errno));
                    return -1;
                }
                if (verbose) {
                    printf("  %s[OK]%s   Diretorio pai criado: %s%s/%s\n", COLOR_OK, COLOR_RESET, COLOR_DIR, temp, COLOR_RESET);
                }
            } else if (!S_ISDIR(st.st_mode)) {
                fprintf(stderr, "  %s[ERRO]%s '%s' existe e nao e um diretorio\n", COLOR_ERR, COLOR_RESET, temp);
                return -1;
            }
            *p = '/';
        }
    }

    struct stat st;
    if (stat(temp, &st) != 0) {
        if (mkdir(temp, mode) != 0 && errno != EEXIST) {
            fprintf(stderr, "  %s[ERRO]%s falha ao criar '%s': %s\n", COLOR_ERR, COLOR_RESET, temp, strerror(errno));
            return -1;
        }
        if (has_custom_mode) {
            chmod(temp, mode);
        }
        if (verbose) {
            printf("  %s[OK]%s   Diretorio criado:     %s%s/%s\n", COLOR_OK, COLOR_RESET, COLOR_DIR, temp, COLOR_RESET);
        }
    } else if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "  %s[ERRO]%s '%s' existe e nao e um diretorio\n", COLOR_ERR, COLOR_RESET, temp);
        return -1;
    }
    return 0;
}

static int make_single_dir(const char *path, mode_t mode, int verbose) {
    if (mkdir(path, mode) != 0) {
        fprintf(stderr, "  %s[ERRO]%s falha ao criar '%s': %s\n", COLOR_ERR, COLOR_RESET, path, strerror(errno));
        return -1;
    }
    if (has_custom_mode) {
        chmod(path, mode);
    }
    if (verbose) {
        printf("  %s[OK]%s   Diretorio criado: %s%s/%s\n", COLOR_OK, COLOR_RESET, COLOR_DIR, path, COLOR_RESET);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    const char *dirs[256];
    int dir_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--version") == 0) {
            print_help();
            return 0;
        }

        if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--parents") == 0) {
            opt_parents = 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            opt_verbose = 1;
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--mode") == 0) {
            if (i + 1 < argc) {
                const char *m_str = argv[++i];
                custom_mode = (mode_t)strtol(m_str, NULL, 8);
                has_custom_mode = 1;
            }
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (size_t j = 1; j < strlen(argv[i]); j++) {
                if (argv[i][j] == 'p') opt_parents = 1;
                else if (argv[i][j] == 'v') opt_verbose = 1;
                else if (argv[i][j] == 'm' && i + 1 < argc) {
                    custom_mode = (mode_t)strtol(argv[++i], NULL, 8);
                    has_custom_mode = 1;
                    break;
                } else {
                    fprintf(stderr, "mkdir: opcao desconhecida '-%c'\n", argv[i][j]);
                    return 1;
                }
            }
        } else {
            if (dir_count < 256) dirs[dir_count++] = argv[i];
        }
    }

    if (dir_count == 0) {
        print_help();
        return 1;
    }

    int has_errors = 0;
    for (int i = 0; i < dir_count; i++) {
        char exp_path[2048];
        expand_tilde(dirs[i], exp_path, sizeof(exp_path));

        if (opt_parents) {
            if (make_parents(exp_path, custom_mode, opt_verbose) < 0) has_errors = 1;
        } else {
            if (make_single_dir(exp_path, custom_mode, opt_verbose) < 0) has_errors = 1;
        }
    }

    return has_errors ? 1 : 0;
}
