#ifndef UTILIPC_H
#define UTILIPC_H

#include <time.h>
#include <stddef.h>
#include <stdint.h>

#define UTILIPC_SHM_NAME "/utils_ipc_shm"
#define UTILIPC_MAX_MSG 256

typedef struct {
    double ram_used_mb;
    double ram_total_mb;
    double cpu_load1;
    char last_action[UTILIPC_MAX_MSG];
    time_t last_updated;
    unsigned int total_ipc_calls;
} utilipc_data_t;

int utilipc_init(void);
int utilipc_write_status(double ram_used, double ram_total, double load1, const char *action);
int utilipc_read_status(utilipc_data_t *out_data);
void utilipc_close(void);

/* --- Shared Crypto --- */
uint32_t crc32(const unsigned char *data, size_t len, uint32_t current_crc);
uint32_t fnv1a_32(const unsigned char *data, size_t len, uint32_t current_hash);

typedef struct {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} SHA256_CTX;

void sha256_init(SHA256_CTX *ctx);
void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len);
void sha256_final(SHA256_CTX *ctx, uint8_t hash[]);

#endif
