#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <sys/syscall.h>
#include <dirent.h>
#include <utime.h>
#include <ctype.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET "\033[0m"
#define COLOR_OK    "\033[1;32m"
#define COLOR_ERR   "\033[1;31m"
#define COLOR_FILE  "\033[1;36m"
#define COLOR_TAG   "\033[1;33m"

#ifndef __NR_copy_file_range
    #if defined(__x86_64__)
        #define __NR_copy_file_range 326
    #elif defined(__i386__)
        #define __NR_copy_file_range 377
    #elif defined(__aarch64__) || defined(__arm__)
        #define __NR_copy_file_range 285
    #endif
#endif

static int opt_force = 0;
static int opt_interactive = 0;
static int opt_no_clobber = 0;
static int opt_update = 0;
static int opt_verbose = 1;
static int opt_backup = 0;
static int opt_no_target_dir = 0;

static void print_help(void) {
    low_print_banner("mv");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./mv [OPTIONS] <SOURCE> <DESTINATION>\n");
    printf("  ./mv [OPTIONS] <SOURCE...> <DIRECTORY>\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Move (rename) files with cross-device fallback, combined flags, and update mode.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-f, --force%s               Do not prompt before overwriting\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-i, --interactive%s         Prompt before overwrite\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-n, --no-clobber%s          Do not overwrite existing files\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-u, --update%s              Move only when SOURCE is newer than DEST or missing\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-b, --backup%s              Make a backup of each existing destination file (~)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --verbose%s             Explain what is being done\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-T, --no-target-directory%s Treat DEST as a normal file\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s                Display this formatted help guide and exit\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sCOMBINED SHORT FLAGS EXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./mv -uv antigo.txt ./build/%s          (Update + Verbose)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./mv -fv arq1.txt ~/Downloads/%s        (Force + Verbose)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

static void expand_tilde(const char *in, char *out, size_t out_len) {
    if (!in) { out[0] = '\0'; return; }
    if (in[0] == '~') {
        const char *home = getenv("HOME");
        if (!home || strlen(home) == 0) home = ".";
        if (in[1] == '/' || in[1] == '\0') {
            snprintf(out, out_len, "%s%s", home, in + 1);
            return;
        }
    }
    strncpy(out, in, out_len - 1);
    out[out_len - 1] = '\0';
}

static inline ssize_t k_copy_file_range(int fd_in, off_t *off_in, int fd_out, off_t *off_out, size_t len, unsigned int flags) {
#if defined(SYS_copy_file_range)
    return syscall(SYS_copy_file_range, fd_in, off_in, fd_out, off_out, len, flags);
#elif defined(__NR_copy_file_range)
    return syscall(__NR_copy_file_range, fd_in, off_in, fd_out, off_out, len, flags);
#else
    (void)fd_in; (void)off_in; (void)fd_out; (void)off_out; (void)len; (void)flags;
    errno = ENOSYS;
    return -1;
#endif
}

static int cross_device_move(const char *src, const char *dest, struct stat *st_src) {
    if (S_ISLNK(st_src->st_mode)) {
        char link_target[1024];
        ssize_t len = readlink(src, link_target, sizeof(link_target) - 1);
        if (len < 0) return -1;
        link_target[len] = '\0';
        unlink(dest);
        if (symlink(link_target, dest) < 0) return -1;
        unlink(src);
        return 0;
    }

    int fd_in = open(src, O_RDONLY);
    if (fd_in < 0) return -1;

    int fd_out = open(dest, O_WRONLY | O_CREAT | O_TRUNC, st_src->st_mode);
    if (fd_out < 0) { close(fd_in); return -1; }

    off_t total = st_src->st_size;
    off_t copied = 0;

    while (copied < total) {
        ssize_t ret = k_copy_file_range(fd_in, NULL, fd_out, NULL, total - copied, 0);
        if (ret <= 0) ret = sendfile(fd_out, fd_in, NULL, total - copied);
        if (ret <= 0) {
            char buf[65536];
            ssize_t r = read(fd_in, buf, sizeof(buf));
            if (r <= 0) break;
            ret = write(fd_out, buf, r);
        }
        if (ret <= 0) break;
        copied += ret;
    }

    fchmod(fd_out, st_src->st_mode);
    struct timespec times[2] = { st_src->st_atim, st_src->st_mtim };
    futimens(fd_out, times);

    close(fd_in);
    close(fd_out);

    unlink(src);
    return 0;
}

static int move_single_file(const char *src_raw, const char *dest_input) {
    char src[1024], dest_base[1024], final_dest[1024];
    expand_tilde(src_raw, src, sizeof(src));
    expand_tilde(dest_input, dest_base, sizeof(dest_base));

    struct stat st_src;
    if (lstat(src, &st_src) < 0) {
        fprintf(stderr, "  %s[ERRO]%s Nao foi possivel acessar '%s': %s\n", COLOR_ERR, COLOR_RESET, src, strerror(errno));
        return -1;
    }

    struct stat st_dest;
    int dest_exists = (lstat(dest_base, &st_dest) == 0);

    if (dest_exists && S_ISDIR(st_dest.st_mode) && !opt_no_target_dir) {
        const char *base = strrchr(src, '/');
        base = (base) ? base + 1 : src;
        snprintf(final_dest, sizeof(final_dest), "%s/%s", dest_base, base);
    } else {
        strncpy(final_dest, dest_base, sizeof(final_dest) - 1);
        final_dest[sizeof(final_dest) - 1] = '\0';
    }

    struct stat st_final;
    int final_exists = (lstat(final_dest, &st_final) == 0);

    if (final_exists) {
        if (st_src.st_ino == st_final.st_ino && st_src.st_dev == st_final.st_dev) {
            fprintf(stderr, "  %s[ERRO]%s '%s' e '%s' sao o mesmo arquivo!\n", COLOR_ERR, COLOR_RESET, src, final_dest);
            return -1;
        }

        if (opt_no_clobber) {
            if (opt_verbose) printf("  [Ignorado: %s ja existe]\n", final_dest);
            return 0;
        }

        if (opt_update && (st_src.st_mtim.tv_sec <= st_final.st_mtim.tv_sec)) {
            if (opt_verbose) printf("  [Ignorado: %s e mais recente]\n", final_dest);
            return 0;
        }

        if (opt_backup) {
            char backup_path[1050];
            snprintf(backup_path, sizeof(backup_path), "%s~", final_dest);
            rename(final_dest, backup_path);
        }

        if (opt_interactive && !opt_force) {
            printf("mv: sobrescrever '%s'? (s/n): ", final_dest);
            fflush(stdout);
            char ans = getchar();
            while (getchar() != '\n');
            if (ans != 's' && ans != 'S' && ans != 'y' && ans != 'Y') {
                return 0;
            }
        }
    }

    if (rename(src, final_dest) == 0) {
        if (opt_verbose) {
            printf("  %s[OK]%s   %s%s%s -> %s%s%s (Renomeado)\n",
                   COLOR_OK, COLOR_RESET, COLOR_FILE, src, COLOR_RESET, COLOR_FILE, final_dest, COLOR_RESET);
        }
        return 0;
    }

    if (errno == EXDEV) {
        if (cross_device_move(src, final_dest, &st_src) == 0) {
            if (opt_verbose) {
                printf("  %s[OK]%s   %s%s%s -> %s%s%s (Cross-Device Move)\n",
                       COLOR_OK, COLOR_RESET, COLOR_FILE, src, COLOR_RESET, COLOR_FILE, final_dest, COLOR_RESET);
            }
            return 0;
        }
    }

    fprintf(stderr, "  %s[ERRO]%s Falha ao mover '%s' para '%s': %s\n", COLOR_ERR, COLOR_RESET, src, final_dest, strerror(errno));
    return -1;
}

int main(int argc, char *argv[]) {
    int stop_flags = 0;
    const char *sources[256];
    int source_count = 0;

    for (int i = 1; i < argc; i++) {
        if (!stop_flags && strcmp(argv[i], "--") == 0) {
            stop_flags = 1;
            continue;
        }

        if (!stop_flags && argv[i][0] == '-' && argv[i][1] != '\0') {
            if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
                print_help();
                return 0;
            }
            if (strcmp(argv[i], "--version") == 0) {
                print_help();
                return 0;
            }
            if (strcmp(argv[i], "--force") == 0) { opt_force = 1; continue; }
            if (strcmp(argv[i], "--interactive") == 0) { opt_interactive = 1; continue; }
            if (strcmp(argv[i], "--no-clobber") == 0) { opt_no_clobber = 1; continue; }
            if (strcmp(argv[i], "--update") == 0) { opt_update = 1; continue; }
            if (strcmp(argv[i], "--backup") == 0) { opt_backup = 1; continue; }
            if (strcmp(argv[i], "--verbose") == 0) { opt_verbose = 1; continue; }
            if (strcmp(argv[i], "--no-target-directory") == 0) { opt_no_target_dir = 1; continue; }

            // Flags combinadas (ex: -uv, -fv, -bv, -nv)
            size_t flen = strlen(argv[i]);
            for (size_t j = 1; j < flen; j++) {
                char opt = argv[i][j];
                if (opt == 'f') opt_force = 1;
                else if (opt == 'i') opt_interactive = 1;
                else if (opt == 'n') opt_no_clobber = 1;
                else if (opt == 'u') opt_update = 1;
                else if (opt == 'b') opt_backup = 1;
                else if (opt == 'v') opt_verbose = 1;
                else if (opt == 'T') opt_no_target_dir = 1;
                else if (opt == 'h') { print_help(); return 0; }
                else {
                    if (!opt_force) fprintf(stderr, "mv: opcao desconhecida '-%c'\n", opt);
                }
            }
        } else {
            if (source_count < 256) sources[source_count++] = argv[i];
        }
    }

    if (source_count < 2) {
        print_help();
        return 1;
    }

    const char *dest = sources[source_count - 1];
    source_count--;

    if (source_count > 1 && !opt_no_target_dir) {
        char exp_dest[1024];
        expand_tilde(dest, exp_dest, sizeof(exp_dest));
        struct stat st_dir;
        if (stat(exp_dest, &st_dir) < 0 || !S_ISDIR(st_dir.st_mode)) {
            fprintf(stderr, "mv: o destino '%s' nao e um diretorio valido\n", dest);
            return 1;
        }
    }

    int has_errors = 0;
    for (int i = 0; i < source_count; i++) {
        if (move_single_file(sources[i], dest) < 0) {
            has_errors = 1;
        }
    }

    return has_errors ? 1 : 0;
}
