#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_FILE    "\033[1;36m"
#define COLOR_PERM    "\033[1;33m"
#define COLOR_DRY     "\033[1;35m"

static void print_help(void) {
    low_print_banner("chmod");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./chmod [OPTIONS] <MODE> <FILE...>\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Modify file permissions with recursion, safety dry-run, and type filters.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-R, --recursive%s      Change files and directories recursively\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-n, --dry-run%s        Simulate and display changes without applying\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s--files-only%s         Only apply permissions to regular files\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s--dirs-only%s          Only apply permissions to directories\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s--no-preserve-root%s   Do not treat '/' specially (dangerous)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s           Display this formatted help guide and exit\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --version%s        Display version and repository information\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./chmod -R 755 ./projeto/%s            (Recursivo em tudo)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./chmod -R --dry-run 777 ./pasta%s     (Simula alteracao sem risco)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./chmod -R --files-only 644 ./docs%s   (Altera apenas arquivos)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./chmod -R --dirs-only 755 ./docs%s    (Altera apenas pastas)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

static void mode_to_str(mode_t mode, char *str) {
    strcpy(str, "---------");
    if (mode & S_IRUSR) str[0] = 'r';
    if (mode & S_IWUSR) str[1] = 'w';
    if (mode & S_IXUSR) str[2] = 'x';
    if (mode & S_IRGRP) str[3] = 'r';
    if (mode & S_IWGRP) str[4] = 'w';
    if (mode & S_IXGRP) str[5] = 'x';
    if (mode & S_IROTH) str[6] = 'r';
    if (mode & S_IWOTH) str[7] = 'w';
    if (mode & S_IXOTH) str[8] = 'x';

    if (mode & S_ISUID) str[2] = (mode & S_IXUSR) ? 's' : 'S';
    if (mode & S_ISGID) str[5] = (mode & S_IXGRP) ? 's' : 'S';
    if (mode & S_ISVTX) str[8] = (mode & S_IXOTH) ? 't' : 'T';
}

static mode_t parse_symbolic_mode(const char *modestr, mode_t current_mode) {
    mode_t new_mode = current_mode;
    int who_mask = 0;
    const char *p = modestr;

    while (*p == 'u' || *p == 'g' || *p == 'o' || *p == 'a') {
        if (*p == 'u') who_mask |= S_IRWXU | S_ISUID;
        if (*p == 'g') who_mask |= S_IRWXG | S_ISGID;
        if (*p == 'o') who_mask |= S_IRWXO | S_ISVTX;
        if (*p == 'a') who_mask |= S_IRWXU | S_IRWXG | S_IRWXO | S_ISUID | S_ISGID | S_ISVTX;
        p++;
    }

    if (who_mask == 0) {
        who_mask = S_IRWXU | S_IRWXG | S_IRWXO | S_ISUID | S_ISGID | S_ISVTX;
    }

    char op = *p++;
    if (op != '+' && op != '-' && op != '=') return (mode_t)-1;

    int perm_mask = 0;
    while (*p) {
        if (*p == 'r') perm_mask |= S_IRUSR | S_IRGRP | S_IROTH;
        else if (*p == 'w') perm_mask |= S_IWUSR | S_IWGRP | S_IWOTH;
        else if (*p == 'x') perm_mask |= S_IXUSR | S_IXGRP | S_IXOTH;
        else if (*p == 's') perm_mask |= S_ISUID | S_ISGID;
        else if (*p == 't') perm_mask |= S_ISVTX;
        else return (mode_t)-1;
        p++;
    }

    int apply_mask = perm_mask & who_mask;

    if (op == '+') {
        new_mode |= apply_mask;
    } else if (op == '-') {
        new_mode &= ~apply_mask;
    } else if (op == '=') {
        new_mode &= ~who_mask;
        new_mode |= apply_mask;
    }

    return new_mode;
}

static int apply_chmod_file(const char *filepath, const char *mode_arg, int is_octal, mode_t parsed_octal, int dry_run, int files_only, int dirs_only) {
    struct stat st;
    if (lstat(filepath, &st) < 0) {
        fprintf(stderr, "  %s[ERRO]%s %s: %s\n", COLOR_ERR, COLOR_RESET, filepath, strerror(errno));
        return -1;
    }

    if (files_only && !S_ISREG(st.st_mode)) return 0;
    if (dirs_only && !S_ISDIR(st.st_mode)) return 0;

    mode_t current_mode = st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO | S_ISUID | S_ISGID | S_ISVTX);
    mode_t new_mode = current_mode;

    if (is_octal) {
        new_mode = parsed_octal;
    } else {
        char mode_copy[256];
        strncpy(mode_copy, mode_arg, sizeof(mode_copy) - 1);
        char *token = strtok(mode_copy, ",");

        while (token != NULL) {
            new_mode = parse_symbolic_mode(token, new_mode);
            if (new_mode == (mode_t)-1) {
                fprintf(stderr, "  %s[ERRO]%s Modo simbolico invalido: '%s'\n", COLOR_ERR, COLOR_RESET, token);
                return -1;
            }
            token = strtok(NULL, ",");
        }
    }

    if (current_mode == new_mode) {
        return 0;
    }

    char old_p[10], new_p[10];
    mode_to_str(current_mode, old_p);
    mode_to_str(new_mode, new_p);

    if (dry_run) {
        printf("  %s[DRY-RUN]%s %s%s%s: %s%s%s -> %s%s%s (%04o -> %04o)\n",
               COLOR_DRY, COLOR_RESET, COLOR_FILE, filepath, COLOR_RESET,
               COLOR_ERR, old_p, COLOR_RESET, COLOR_OK, new_p, COLOR_RESET,
               current_mode, new_mode);
        return 0;
    }

    if (chmod(filepath, new_mode) < 0) {
        fprintf(stderr, "  %s[ERRO]%s %s: %s\n", COLOR_ERR, COLOR_RESET, filepath, strerror(errno));
        return -1;
    }

    printf("  %s[OK]%s      %s%s%s: %s%s%s -> %s%s%s (%04o -> %04o)\n",
           COLOR_OK, COLOR_RESET, COLOR_FILE, filepath, COLOR_RESET,
           COLOR_ERR, old_p, COLOR_RESET, COLOR_OK, new_p, COLOR_RESET,
           current_mode, new_mode);

    return 0;
}

static int chmod_recursive(const char *dir_path, const char *mode_arg, int is_octal, mode_t parsed_octal, int dry_run, int files_only, int dirs_only) {
    apply_chmod_file(dir_path, mode_arg, is_octal, parsed_octal, dry_run, files_only, dirs_only);

    DIR *dir = opendir(dir_path);
    if (!dir) return -1;

    struct dirent *entry;
    char path[2048];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            chmod_recursive(path, mode_arg, is_octal, parsed_octal, dry_run, files_only, dirs_only);
        } else {
            apply_chmod_file(path, mode_arg, is_octal, parsed_octal, dry_run, files_only, dirs_only);
        }
    }

    closedir(dir);
    return 0;
}

int main(int argc, char *argv[]) {
    int recursive = 0, dry_run = 0, files_only = 0, dirs_only = 0, preserve_root = 1;
    const char *mode_arg = NULL;
    const char *targets[256];
    int target_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            print_help();
            return 0;
        }

        if (strcmp(argv[i], "-R") == 0 || strcmp(argv[i], "--recursive") == 0) recursive = 1;
        else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--dry-run") == 0) dry_run = 1;
        else if (strcmp(argv[i], "--files-only") == 0) files_only = 1;
        else if (strcmp(argv[i], "--dirs-only") == 0) dirs_only = 1;
        else if (strcmp(argv[i], "--no-preserve-root") == 0) preserve_root = 0;
        else {
            if (!mode_arg) {
                mode_arg = argv[i];
            } else if (target_count < 256) {
                targets[target_count++] = argv[i];
            }
        }
    }

    if (!mode_arg || target_count == 0) {
        print_help();
        return 1;
    }

    int is_octal = (mode_arg[0] >= '0' && mode_arg[0] <= '7');
    mode_t parsed_octal = is_octal ? (mode_t)strtol(mode_arg, NULL, 8) : 0;

    int has_errors = 0;
    for (int i = 0; i < target_count; i++) {
        if (preserve_root && recursive && strcmp(targets[i], "/") == 0) {
            fprintf(stderr, "  %s[SEGURANCA]%s Modificar a raiz '/' recursivamente foi bloqueado (use --no-preserve-root)\n", COLOR_ERR, COLOR_RESET);
            return 1;
        }

        struct stat st;
        if (lstat(targets[i], &st) == 0 && S_ISDIR(st.st_mode) && recursive) {
            if (chmod_recursive(targets[i], mode_arg, is_octal, parsed_octal, dry_run, files_only, dirs_only) < 0) {
                has_errors = 1;
            }
        } else {
            if (apply_chmod_file(targets[i], mode_arg, is_octal, parsed_octal, dry_run, files_only, dirs_only) < 0) {
                has_errors = 1;
            }
        }
    }

    return has_errors ? 1 : 0;
}
