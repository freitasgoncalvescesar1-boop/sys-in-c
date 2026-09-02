#include "low.h"

#include <stdlib.h>
#include <unistd.h>

static int is_executable(const char *path) {
    return access(path, X_OK) == 0;
}

static void print_help(const char *program) {
    printf("Usage: %s [OPTION]... COMMAND [COMMAND ...]\n\n", program);
    printf("Locate executable commands by searching PATH.\n");
    printf("By default, the first executable match for each command is printed.\n\n");
    printf("Options:\n");
    printf("  -a, --all       Print all matching executables in PATH.\n");
    printf("  -c, --color     Print results with ANSI colors.\n");
    printf("  -s, --silent    Do not print results; use the exit status only.\n");
    printf("  --skip-alias    Compatibility option; aliases are not searched.\n");
    printf("  --help          Show this help message.\n");
    printf("  -example        Show a usage example.\n\n");
    printf("Behavior:\n");
    printf("  Multiple commands may be queried at once.\n");
    printf("  A command containing '/' is checked directly instead of searching PATH.\n");
    printf("  Empty PATH components represent the current directory.\n");
    printf("  Exit status is 0 when at least one command is found, otherwise 1.\n");
}

static void print_example(const char *program) {
    printf("Examples:\n");
    printf("  $ %s gcc\n", program);
    printf("  /usr/bin/gcc\n\n");
    printf("  $ %s -a sh\n", program);
    printf("  /usr/bin/sh\n");
    printf("  /bin/sh\n\n");
    printf("  $ %s -c gcc\n", program);
    printf("  $ %s --silent gcc\n", program);
}

static void print_result(const char *path, int color, int silent) {
    if (silent) {
        return;
    }
    if (color) {
        printf("\033[32m%s\033[0m\n", path);
    } else {
        puts(path);
    }
}

static void print_not_found(const char *program, const char *command, int color, int silent) {
    if (silent) {
        return;
    }
    if (color) {
        fprintf(stderr, "%s: \033[31m%s\033[0m not found\n", program, command);
    } else {
        fprintf(stderr, "%s: %s not found\n", program, command);
    }
}

int main(int argc, char **argv) {
    int color = 0;
    int all = 0;
    int silent = 0;
    int first_command = 1;

    if (argc < 2) {
        low_print_banner("which");
        print_help(argv[0]);
        return 2;
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            low_print_banner("which");
            print_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-example") == 0) {
            low_print_banner("which");
            print_example(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--color") == 0) {
            color = 1;
            first_command++;
            continue;
        }
        if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0) {
            all = 1;
            first_command++;
            continue;
        }
        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--silent") == 0) {
            silent = 1;
            first_command++;
            continue;
        }
        if (strcmp(argv[i], "--skip-alias") == 0) {
            first_command++;
            continue;
        }
        if (argv[i][0] == '-' && strcmp(argv[i], "-") != 0) {
            fprintf(stderr, "%s: unknown option '%s'\n", argv[0], argv[i]);
            return 2;
        }
        break;
    }

    if (first_command >= argc) {
        low_print_banner("which");
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
        if (path_copy == NULL) {
            perror("strdup");
            return 1;
        }

        char *saveptr = NULL;
        for (char *dir = strtok_r(path_copy, ":", &saveptr);
             dir != NULL;
             dir = strtok_r(NULL, ":", &saveptr)) {
            const char *base = (*dir == '\0') ? "." : dir;
            size_t needed = strlen(base) + 1 + strlen(command) + 1;
            char *candidate = malloc(needed);

            if (candidate == NULL) {
                free(path_copy);
                perror("malloc");
                return 1;
            }

            snprintf(candidate, needed, "%s/%s", base, command);
            if (is_executable(candidate)) {
                print_result(candidate, color, silent);
                found = 1;
                found_any = 1;
                free(candidate);
                if (!all) {
                    break;
                }
                continue;
            }
            free(candidate);
        }

        free(path_copy);

        if (!found) {
            print_not_found(argv[0], command, color, silent);
        }
    }

    return found_any ? 0 : 1;
}
