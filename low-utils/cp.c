#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <sys/syscall.h>
#include <time.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET "\033[0m"
#define COLOR_OK    "\033[1;32m"
#define COLOR_ERR   "\033[1;31m"
#define COLOR_FILE  "\033[1;36m"
#define COLOR_PROG  "\033[1;33m"

#ifndef __NR_copy_file_range
    #if defined(__x86_64__)
        #define __NR_copy_file_range 326
    #elif defined(__i386__)
        #define __NR_copy_file_range 377
    #elif defined(__aarch64__) || defined(__arm__)
        #define __NR_copy_file_range 285
    #endif
#endif

static void print_help(void) {
    low_print_banner("cp");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./cp [OPTIONS] <SOURCE> <DESTINATION>\n");
    printf("  ./cp [OPTIONS] <SOURCE...> <DIRECTORY>\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Zero-Copy recursive file copier with live progress and attribute preservation.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-r, -R, --recursive%s       Copy directories recursively\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-p, -a, --preserve%s        Preserve timestamps and permissions\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-i, --interactive%s         Prompt before overwrite\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-d, -P, --no-dereference%s  Copy symlinks as symlinks\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-f, --force%s               Force overwrite existing destination files\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s                Display this formatted help guide and exit\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --version%s             Display version and repository information\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./cp -r ./src ./backup/%s        (Copia pasta inteira recursivamente)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./cp -p arquivo.iso ~/Downloads/%s (Preserva datas e exibe progresso)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./cp -d atalho.lnk ./destino%s    (Preserva link simbolico)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
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
    errno = ENOSYS;
    return -1;
#endif
}

static int copy_single_file(const char *src, const char *dest, int no_dereference, int force, int interactive, int preserve);

static int copy_directory_recursive(const char *src_dir, const char *dest_dir, int no_dereference, int force, int interactive, int preserve) {
    struct stat st;
    if (stat(src_dir, &st) < 0) return -1;

    mkdir(dest_dir, st.st_mode);

    DIR *dir = opendir(src_dir);
    if (!dir) return -1;

    struct dirent *entry;
    char sub_src[2048], sub_dest[2048];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        snprintf(sub_src, sizeof(sub_src), "%s/%s", src_dir, entry->d_name);
        snprintf(sub_dest, sizeof(sub_dest), "%s/%s", dest_dir, entry->d_name);

        struct stat sub_st;
        if (lstat(sub_src, &sub_st) == 0 && S_ISDIR(sub_st.st_mode)) {
            copy_directory_recursive(sub_src, sub_dest, no_dereference, force, interactive, preserve);
        } else {
            copy_single_file(sub_src, sub_dest, no_dereference, force, interactive, preserve);
        }
    }

    closedir(dir);
    return 0;
}

static int copy_single_file(const char *src_raw, const char *dest_input, int no_dereference, int force, int interactive, int preserve) {
    char src[1024], dest_base[1024], final_dest[1024];
    expand_tilde(src_raw, src, sizeof(src));
    expand_tilde(dest_input, dest_base, sizeof(dest_base));

    struct stat st_src;
    int src_res = no_dereference ? lstat(src, &st_src) : stat(src, &st_src);
    if (src_res < 0) {
        fprintf(stderr, "  %s[ERRO]%s '%s': %s\n", COLOR_ERR, COLOR_RESET, src, strerror(errno));
        return -1;
    }

    struct stat st_dest;
    if (stat(dest_base, &st_dest) == 0 && S_ISDIR(st_dest.st_mode)) {
        const char *base = strrchr(src, '/');
        base = (base) ? base + 1 : src;
        snprintf(final_dest, sizeof(final_dest), "%s/%s", dest_base, base);
    } else {
        strncpy(final_dest, dest_base, sizeof(final_dest) - 1);
        final_dest[sizeof(final_dest) - 1] = '\0';
    }

    struct stat st_final;
    if (stat(final_dest, &st_final) == 0) {
        if (st_src.st_ino == st_final.st_ino && st_src.st_dev == st_final.st_dev) {
            fprintf(stderr, "  %s[ERRO]%s '%s' e '%s' sao o mesmo arquivo!\n", COLOR_ERR, COLOR_RESET, src, final_dest);
            return -1;
        }

        if (interactive) {
            printf("cp: sobrescrever '%s'? (s/n): ", final_dest);
            char ans = getchar();
            while (getchar() != '\n');
            if (ans != 's' && ans != 'S' && ans != 'y' && ans != 'Y') {
                printf("  [Ignorado]\n");
                return 0;
            }
        }
        if (force) unlink(final_dest);
    }

    if (no_dereference && S_ISLNK(st_src.st_mode)) {
        char link_target[1024];
        ssize_t len = readlink(src, link_target, sizeof(link_target) - 1);
        if (len < 0) return -1;
        link_target[len] = '\0';

        unlink(final_dest);
        symlink(link_target, final_dest);
        printf("  %s[OK: SYMLINK]%s %s%s%s -> %s%s%s\n", COLOR_OK, COLOR_RESET, COLOR_FILE, src, COLOR_RESET, COLOR_FILE, final_dest, COLOR_RESET);
        return 0;
    }

    int fd_in = open(src, O_RDONLY);
    if (fd_in < 0) return -1;

    int fd_out = open(final_dest, O_WRONLY | O_CREAT | O_TRUNC, st_src.st_mode);
    if (fd_out < 0) {
        close(fd_in);
        return -1;
    }

    off_t total = st_src.st_size;
    off_t copied = 0;
    int show_progress = (total > 5 * 1024 * 1024 && isatty(STDOUT_FILENO));

    while (copied < total) {
        size_t chunk = (total - copied > 2 * 1024 * 1024) ? 2 * 1024 * 1024 : total - copied;
        ssize_t ret = k_copy_file_range(fd_in, NULL, fd_out, NULL, chunk, 0);
        if (ret <= 0) {
            ret = sendfile(fd_out, fd_in, NULL, chunk);
        }
        if (ret <= 0) {
            char buf[65536];
            ssize_t r = read(fd_in, buf, sizeof(buf));
            if (r <= 0) break;
            ret = write(fd_out, buf, r);
        }
        if (ret <= 0) break;

        copied += ret;
        if (show_progress) {
            int pct = (int)(((double)copied / (double)total) * 100.0);
            printf("\r  %s[COPIANDO]%s %s%s%s -> %d%% (%.1f/%.1f MB)",
                   COLOR_PROG, COLOR_RESET, COLOR_FILE, src, COLOR_RESET, pct, (double)copied/(1024*1024), (double)total/(1024*1024));
            fflush(stdout);
        }
    }

    if (show_progress) printf("\n");

    fchmod(fd_out, st_src.st_mode);

    if (preserve) {
        struct timespec times[2];
        times[0] = st_src.st_atim;
        times[1] = st_src.st_mtim;
        futimens(fd_out, times);
    }

    close(fd_in);
    close(fd_out);

    printf("  %s[OK]%s   %s%s%s -> %s%s%s (%lld bytes)\n",
           COLOR_OK, COLOR_RESET, COLOR_FILE, src, COLOR_RESET, COLOR_FILE, final_dest, COLOR_RESET, (long long)copied);

    return 0;
}

int main(int argc, char *argv[]) {
    int recursive = 0, no_dereference = 0, force = 0, interactive = 0, preserve = 0;
    const char *sources[256];
    int source_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            print_help();
            return 0;
        }

        if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "-R") == 0 || strcmp(argv[i], "--recursive") == 0) recursive = 1;
        else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--preserve") == 0) preserve = 1;
        else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--interactive") == 0) interactive = 1;
        else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "-P") == 0 || strcmp(argv[i], "--no-dereference") == 0) no_dereference = 1;
        else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) force = 1;
        else {
            if (source_count < 256) sources[source_count++] = argv[i];
        }
    }

    if (source_count < 2) {
        print_help();
        return 1;
    }

    const char *dest = sources[source_count - 1];
    source_count--;

    int has_errors = 0;
    for (int i = 0; i < source_count; i++) {
        char exp_src[1024];
        expand_tilde(sources[i], exp_src, sizeof(exp_src));

        struct stat st;
        if (lstat(exp_src, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (recursive) {
                if (copy_directory_recursive(exp_src, dest, no_dereference, force, interactive, preserve) < 0) has_errors = 1;
            } else {
                fprintf(stderr, "cp: omitindo diretorio '%s' (use -r para recursao)\n", sources[i]);
                has_errors = 1;
            }
        } else {
            if (copy_single_file(sources[i], dest, no_dereference, force, interactive, preserve) < 0) has_errors = 1;
        }
    }

    return has_errors ? 1 : 0;
}
