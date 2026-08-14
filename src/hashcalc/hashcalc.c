#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../libutilipc/utilipc.h"

static void hash_file_stream(const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        perror("Error opening file");
        return;
    }

    unsigned char buffer[65536];
    size_t bytes_read = 0;
    uint32_t c32 = 0;
    uint32_t fnv = 2166136261U;
    SHA256_CTX sha_ctx;
    sha256_init(&sha_ctx);

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        c32 = crc32(buffer, bytes_read, c32);
        fnv = fnv1a_32(buffer, bytes_read, fnv);
        sha256_update(&sha_ctx, buffer, bytes_read);
    }
    fclose(fp);

    uint8_t sha[32];
    sha256_final(&sha_ctx, sha);

    printf("======================\n");
    printf("[Target File: %s]\n", filepath);
    printf("======================\n");
    printf("  CRC32:   %08X\n", c32);
    printf("  FNV-1a:  %08X\n", fnv);
    printf("  SHA-256: ");
    for (int i = 0; i < 32; i++) printf("%02x", sha[i]);
    printf("\n======================\n");

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "hashcalc: hashed file %s", filepath);
    utilipc_write_status(-1, -1, -1, log_msg);
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 2) {
        printf("Usage: %s -s \"string\" OR %s -f <file>\n", argv[0], argv[0]);
        utilipc_close();
        return 1;
    }

    if (strcmp(argv[1], "-f") == 0 && argc > 2) {
        hash_file_stream(argv[2]);
    } else if (strcmp(argv[1], "-s") == 0 && argc > 2) {
        const unsigned char *data = (const unsigned char *)argv[2];
        size_t len = strlen(argv[2]);

        uint32_t c32 = crc32(data, len, 0);
        uint32_t fnv = fnv1a_32(data, len, 2166136261U);
        SHA256_CTX sha_ctx;
        uint8_t sha[32];
        sha256_init(&sha_ctx);
        sha256_update(&sha_ctx, data, len);
        sha256_final(&sha_ctx, sha);

        printf("======================\n");
        printf("[Target String: %s]\n", argv[2]);
        printf("======================\n");
        printf("  CRC32:   %08X\n", c32);
        printf("  FNV-1a:  %08X\n", fnv);
        printf("  SHA-256: ");
        for (int i = 0; i < 32; i++) printf("%02x", sha[i]);
        printf("\n======================\n");

        char log_msg[UTILIPC_MAX_MSG];
        snprintf(log_msg, sizeof(log_msg), "hashcalc: hashed string");
        utilipc_write_status(-1, -1, -1, log_msg);
    } else {
        hash_file_stream(argv[1]);
    }

    utilipc_close();
    return 0;
}
