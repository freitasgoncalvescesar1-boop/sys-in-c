#define _GNU_SOURCE
#define _DEFAULT_SOURCE

#include "utilipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

typedef struct {
    pthread_mutex_t lock;
    utilipc_data_t data;
} utilipc_shm_t;

static int shm_fd = -1;
static utilipc_shm_t *shm_ptr = NULL;

static const char *get_shm_path(void) {
    static char full_path[512];
    const char *tmp = getenv("TMPDIR");
    if (!tmp || strlen(tmp) == 0) tmp = "/tmp";
    snprintf(full_path, sizeof(full_path), "%s/utils_ipc_shm", tmp);
    return full_path;
}

int utilipc_init(void) {
    if (shm_ptr && shm_ptr != MAP_FAILED) return 0;

    int created = 0;
    const char *path = get_shm_path();

    shm_fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (shm_fd >= 0) {
        created = 1;
        if (ftruncate(shm_fd, sizeof(utilipc_shm_t)) < 0) {
            close(shm_fd);
            shm_fd = -1;
            return -1;
        }
    } else {
        shm_fd = open(path, O_RDWR);
        if (shm_fd < 0) return -1;
    }

    shm_ptr = mmap(NULL, sizeof(utilipc_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        close(shm_fd);
        shm_fd = -1;
        return -1;
    }

    if (created) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&shm_ptr->lock, &attr);
        pthread_mutexattr_destroy(&attr);

        memset(&shm_ptr->data, 0, sizeof(utilipc_data_t));
        strncpy(shm_ptr->data.last_action, "SHM Initialized", UTILIPC_MAX_MSG - 1);
        shm_ptr->data.last_updated = time(NULL);
    }
    return 0;
}

int utilipc_write_status(double ram_used, double ram_total, double load1, const char *action) {
    if (!shm_ptr) if (utilipc_init() < 0) return -1;

    pthread_mutex_lock(&shm_ptr->lock);
    if (ram_used > 0) shm_ptr->data.ram_used_mb = ram_used;
    if (ram_total > 0) shm_ptr->data.ram_total_mb = ram_total;
    if (load1 >= 0) shm_ptr->data.cpu_load1 = load1;
    if (action && strlen(action) > 0) {
        strncpy(shm_ptr->data.last_action, action, UTILIPC_MAX_MSG - 1);
        shm_ptr->data.last_action[UTILIPC_MAX_MSG - 1] = '\0';
    }
    shm_ptr->data.last_updated = time(NULL);
    shm_ptr->data.total_ipc_calls++;
    pthread_mutex_unlock(&shm_ptr->lock);
    return 0;
}

int utilipc_read_status(utilipc_data_t *out_data) {
    if (!shm_ptr) if (utilipc_init() < 0) return -1;
    pthread_mutex_lock(&shm_ptr->lock);
    memcpy(out_data, &shm_ptr->data, sizeof(utilipc_data_t));
    pthread_mutex_unlock(&shm_ptr->lock);
    return 0;
}

void utilipc_close(void) {
    if (shm_ptr && shm_ptr != MAP_FAILED) {
        munmap(shm_ptr, sizeof(utilipc_shm_t));
        shm_ptr = NULL;
    }
    if (shm_fd >= 0) {
        close(shm_fd);
        shm_fd = -1;
    }
}

/* --- SHARED CRYPTO IMPLEMENTATION --- */

uint32_t crc32(const unsigned char *data, size_t len, uint32_t current_crc) {
    uint32_t crc = ~current_crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

uint32_t fnv1a_32(const unsigned char *data, size_t len, uint32_t current_hash) {
    uint32_t hash = current_hash;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619U;
    }
    return hash;
}

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

void sha256_init(SHA256_CTX *ctx) {
    ctx->datalen = 0; ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
}

void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len) {
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

void sha256_final(SHA256_CTX *ctx, uint8_t hash[]) {
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
