#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include "../libutilipc/utilipc.h"

#define MAX_DEPTH 15
#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_GROUP   "\033[1;33m"
#define COLOR_PATH    "\033[0;36m"
#define COLOR_SIZE    "\033[1;32m"

// --- SHA-256 ---
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

#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))
#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

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
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
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
    ctx->data[57] = ctx->bitlen >> 56;
    ctx->data[58] = ctx->bitlen >> 48;
    ctx->data[59] = ctx->bitlen >> 40;
    ctx->data[60] = ctx->bitlen >> 32;
    ctx->data[61] = ctx->bitlen >> 24;
    ctx->data[62] = ctx->bitlen >> 16;
    ctx->data[63] = ctx->bitlen >> 8;
    ctx->data[64 - 1] = ctx->bitlen;
    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4; ++i) {
        hash[i]      = (ctx->state[0] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 4]  = (ctx->state[1] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 8]  = (ctx->state[2] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 20] = (ctx->state[5] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 24] = (ctx->state[6] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 28] = (ctx->state[7] >> (24 - i * 8)) & 0x000000ff;
    }
}

static int compute_file_sha256(const char *filepath, uint8_t hash_out[32]) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return -1;

    SHA256_CTX ctx;
    sha256_init(&ctx);

    unsigned char buf[65536];
    size_t bytes = 0;
    while ((bytes = fread(buf, 1, sizeof(buf), fp)) > 0) {
        sha256_update(&ctx, buf, bytes);
    }
    fclose(fp);

    sha256_final(&ctx, hash_out);
    return 0;
}

typedef struct {
    char path[2048];
    off_t size;
    uint8_t hash[32];
    int hash_computed;
    int group_id;
} FileEntry;

static FileEntry *files = NULL;
static size_t file_count = 0;
static size_t file_capacity = 0;

static const char *ignored_dirs[] = {
    ".git", "node_modules", "vendor", ".cache", ".vscode", ".idea", "build", "dist",
    "/proc", "/sys", "/dev", "/system", "/data", NULL
};

static int is_ignored(const char *name) {
    for (int i = 0; ignored_dirs[i] != NULL; i++) {
        if (strcmp(name, ignored_dirs[i]) == 0 || strncmp(name, ignored_dirs[i], strlen(ignored_dirs[i])) == 0) {
            return 1;
        }
    }
    return 0;
}

static void add_file(const char *path, off_t size) {
    if (file_count >= file_capacity) {
        file_capacity = (file_capacity == 0) ? 128 : file_capacity * 2;
        files = realloc(files, file_capacity * sizeof(FileEntry));
    }
    strncpy(files[file_count].path, path, sizeof(files[file_count].path) - 1);
    files[file_count].path[sizeof(files[file_count].path) - 1] = '\0';
    files[file_count].size = size;
    files[file_count].hash_computed = 0;
    files[file_count].group_id = -1;
    file_count++;
}

static void scan_recursive(const char *dir_path, int depth) {
    if (depth > MAX_DEPTH) return;
    if (is_ignored(dir_path)) return;

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    char path[2048];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (entry->d_name[0] == '.') continue;
        if (is_ignored(entry->d_name)) continue;

        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

#ifdef DT_DIR
        if (entry->d_type == DT_DIR) {
            scan_recursive(path, depth + 1);
            continue;
        } else if (entry->d_type == DT_REG) {
            struct stat st;
            if (stat(path, &st) == 0 && st.st_size > 0) {
                add_file(path, st.st_size);
            }
            continue;
        }
#endif

        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                scan_recursive(path, depth + 1);
            } else if (S_ISREG(st.st_mode) && st.st_size > 0) {
                add_file(path, st.st_size);
            }
        }
    }
    closedir(dir);
}

static int compare_by_size(const void *a, const void *b) {
    const FileEntry *fa = (const FileEntry *)a;
    const FileEntry *fb = (const FileEntry *)b;
    if (fb->size > fa->size) return 1;
    if (fb->size < fa->size) return -1;
    return 0;
}

int main(int argc, char *argv[]) {
    utilipc_init();

    const char *start_dir = ".";
    if (argc >= 2) start_dir = argv[1];

    printf("%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ fdup - Duplicate File Finder ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("  Scanning directory: %s...\n", start_dir);

    scan_recursive(start_dir, 1);

    if (file_count == 0) {
        printf("No files found.\n");
        free(files);
        utilipc_close();
        return 0;
    }

    qsort(files, file_count, sizeof(FileEntry), compare_by_size);

    int current_group = 0;
    size_t total_wasted_bytes = 0;

    for (size_t i = 0; i < file_count; i++) {
        if (files[i].group_id != -1) continue;

        size_t match_count = 0;
        for (size_t j = i + 1; j < file_count; j++) {
            if (files[j].size != files[i].size) break;
            match_count++;
        }

        if (match_count == 0) continue;

        if (!files[i].hash_computed) {
            if (compute_file_sha256(files[i].path, files[i].hash) < 0) continue;
            files[i].hash_computed = 1;
        }

        int group_found = 0;
        for (size_t j = i + 1; j < file_count; j++) {
            if (files[j].size != files[i].size) break;

            if (!files[j].hash_computed) {
                if (compute_file_sha256(files[j].path, files[j].hash) < 0) continue;
                files[j].hash_computed = 1;
            }

            if (memcmp(files[i].hash, files[j].hash, 32) == 0) {
                if (!group_found) {
                    current_group++;
                    files[i].group_id = current_group;
                    group_found = 1;

                    double size_mb = (double)files[i].size / (1024.0 * 1024.0);
                    printf("\n%s[Group %d - Size: %s%.2f MB%s | SHA256: ", COLOR_GROUP, current_group, COLOR_SIZE, size_mb, COLOR_GROUP);
                    for (int h = 0; h < 8; h++) printf("%02x", files[i].hash[h]);
                    printf("...]%s\n", COLOR_RESET);
                    printf("  • %s%s%s\n", COLOR_PATH, files[i].path, COLOR_RESET);
                }

                files[j].group_id = current_group;
                printf("  • %s%s%s\n", COLOR_PATH, files[j].path, COLOR_RESET);
                total_wasted_bytes += files[i].size;
            }
        }
    }

    printf("\n%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);
    double wasted_mb = (double)total_wasted_bytes / (1024.0 * 1024.0);
    if (wasted_mb >= 1024.0) {
        printf("[Done. Duplicate Groups: %d | Wasted Space: %s%.2f GB%s]\n", current_group, COLOR_SIZE, wasted_mb / 1024.0, COLOR_RESET);
    } else {
        printf("[Done. Duplicate Groups: %d | Wasted Space: %s%.2f MB%s]\n", current_group, COLOR_SIZE, wasted_mb, COLOR_RESET);
    }
    printf("%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "fdup: found %d duplicate groups (wasted: %.1fMB)", current_group, wasted_mb);
    utilipc_write_status(-1, -1, -1, log_msg);

    free(files);
    utilipc_close();
    return 0;
}
