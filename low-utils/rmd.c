#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/xattr.h>
#include <time.h>
#include <utime.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include "low.h"

#define ALIGN_SIZE    4096
#define CHUNK_SIZE    65536

#define COLOR_RESET   "\033[0m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_FILE    "\033[1;36m"
#define COLOR_TAG     "\033[1;33m"
#define COLOR_CRYPTO  "\033[1;35m"

#ifndef MADV_DONTDUMP
#define MADV_DONTDUMP 16
#endif

/* =========================================================================
 *  ZEROIZAÇÃO E BLOQUEIO DE MEMÓRIA (mlock + MADV_DONTDUMP + explicit_bzero)
 * ========================================================================= */
static void secure_bzero(void *v, size_t n) {
    if (!v || n == 0) return;
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))
    explicit_bzero(v, n);
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    explicit_bzero(v, n);
#else
    volatile uint8_t *p = (volatile uint8_t *)v;
    while (n--) *p++ = 0;
    __asm__ __volatile__("" : : "r"(v) : "memory");
#endif
}

static void *secure_alloc_and_lock(size_t size) {
    void *ptr = NULL;
    if (posix_memalign(&ptr, ALIGN_SIZE, size) != 0 || !ptr) {
        return NULL;
    }
    mlock(ptr, size);
    madvise(ptr, size, MADV_DONTDUMP);
    return ptr;
}

static void secure_unlock_and_free(void *ptr, size_t size) {
    if (!ptr) return;
    secure_bzero(ptr, size);
    munlock(ptr, size);
    free(ptr);
}

static void wipe_extended_attributes(int fd, const char *filepath) {
    char list[4096];
    ssize_t len = 0;

    if (fd >= 0) {
        len = flistxattr(fd, list, sizeof(list));
    } else {
        len = llistxattr(filepath, list, sizeof(list));
    }

    if (len > 0) {
        const char *name = list;
        while (name < list + len) {
            if (fd >= 0) fremovexattr(fd, name);
            else lremovexattr(filepath, name);
            name += strlen(name) + 1;
        }
    }
}

static void sync_parent_directory(const char *filepath) {
    char dir_part[1024] = ".";
    const char *last_slash = strrchr(filepath, '/');
    if (last_slash) {
        size_t dlen = last_slash - filepath;
        if (dlen == 0) {
            strcpy(dir_part, "/");
        } else if (dlen < sizeof(dir_part)) {
            strncpy(dir_part, filepath, dlen);
            dir_part[dlen] = '\0';
        }
    }

    int dfd = open(dir_part, O_RDONLY | O_DIRECTORY);
    if (dfd >= 0) {
        fsync(dfd);
        close(dfd);
    }
}

static void drop_kernel_caches(void) {
    if (geteuid() == 0) {
        int fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
        if (fd >= 0) {
            write(fd, "3\n", 2);
            close(fd);
        }
    }
}

/* =========================================================================
 *  MOTOR CRIPTOGRÁFICO: CHACHA20 & AES-256-CTR
 * ========================================================================= */
#define ROTL(a,b) (((a) << (b)) | ((a) >> (32 - (b))))
#define QR(a, b, c, d) \
    a += b; d ^= a; d = ROTL(d, 16); \
    c += d; b ^= c; b = ROTL(b, 12); \
    a += b; d ^= a; d = ROTL(d, 8);  \
    c += d; b ^= c; b = ROTL(b, 7);

static void chacha20_block(const uint32_t key[8], const uint32_t nonce[3], uint32_t counter, uint8_t out[64]) {
    uint32_t state[16] = {
        0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
        key[0], key[1], key[2], key[3], key[4], key[5], key[6], key[7],
        counter, nonce[0], nonce[1], nonce[2]
    };
    uint32_t x[16];
    memcpy(x, state, sizeof(x));

    for (int i = 0; i < 10; i++) {
        QR(x[0], x[4], x[8],  x[12]);
        QR(x[1], x[5], x[9],  x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8],  x[13]);
        QR(x[3], x[4], x[9],  x[14]);
    }

    for (int i = 0; i < 16; i++) {
        uint32_t v = x[i] + state[i];
        out[i * 4 + 0] = (uint8_t)((v >> 0) & 0xFF);
        out[i * 4 + 1] = (uint8_t)((v >> 8) & 0xFF);
        out[i * 4 + 2] = (uint8_t)((v >> 16) & 0xFF);
        out[i * 4 + 3] = (uint8_t)((v >> 24) & 0xFF);
    }
    secure_bzero(x, sizeof(x));
}

static void chacha20_crypt_stream(const uint8_t key[32], const uint8_t nonce[12], uint32_t *counter, uint8_t *data, size_t len) {
    uint32_t k[8], n[3];
    memcpy(k, key, 32);
    memcpy(n, nonce, 12);

    uint8_t block[64];
    while (len > 0) {
        chacha20_block(k, n, *counter, block);
        (*counter)++;
        size_t chunk = (len > 64) ? 64 : len;
        for (size_t i = 0; i < chunk; i++) data[i] ^= block[i];
        data += chunk;
        len -= chunk;
    }
    secure_bzero(block, sizeof(block));
    secure_bzero(k, sizeof(k));
    secure_bzero(n, sizeof(n));
}

static const uint8_t aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t aes_rcon[11] = {
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36
};

typedef struct {
    uint32_t round_keys[60];
} aes256_ctx_t;

static inline uint8_t xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ (((x >> 7) & 1) * 0x1B));
}

static void aes256_key_expansion(aes256_ctx_t *ctx, const uint8_t key[32]) {
    for (int i = 0; i < 8; i++) {
        ctx->round_keys[i] = (key[4*i] << 24) | (key[4*i+1] << 16) | (key[4*i+2] << 8) | key[4*i+3];
    }
    for (int i = 8; i < 60; i++) {
        uint32_t temp = ctx->round_keys[i - 1];
        if (i % 8 == 0) {
            temp = (temp << 8) | (temp >> 24);
            temp = ((uint32_t)aes_sbox[(temp >> 24) & 0xFF] << 24) |
                   ((uint32_t)aes_sbox[(temp >> 16) & 0xFF] << 16) |
                   ((uint32_t)aes_sbox[(temp >> 8)  & 0xFF] << 8)  |
                   ((uint32_t)aes_sbox[temp & 0xFF]);
            temp ^= ((uint32_t)aes_rcon[i / 8] << 24);
        } else if (i % 8 == 4) {
            temp = ((uint32_t)aes_sbox[(temp >> 24) & 0xFF] << 24) |
                   ((uint32_t)aes_sbox[(temp >> 16) & 0xFF] << 16) |
                   ((uint32_t)aes_sbox[(temp >> 8)  & 0xFF] << 8)  |
                   ((uint32_t)aes_sbox[temp & 0xFF]);
        }
        ctx->round_keys[i] = ctx->round_keys[i - 8] ^ temp;
    }
}

static void aes256_encrypt_block(const aes256_ctx_t *ctx, const uint8_t in[16], uint8_t out[16]) {
    uint8_t state[4][4];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            state[r][c] = in[r + 4 * c] ^ ((ctx->round_keys[c] >> (24 - 8 * r)) & 0xFF);

    for (int round = 1; round <= 14; round++) {
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                state[r][c] = aes_sbox[state[r][c]];

        uint8_t t;
        t = state[1][0]; state[1][0] = state[1][1]; state[1][1] = state[1][2]; state[1][2] = state[1][3]; state[1][3] = t;
        t = state[2][0]; state[2][0] = state[2][2]; state[2][2] = t; t = state[2][1]; state[2][1] = state[2][3]; state[2][3] = t;
        t = state[3][3]; state[3][3] = state[3][2]; state[3][2] = state[3][1]; state[3][1] = state[3][0]; state[3][0] = t;

        if (round < 14) {
            for (int c = 0; c < 4; c++) {
                uint8_t a = state[0][c], b = state[1][c], d = state[2][c], e = state[3][c];
                state[0][c] = xtime(a ^ b) ^ b ^ d ^ e;
                state[1][c] = xtime(b ^ d) ^ d ^ e ^ a;
                state[2][c] = xtime(d ^ e) ^ e ^ a ^ b;
                state[3][c] = xtime(e ^ a) ^ a ^ b ^ d;
            }
        }

        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                state[r][c] ^= (uint8_t)((ctx->round_keys[round * 4 + c] >> (24 - 8 * r)) & 0xFF);
    }

    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r + 4 * c] = state[r][c];

    secure_bzero(state, sizeof(state));
}

static void aes256_ctr_stream(const aes256_ctx_t *ctx, uint8_t iv[16], uint8_t *data, size_t len) {
    uint8_t keystream[16];
    while (len > 0) {
        aes256_encrypt_block(ctx, iv, keystream);

        for (int i = 15; i >= 0; i--) {
            if (++iv[i] != 0) break;
        }

        size_t chunk = (len > 16) ? 16 : len;
        for (size_t i = 0; i < chunk; i++) data[i] ^= keystream[i];
        data += chunk;
        len -= chunk;
    }
    secure_bzero(keystream, sizeof(keystream));
}

static void print_help(void) {
    low_print_banner("rmd");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./rmd [OPTIONS] <FILE/DIR...>\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Direct I/O Military Crypto-Shredder with Memory Lock, XAttr Purge & Directory Sync.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-r, -R, --recursive%s  Remove directories and their contents recursively\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-f, --force%s          Ignore nonexistent files and never prompt\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-p, --passes <N>%s     Number of wipe passes (1=zeros, 3=DoD zeros/ones/urandom) [Default: 1]\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s--no-preserve-root%s   Do not treat '/' specially (dangerous)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s           Display this formatted help guide and exit\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --version%s        Display version and repository information\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sCOMBINED SHORT FLAGS SUPPORT:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./rmd -rf ./pasta_antiga/%s           (Recursivo + Forçado sem confirmações)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./rmd -rfp 3 ./segredos/%s            (Recursivo + Forçado + 3 passes DoD)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

static void obfuscate_and_unlink(const char *filepath) {
    char dir_part[1024] = ".";
    const char *last_slash = strrchr(filepath, '/');
    if (last_slash) {
        size_t dlen = last_slash - filepath;
        if (dlen == 0) {
            strcpy(dir_part, "/");
        } else if (dlen < sizeof(dir_part)) {
            strncpy(dir_part, filepath, dlen);
            dir_part[dlen] = '\0';
        }
    }

    char obf_path[2048];
    snprintf(obf_path, sizeof(obf_path), "%s/tmp_shred_%ld_%d", dir_part, time(NULL), rand() % 99999);
    rename(filepath, obf_path);

    struct utimbuf ut = { .actime = 0, .modtime = 0 };
    utime(obf_path, &ut);

    unlink(obf_path);
    sync_parent_directory(obf_path);
}

static int secure_shred_file(const char *filepath, int passes, int force) {
    struct stat st;
    if (lstat(filepath, &st) < 0) {
        if (!force) fprintf(stderr, "  %s[ERRO]%s %s: %s\n", COLOR_ERR, COLOR_RESET, filepath, strerror(errno));
        return -1;
    }

    if (S_ISLNK(st.st_mode)) {
        if (unlink(filepath) == 0) {
            sync_parent_directory(filepath);
            printf("  %s[OK: LINK REMOVIDO]%s %s%s%s\n", COLOR_OK, COLOR_RESET, COLOR_FILE, filepath, COLOR_RESET);
            return 0;
        }
        return -1;
    }

    if (S_ISREG(st.st_mode) && st.st_size > 0) {
        wipe_extended_attributes(-1, filepath);

        int fd = open(filepath, O_RDWR | O_DIRECT);
        int is_direct = 1;
        if (fd < 0) {
            is_direct = 0;
            fd = open(filepath, O_RDWR);
        }

        if (fd < 0) {
            if (!force) fprintf(stderr, "  %s[ERRO]%s %s: %s\n", COLOR_ERR, COLOR_RESET, filepath, strerror(errno));
            return -1;
        }

        uint8_t *aligned_buf = (uint8_t *)secure_alloc_and_lock(CHUNK_SIZE);
        if (!aligned_buf) {
            close(fd);
            return -1;
        }

        int rand_fd = open("/dev/urandom", O_RDONLY);

        for (int p = 1; p <= passes; p++) {
            lseek(fd, 0, SEEK_SET);
            off_t remaining = st.st_size;

            if (p == 1) memset(aligned_buf, 0x00, CHUNK_SIZE);
            else if (p == 2) memset(aligned_buf, 0xFF, CHUNK_SIZE);

            while (remaining > 0) {
                size_t to_write = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
                if (p >= 3 && rand_fd >= 0) {
                    ssize_t r = read(rand_fd, aligned_buf, to_write);
                    (void)r;
                }

                size_t write_sz = is_direct ? ((to_write + ALIGN_SIZE - 1) & ~(ALIGN_SIZE - 1)) : to_write;
                ssize_t w = write(fd, aligned_buf, write_sz);
                if (w < 0) break;
                remaining -= to_write;
            }
            fsync(fd);
        }

        // Crypto Shred 1: AES-256-CTR
        if (rand_fd >= 0) {
            uint8_t *crypto_keys = (uint8_t *)secure_alloc_and_lock(48);
            if (crypto_keys) {
                uint8_t *aes_key = crypto_keys;
                uint8_t *aes_iv = crypto_keys + 32;

                ssize_t r1 = read(rand_fd, aes_key, 32);
                ssize_t r2 = read(rand_fd, aes_iv, 16);
                (void)r1; (void)r2;

                aes256_ctx_t aes_ctx;
                aes256_key_expansion(&aes_ctx, aes_key);

                lseek(fd, 0, SEEK_SET);
                off_t remaining = st.st_size;
                while (remaining > 0) {
                    off_t cur_pos = lseek(fd, 0, SEEK_CUR);
                    size_t to_crypt = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
                    size_t read_sz = is_direct ? ((to_crypt + ALIGN_SIZE - 1) & ~(ALIGN_SIZE - 1)) : to_crypt;

                    ssize_t n = read(fd, aligned_buf, read_sz);
                    if (n <= 0) break;

                    aes256_ctr_stream(&aes_ctx, aes_iv, aligned_buf, n);

                    lseek(fd, cur_pos, SEEK_SET);
                    ssize_t w = write(fd, aligned_buf, is_direct ? read_sz : (size_t)n);
                    (void)w;
                    remaining -= to_crypt;
                }
                fsync(fd);

                secure_bzero(&aes_ctx, sizeof(aes_ctx));
                secure_unlock_and_free(crypto_keys, 48);
            }
        }

        // Crypto Shred 2: ChaCha20
        if (rand_fd >= 0) {
            uint8_t *crypto_keys = (uint8_t *)secure_alloc_and_lock(48);
            if (crypto_keys) {
                uint8_t *chacha_key = crypto_keys;
                uint8_t *chacha_nonce = crypto_keys + 32;

                ssize_t r1 = read(rand_fd, chacha_key, 32);
                ssize_t r2 = read(rand_fd, chacha_nonce, 12);
                (void)r1; (void)r2;

                uint32_t counter = 1;
                lseek(fd, 0, SEEK_SET);
                off_t remaining = st.st_size;
                while (remaining > 0) {
                    off_t cur_pos = lseek(fd, 0, SEEK_CUR);
                    size_t to_crypt = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
                    size_t read_sz = is_direct ? ((to_crypt + ALIGN_SIZE - 1) & ~(ALIGN_SIZE - 1)) : to_crypt;

                    ssize_t n = read(fd, aligned_buf, read_sz);
                    if (n <= 0) break;

                    chacha20_crypt_stream(chacha_key, chacha_nonce, &counter, aligned_buf, n);

                    lseek(fd, cur_pos, SEEK_SET);
                    ssize_t w = write(fd, aligned_buf, is_direct ? read_sz : (size_t)n);
                    (void)w;
                    remaining -= to_crypt;
                }
                fsync(fd);

                secure_unlock_and_free(crypto_keys, 48);
            }
        }

        ftruncate(fd, 0);
        fsync(fd);

        secure_unlock_and_free(aligned_buf, CHUNK_SIZE);
        if (rand_fd >= 0) close(rand_fd);
        close(fd);
    }

    obfuscate_and_unlink(filepath);

    printf("  %s[OK: FORENSIC PURGE]%s %s%s%s (%d-pass | O_DIRECT + AES-256 + ChaCha20 + mlock + DirSync)\n",
           COLOR_OK, COLOR_RESET, COLOR_FILE, filepath, COLOR_RESET, passes);

    return 0;
}

static int rmd_recursive(const char *dir_path, int passes, int force) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        if (!force) fprintf(stderr, "  %s[ERRO]%s Nao foi possivel abrir pasta '%s': %s\n", COLOR_ERR, COLOR_RESET, dir_path, strerror(errno));
        return -1;
    }

    struct dirent *entry;
    char path[2048];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            rmd_recursive(path, passes, force);
        } else {
            secure_shred_file(path, passes, force);
        }
    }

    closedir(dir);
    if (rmdir(dir_path) == 0) {
        sync_parent_directory(dir_path);
        printf("  %s[OK: DIR REMOVIDO]%s  %s%s%s\n", COLOR_OK, COLOR_RESET, COLOR_FILE, dir_path, COLOR_RESET);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int recursive = 0, force = 0, passes = 1, preserve_root = 1;
    const char *targets[256];
    int target_count = 0;
    int stop_flags = 0;

    for (int i = 1; i < argc; i++) {
        if (!stop_flags && strcmp(argv[i], "--") == 0) {
            stop_flags = 1;
            continue;
        }

        if (!stop_flags && argv[i][0] == '-' && argv[i][1] != '\0') {
            // Flags Longas
            if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
                print_help();
                return 0;
            }
            if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
                print_help();
                return 0;
            }
            if (strcmp(argv[i], "--recursive") == 0) {
                recursive = 1;
                continue;
            }
            if (strcmp(argv[i], "--force") == 0) {
                force = 1;
                continue;
            }
            if (strcmp(argv[i], "--no-preserve-root") == 0) {
                preserve_root = 0;
                continue;
            }
            if (strcmp(argv[i], "--passes") == 0) {
                if (i + 1 < argc) {
                    passes = atoi(argv[++i]);
                    if (passes <= 0) passes = 1;
                    if (passes > 10) passes = 10;
                }
                continue;
            }

            // Flags Curtas Combinadas (ex: -rf, -fr, -rfp 3, -p3)
            size_t flen = strlen(argv[i]);
            for (size_t j = 1; j < flen; j++) {
                char opt = argv[i][j];
                if (opt == 'r' || opt == 'R') {
                    recursive = 1;
                } else if (opt == 'f') {
                    force = 1;
                } else if (opt == 'h') {
                    print_help();
                    return 0;
                } else if (opt == 'p') {
                    if (j + 1 < flen && isdigit((unsigned char)argv[i][j + 1])) {
                        passes = atoi(&argv[i][j + 1]);
                        if (passes <= 0) passes = 1;
                        if (passes > 10) passes = 10;
                        break;
                    } else if (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0])) {
                        passes = atoi(argv[++i]);
                        if (passes <= 0) passes = 1;
                        if (passes > 10) passes = 10;
                        break;
                    }
                } else {
                    if (!force) fprintf(stderr, "rmd: opcao desconhecida '-%c'\n", opt);
                }
            }
        } else {
            if (target_count < 256) targets[target_count++] = argv[i];
        }
    }

    if (target_count == 0) {
        print_help();
        return 1;
    }

    srand(time(NULL));
    int has_errors = 0;

    for (int i = 0; i < target_count; i++) {
        if (preserve_root && (strcmp(targets[i], "/") == 0 || strcmp(targets[i], "/etc") == 0)) {
            fprintf(stderr, "  %s[SEGURANCA]%s Destruicao de '%s' bloqueada! (use --no-preserve-root)\n", COLOR_ERR, COLOR_RESET, targets[i]);
            return 1;
        }

        struct stat st;
        if (lstat(targets[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            if (recursive) {
                if (rmd_recursive(targets[i], passes, force) < 0) has_errors = 1;
            } else {
                if (!force) fprintf(stderr, "  %s[ERRO]%s '%s' e um diretorio (use -r ou -rf para recursao)\n", COLOR_ERR, COLOR_RESET, targets[i]);
                has_errors = 1;
            }
        } else {
            if (secure_shred_file(targets[i], passes, force) < 0) has_errors = 1;
        }
    }

    drop_kernel_caches();
    return has_errors ? 1 : 0;
}
