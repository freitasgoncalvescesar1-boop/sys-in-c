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
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_FILE    "\033[1;36m"
#define COLOR_PROG    "\033[1;33m"
#define COLOR_VERIFY  "\033[1;35m"

#ifndef __NR_copy_file_range
    #if defined(__x86_64__)
        #define __NR_copy_file_range 326
    #elif defined(__i386__)
        #define __NR_copy_file_range 377
    #elif defined(__aarch64__) || defined(__arm__)
        #define __NR_copy_file_range 285
    #endif
#endif

// --- MOTOR SHA-256 PARA VALIDAÇÃO (-V / --verify) ---
typedef struct {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} SHA256_CTX;

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef4a3f7,0xc67178f2
};

#define ROTR(a,b) (((a) >> (b)) | ((a) << (32 - (b))))
#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x,2) ^ ROTR(x,13) ^ ROTR(x,22))
#define EP1(x) (ROTR(x,6) ^ ROTR(x,11) ^ ROTR(x,25))
#define SIG0(x) (ROTR(x,7) ^ ROTR(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x,17) ^ ROTR(x,19) ^ ((x) >> 10))

static void sha256_transform(SHA256_CTX *ctx, const uint8_t data[]) {
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];
    for (i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);
    for (; i < 64; ++i)
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e, f, g) + K[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx) {
    ctx->datalen = 0; ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(SHA256_CTX *ctx, uint8_t hash[]) {
    uint32_t i = ctx->datalen;
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }
    ctx->bitlen += ctx->datalen * 8;
    ctx->data[56] = (ctx->bitlen >> 56) & 0xFF;
    ctx->data[57] = (ctx->bitlen >> 48) & 0xFF;
    ctx->data[58] = (ctx->bitlen >> 40) & 0xFF;
    ctx->data[59] = (ctx->bitlen >> 32) & 0xFF;
    ctx->data[60] = (ctx->bitlen >> 24) & 0xFF;
    ctx->data[61] = (ctx->bitlen >> 16) & 0xFF;
    ctx->data[62] = (ctx->bitlen >> 8) & 0xFF;
    ctx->data[63] = (ctx->bitlen) & 0xFF;
    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4; ++i) {
        hash[i]      = (ctx->state[0] >> (24 - i * 8)) & 0xFF;
        hash[i + 4]  = (ctx->state[1] >> (24 - i * 8)) & 0xFF;
        hash[i + 8]  = (ctx->state[2] >> (24 - i * 8)) & 0xFF;
        hash[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0xFF;
        hash[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0xFF;
        hash[i + 20] = (ctx->state[5] >> (24 - i * 8)) & 0xFF;
        hash[i + 24] = (ctx->state[6] >> (24 - i * 8)) & 0xFF;
        hash[i + 28] = (ctx->state[7] >> (24 - i * 8)) & 0xFF;
    }
}

static int compute_file_sha256(const char *filepath, uint8_t hash_out[32]) {
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) return -1;

    SHA256_CTX ctx;
    sha256_init(&ctx);
    uint8_t buf[65536];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        sha256_update(&ctx, buf, n);
    }
    close(fd);
    sha256_final(&ctx, hash_out);
    return 0;
}

static void print_help(void) {
    low_print_banner("cp");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./cp [OPTIONS] <SOURCE> <DESTINATION>\n");
    printf("  ./cp [OPTIONS] <SOURCE...> <DIRECTORY>\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Zero-Copy recursive file copier with colorful output, SHA-256 verification and combined flags.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-r, -R, --recursive%s       Copy directories recursively\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-p, -a, --preserve%s        Preserve timestamps and permissions\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-u, --update%s              Copy only when SOURCE is newer than DEST or missing\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-V, --verify%s              Verify data integrity via SHA-256 post-copy\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-i, --interactive%s         Prompt before overwrite\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-d, -P, --no-dereference%s  Copy symlinks as symlinks\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-f, --force%s               Force overwrite existing destination files\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s                Display this formatted help guide and exit\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --version%s             Display version information\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sCOMBINED SHORT FLAGS EXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./cp -rp ./src ./backup/%s              (Recursivo + Preserva permissões/datas)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./cp -rV imagem.iso /tmp/%s              (Copia com verificação SHA-256)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./cp -rf ./pasta1 /destino/%s            (Recursivo + Forçado sem confirmações)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
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

static double get_time_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static int copy_single_file(const char *src_raw, const char *dest_input, int no_dereference, int force, int interactive, int preserve, int update, int verify);

static int copy_directory_recursive(const char *src_dir, const char *dest_dir, int no_dereference, int force, int interactive, int preserve, int update, int verify) {
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
            copy_directory_recursive(sub_src, sub_dest, no_dereference, force, interactive, preserve, update, verify);
        } else {
            copy_single_file(sub_src, sub_dest, no_dereference, force, interactive, preserve, update, verify);
        }
    }

    closedir(dir);

    if (preserve) {
        struct timespec times[2] = { st.st_atim, st.st_mtim };
        chmod(dest_dir, st.st_mode);
        utimensat(AT_FDCWD, dest_dir, times, 0);
    }
    return 0;
}

static int copy_single_file(const char *src_raw, const char *dest_input, int no_dereference, int force, int interactive, int preserve, int update, int verify) {
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
    int final_exists = (stat(final_dest, &st_final) == 0);

    if (final_exists) {
        if (st_src.st_ino == st_final.st_ino && st_src.st_dev == st_final.st_dev) {
            fprintf(stderr, "  %s[ERRO]%s '%s' e '%s' sao o mesmo arquivo!\n", COLOR_ERR, COLOR_RESET, src, final_dest);
            return -1;
        }

        if (update && (st_src.st_mtim.tv_sec <= st_final.st_mtim.tv_sec)) {
            printf("  [Ignorado: '%s' ja esta atualizado]\n", final_dest);
            return 0;
        }

        if (interactive && !force) {
            printf("cp: sobrescrever '%s'? (s/n): ", final_dest);
            fflush(stdout);
            char ans = getchar();
            while (getchar() != '\n');
            if (ans != 's' && ans != 'S' && ans != 'y' && ans != 'Y') {
                return 0;
            }
        }
        if (force) unlink(final_dest);
    }

    // Cópia de Symlink
    if (no_dereference && S_ISLNK(st_src.st_mode)) {
        char link_target[1024];
        ssize_t len = readlink(src, link_target, sizeof(link_target) - 1);
        if (len < 0) return -1;
        link_target[len] = '\0';

        unlink(final_dest);
        symlink(link_target, final_dest);

        printf("  %s[OK: SYMLINK]%s %s%s%s -> %s%s%s\n",
               COLOR_OK, COLOR_RESET, COLOR_FILE, src, COLOR_RESET, COLOR_FILE, final_dest, COLOR_RESET);
        return 0;
    }

    int fd_in = open(src, O_RDONLY);
    if (fd_in < 0) {
        fprintf(stderr, "  %s[ERRO]%s Nao foi possivel abrir '%s': %s\n", COLOR_ERR, COLOR_RESET, src, strerror(errno));
        return -1;
    }

    int fd_out = open(final_dest, O_WRONLY | O_CREAT | O_TRUNC, st_src.st_mode);
    if (fd_out < 0) {
        fprintf(stderr, "  %s[ERRO]%s Nao foi possivel criar '%s': %s\n", COLOR_ERR, COLOR_RESET, final_dest, strerror(errno));
        close(fd_in);
        return -1;
    }

    off_t total = st_src.st_size;
    off_t copied = 0;
    int show_progress = (total > 4 * 1024 * 1024 && isatty(STDOUT_FILENO));
    double t_start = get_time_sec();
    double last_print = 0;

    while (copied < total) {
        size_t chunk = (total - copied > 2 * 1024 * 1024) ? 2 * 1024 * 1024 : (size_t)(total - copied);
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

        double now = get_time_sec();
        if (show_progress && (now - last_print >= 0.1 || copied == total)) {
            last_print = now;
            double elapsed = now - t_start;
            double speed = (elapsed > 0.05) ? ((double)copied / (1024.0 * 1024.0 * elapsed)) : 0;
            int pct = (int)(((double)copied / (double)total) * 100.0);

            printf("\r  %s[COPIANDO]%s %s%s%s -> %s%d%%%s (%.1f/%.1f MB | %.1f MB/s)  ",
                   COLOR_PROG, COLOR_RESET, COLOR_FILE, src, COLOR_RESET,
                   COLOR_OK, pct, COLOR_RESET,
                   (double)copied / (1024.0 * 1024.0), (double)total / (1024.0 * 1024.0), speed);
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

    // ✨ Saída Rica e Colorida Restaurada por Padrão!
    printf("  %s[OK]%s   %s%s%s -> %s%s%s (%lld bytes)\n",
           COLOR_OK, COLOR_RESET, COLOR_FILE, src, COLOR_RESET, COLOR_FILE, final_dest, COLOR_RESET, (long long)copied);

    // Validação de Integridade SHA-256 (-V / --verify)
    if (verify) {
        uint8_t hash_src[32], hash_dest[32];
        if (compute_file_sha256(src, hash_src) == 0 && compute_file_sha256(final_dest, hash_dest) == 0) {
            if (memcmp(hash_src, hash_dest, 32) == 0) {
                printf("  %s[✔ SHA-256 VERIFIED]%s %s%s%s -> %s%s%s (Integridade 100%% OK)\n",
                       COLOR_VERIFY, COLOR_RESET, COLOR_FILE, src, COLOR_RESET, COLOR_FILE, final_dest, COLOR_RESET);
            } else {
                fprintf(stderr, "  %s[✖ SHA-256 FALHA]%s Os hashes da origem e destino nao conferem!\n", COLOR_ERR, COLOR_RESET);
                return -1;
            }
        }
    }

    return 0;
}

int main(int argc, char *argv[]) {
    int recursive = 0, no_dereference = 0, force = 0, interactive = 0;
    int preserve = 0, update = 0, verify = 0;
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
            if (strcmp(argv[i], "--recursive") == 0) { recursive = 1; continue; }
            if (strcmp(argv[i], "--preserve") == 0) { preserve = 1; continue; }
            if (strcmp(argv[i], "--interactive") == 0) { interactive = 1; continue; }
            if (strcmp(argv[i], "--no-dereference") == 0) { no_dereference = 1; continue; }
            if (strcmp(argv[i], "--force") == 0) { force = 1; continue; }
            if (strcmp(argv[i], "--update") == 0) { update = 1; continue; }
            if (strcmp(argv[i], "--verify") == 0) { verify = 1; continue; }

            // Flags Curtas Combinadas (ex: -rp, -rf, -a, -p, -u, -V)
            size_t flen = strlen(argv[i]);
            for (size_t j = 1; j < flen; j++) {
                char opt = argv[i][j];
                if (opt == 'r' || opt == 'R') recursive = 1;
                else if (opt == 'p' || opt == 'a') preserve = 1;
                else if (opt == 'i') interactive = 1;
                else if (opt == 'd' || opt == 'P') no_dereference = 1;
                else if (opt == 'f') force = 1;
                else if (opt == 'u') update = 1;
                else if (opt == 'V') verify = 1;
                else if (opt == 'h') { print_help(); return 0; }
                else {
                    if (!force) fprintf(stderr, "cp: opcao desconhecida '-%c'\n", opt);
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

    int has_errors = 0;
    for (int i = 0; i < source_count; i++) {
        char exp_src[1024];
        expand_tilde(sources[i], exp_src, sizeof(exp_src));

        struct stat st;
        if (lstat(exp_src, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (recursive) {
                if (copy_directory_recursive(exp_src, dest, no_dereference, force, interactive, preserve, update, verify) < 0) {
                    has_errors = 1;
                }
            } else {
                fprintf(stderr, "cp: omitindo diretorio '%s' (use -r para recursao)\n", sources[i]);
                has_errors = 1;
            }
        } else {
            if (copy_single_file(sources[i], dest, no_dereference, force, interactive, preserve, update, verify) < 0) {
                has_errors = 1;
            }
        }
    }

    return has_errors ? 1 : 0;
}
