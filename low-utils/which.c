#include "low.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int is_executable(const char *path) {
    return access(path, X_OK) == 0;
}

static void print_help(const char *program) {
    low_print_banner("which");
    printf("Usage: %s [OPTION]... COMMAND [COMMAND ...]\n\n", program);
    printf("Locate executable commands by searching PATH.\n\n");
    printf("Options:\n");
    printf("  -a, --all       Print all matching executables in PATH\n");
    printf("  -c, --color     Print results with ANSI colors\n");
    printf("  -s, --silent    Do not print results; use exit status only\n");
    printf("  --help          Show this help message\n\n");
    printf("Combined Flags Examples:\n");
    printf("  $ %s -ac gcc sh\n", program);
    printf("  $ %s -as python3\n\n", program);
}

static void print_result(const char *path, int color, int silent) {
    if (silent) return;
    if (color) printf("\033[32m%s\033[0m\n", path);
    else puts(path);
}

static void print_not_found(const char *program, const char *command, int color, int silent) {
    if (silent) return;
    if (color) fprintf(stderr, "%s: \033[31m%s\033[0m not found\n", program, command);
    else fprintf(stderr, "%s: %s not found\n", program, command);
}

int main(int argc, char **argv) {
    int color = 0;
    int all = 0;
    int silent = 0;
    int first_command = 1;

    if (argc < 2) {
        print_help(argv[0]);
        return 2;
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--all") == 0) { all = 1; first_command++; continue; }
        if (strcmp(argv[i], "--color") == 0) { color = 1; first_command++; continue; }
        if (strcmp(argv[i], "--silent") == 0) { silent = 1; first_command++; continue; }

        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            size_t flen = strlen(argv[i]);
            for (size_t j = 1; j < flen; j++) {
                char opt = argv[i][j];
                if (opt == 'a') all = 1;
                else if (opt == 'c') color = 1;
                else if (opt == 's') silent = 1;
                else if (opt == 'h') { print_help(argv[0]); return 0; }
                else {
                    fprintf(stderr, "%s: unknown option '-%c'\n", argv[0], opt);
                    return 2;
                }
            }
            first_command++;
            continue;
        }
        break;
    }

    if (first_command >= argc) {
        print_help(argv[0]);
        return 2;
    }

    const char *path_env = getenv("PATH");
    int found_any = 0;

    for (int arg = first_command; arg < argc; ++arg) {
        const char *command = argv[arg];
        int found = 0;

        if (strchr(command, '/') != NULL) {
            if (is_executable(command)) {
                print_result(command, color, silent);
                found_any = 1;
            } else {
                print_not_found(argv[0], command, color, silent);
            }
            continue;
        }

        if (path_env == NULL) {
            print_not_found(argv[0], command, color, silent);
            continue;
        }

        char *path_copy = strdup(path_env);
        if (path_copy == NULL) return 1;

        char *saveptr = NULL;
        for (char *dir = strtok_r(path_copy, ":", &saveptr);
             dir != NULL;
             dir = strtok_r(NULL, ":", &saveptr)) {
            const char *base = (*dir == '\0') ? "." : dir;
            size_t needed = strlen(base) + 1 + strlen(command) + 1;
            char *candidate = malloc(needed);
            if (candidate == NULL) { free(path_copy); return 1; }

            snprintf(candidate, needed, "%s/%s", base, command);
            if (is_executable(candidate)) {
                print_result(candidate, color, silent);
                found = 1;
                found_any = 1;
                free(candidate);
                if (!all) break;
                continue;
            }
            free(candidate);
        }

        free(path_copy);
        if (!found) print_not_found(argv[0], command, color, silent);
    }

    return found_any ? 0 : 1;
}
