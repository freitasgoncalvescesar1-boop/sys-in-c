#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_TAG     "\033[1;33m"
#define COLOR_FILE    "\033[1;36m"
#define COLOR_SHRED   "\033[1;31m"

#define KRYPT_MAGIC   "KRYPT20\0"
#define SALT_SIZE     16
#define NONCE_SIZE    12
#define HASH_SIZE     32
#define CHUNK_SZ      65536

#define VAULT_TYPE_FILE 1
#define VAULT_TYPE_DIR  2

// --- MOTOR SHA-256 INTERNO ---
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

#define ROTL(a,b) (((a) << (b)) | ((a) >> (32 - (b))))
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

// Derivação de Chave PBKDF2 (50.000 rounds)
static void derive_key(const char *password, const uint8_t salt[SALT_SIZE], uint8_t key_out[32], uint8_t mac_key_out[32]) {
    uint8_t buffer[64 + SALT_SIZE];
    size_t pass_len = strlen(password);
    if (pass_len > 64) pass_len = 64;

    memcpy(buffer, password, pass_len);
    memcpy(buffer + pass_len, salt, SALT_SIZE);

    SHA256_CTX ctx;
    uint8_t current_hash[32];

    sha256_init(&ctx);
    sha256_update(&ctx, buffer, pass_len + SALT_SIZE);
    sha256_final(&ctx, current_hash);

    for (int i = 0; i < 50000; i++) {
        sha256_init(&ctx);
        sha256_update(&ctx, current_hash, 32);
        sha256_update(&ctx, salt, SALT_SIZE);
        sha256_final(&ctx, current_hash);
    }
    memcpy(key_out, current_hash, 32);

    sha256_init(&ctx);
    sha256_update(&ctx, current_hash, 32);
    sha256_update(&ctx, (const uint8_t *)"INTEGRITY_KEY_2.0", 17);
    sha256_final(&ctx, mac_key_out);
}

// --- CIFRA DE FLUXO CHACHA20 ---
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
        out[i * 4 + 0] = (v >> 0) & 0xFF;
        out[i * 4 + 1] = (v >> 8) & 0xFF;
        out[i * 4 + 2] = (v >> 16) & 0xFF;
        out[i * 4 + 3] = (v >> 24) & 0xFF;
    }
}

static void chacha20_crypt(const uint8_t key[32], const uint8_t nonce[12], uint32_t *counter, uint8_t *data, size_t len) {
    uint32_t k[8], n[3];
    for (int i = 0; i < 8; i++) k[i] = ((uint32_t *)key)[i];
    for (int i = 0; i < 3; i++) n[i] = ((uint32_t *)nonce)[i];

    uint8_t block[64];
    while (len > 0) {
        chacha20_block(k, n, *counter, block);
        (*counter)++;
        size_t chunk = (len > 64) ? 64 : len;
        for (size_t i = 0; i < chunk; i++) data[i] ^= block[i];
        data += chunk;
        len -= chunk;
    }
}

static void get_hidden_password(char *out_pass, size_t max_len) {
    printf("  %sDigite a senha mestre:%s ", COLOR_TAG, COLOR_RESET);
    fflush(stdout);

    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    if (!fgets(out_pass, max_len, stdin)) out_pass[0] = '\0';
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\n");

    size_t l = strlen(out_pass);
    while (l > 0 && (out_pass[l-1] == '\r' || out_pass[l-1] == '\n')) out_pass[--l] = '\0';
}

static void call_rmd_shred(const char *target_path) {
    char rmd_cmd[1024];
    snprintf(rmd_cmd, sizeof(rmd_cmd),
             "./rmd -r -f \"%s\" 2>/dev/null || low-utils/rmd -r -f \"%s\" 2>/dev/null || rmd -r -f \"%s\"",
             target_path, target_path, target_path);

    printf("  %s[SHRED]%s Invocando rmd -r -f para destruição segura da origem '%s'...\n", COLOR_SHRED, COLOR_RESET, target_path);
    (void)!system(rmd_cmd);
}

static void make_parent_dirs_for_file(const char *file_path) {
    char temp[1024];
    strncpy(temp, file_path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    for (char *p = temp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(temp, 0755);
            *p = '/';
        }
    }
}

// Empacota diretório recursivamente para o arquivo temporário
static int pack_directory_tree(int out_tmp_fd, const char *base_dir, const char *rel_prefix) {
    DIR *dir = opendir(base_dir);
    if (!dir) return -1;

    struct dirent *de;
    char full_path[2048];
    char rel_path[2048];

    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", base_dir, de->d_name);
        if (strlen(rel_prefix) > 0) {
            snprintf(rel_path, sizeof(rel_path), "%s/%s", rel_prefix, de->d_name);
        } else {
            snprintf(rel_path, sizeof(rel_path), "%s", de->d_name);
        }

        struct stat st;
        if (lstat(full_path, &st) != 0) continue;

        uint16_t path_len = (uint16_t)strlen(rel_path);
        uint8_t is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
        uint32_t mode = (uint32_t)st.st_mode;
        uint64_t fsize = is_dir ? 0 : (uint64_t)st.st_size;

        write(out_tmp_fd, &path_len, sizeof(path_len));
        write(out_tmp_fd, rel_path, path_len);
        write(out_tmp_fd, &is_dir, sizeof(is_dir));
        write(out_tmp_fd, &mode, sizeof(mode));
        write(out_tmp_fd, &fsize, sizeof(fsize));

        if (is_dir) {
            pack_directory_tree(out_tmp_fd, full_path, rel_path);
        } else {
            int in_fd = open(full_path, O_RDONLY);
            if (in_fd >= 0) {
                char chunk[CHUNK_SZ];
                ssize_t n;
                while ((n = read(in_fd, chunk, sizeof(chunk))) > 0) {
                    write(out_tmp_fd, chunk, n);
                }
                close(in_fd);
            }
        }
    }
    closedir(dir);
    return 0;
}

static int encrypt_vault(const char *in_target, const char *out_vault, const char *pass, int auto_shred) {
    struct stat st;
    if (lstat(in_target, &st) != 0) {
        fprintf(stderr, "  %s[ERRO]%s Alvo '%s' nao encontrado: %s\n", COLOR_ERR, COLOR_RESET, in_target, strerror(errno));
        return -1;
    }

    int is_dir = S_ISDIR(st.st_mode);
    uint8_t vault_type = is_dir ? VAULT_TYPE_DIR : VAULT_TYPE_FILE;

    const char *tmp_dir = getenv("TMPDIR");
    if (!tmp_dir || strlen(tmp_dir) == 0) tmp_dir = "/tmp";
    char payload_tmp[512];
    snprintf(payload_tmp, sizeof(payload_tmp), "%s/krypt_tmp_%d.bin", tmp_dir, getpid());

    int tmp_fd = open(payload_tmp, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (tmp_fd < 0) {
        fprintf(stderr, "  %s[ERRO]%s Falha ao criar arquivo temporario em %s\n", COLOR_ERR, COLOR_RESET, payload_tmp);
        return -1;
    }

    write(tmp_fd, &vault_type, sizeof(vault_type));

    if (is_dir) {
        printf("  Empacotando arvore de pastas de '%s'...\n", in_target);
        pack_directory_tree(tmp_fd, in_target, "");
        uint16_t end_mark = 0;
        write(tmp_fd, &end_mark, sizeof(end_mark));
    } else {
        int in_fd = open(in_target, O_RDONLY);
        if (in_fd < 0) {
            close(tmp_fd);
            unlink(payload_tmp);
            return -1;
        }
        char chunk[CHUNK_SZ];
        ssize_t n;
        while ((n = read(in_fd, chunk, sizeof(chunk))) > 0) {
            write(tmp_fd, chunk, n);
        }
        close(in_fd);
    }

    off_t payload_size = lseek(tmp_fd, 0, SEEK_CUR);
    lseek(tmp_fd, 0, SEEK_SET);

    // Gera Salt e Nonce aleatórios
    uint8_t salt[SALT_SIZE];
    uint8_t nonce[NONCE_SIZE];
    int rand_fd = open("/dev/urandom", O_RDONLY);
    if (rand_fd < 0 || read(rand_fd, salt, SALT_SIZE) != SALT_SIZE || read(rand_fd, nonce, NONCE_SIZE) != NONCE_SIZE) {
        if (rand_fd >= 0) close(rand_fd);
        close(tmp_fd);
        unlink(payload_tmp);
        return -1;
    }
    close(rand_fd);

    uint8_t key[32], mac_key[32];
    derive_key(pass, salt, key, mac_key);

    int out_fd = open(out_vault, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (out_fd < 0) {
        fprintf(stderr, "  %s[ERRO]%s Falha ao criar cofre '%s': %s\n", COLOR_ERR, COLOR_RESET, out_vault, strerror(errno));
        close(tmp_fd);
        unlink(payload_tmp);
        return -1;
    }

    write(out_fd, KRYPT_MAGIC, 8);
    write(out_fd, salt, SALT_SIZE);
    write(out_fd, nonce, NONCE_SIZE);

    SHA256_CTX mac_ctx;
    sha256_init(&mac_ctx);
    sha256_update(&mac_ctx, mac_key, 32);

    uint8_t chunk[CHUNK_SZ];
    uint32_t counter = 1;
    ssize_t n = 0;

    while ((n = read(tmp_fd, chunk, sizeof(chunk))) > 0) {
        chacha20_crypt(key, nonce, &counter, chunk, n);
        sha256_update(&mac_ctx, chunk, n);
        write(out_fd, chunk, n);
    }

    uint8_t hmac[32];
    sha256_final(&mac_ctx, hmac);
    write(out_fd, hmac, 32);

    close(tmp_fd);
    unlink(payload_tmp);
    close(out_fd);

    printf("  %s[✔ COFRE CRIADO]%s '%s' (%s | ChaCha20 + HMAC | %lld bytes)\n",
           COLOR_OK, COLOR_RESET, out_vault, is_dir ? "Diretório" : "Arquivo Único", (long long)payload_size);

    if (auto_shred) {
        call_rmd_shred(in_target);
    }

    return 0;
}

static int decrypt_vault(const char *in_vault, const char *out_target, const char *pass) {
    int in_fd = open(in_vault, O_RDONLY);
    if (in_fd < 0) {
        fprintf(stderr, "  %s[ERRO]%s Nao foi possivel abrir '%s': %s\n", COLOR_ERR, COLOR_RESET, in_vault, strerror(errno));
        return -1;
    }

    char magic[8];
    uint8_t salt[SALT_SIZE];
    uint8_t nonce[NONCE_SIZE];

    if (read(in_fd, magic, 8) != 8 || strcmp(magic, KRYPT_MAGIC) != 0) {
        fprintf(stderr, "  %s[ERRO]%s O arquivo '%s' nao e um cofre valido do krypt 2.0!\n", COLOR_ERR, COLOR_RESET, in_vault);
        close(in_fd);
        return -1;
    }

    read(in_fd, salt, SALT_SIZE);
    read(in_fd, nonce, NONCE_SIZE);

    uint8_t key[32], mac_key[32];
    derive_key(pass, salt, key, mac_key);

    off_t total_sz = lseek(in_fd, 0, SEEK_END);
    off_t payload_sz = total_sz - 8 - SALT_SIZE - NONCE_SIZE - 32;

    if (payload_sz < 0) {
        fprintf(stderr, "  %s[ERRO]%s Cofre truncado ou danificado!\n", COLOR_ERR, COLOR_RESET);
        close(in_fd);
        return -1;
    }

    // 1. Verificação Estrita de Integridade e Senha (AEAD)
    lseek(in_fd, 8 + SALT_SIZE + NONCE_SIZE, SEEK_SET);
    SHA256_CTX mac_ctx;
    sha256_init(&mac_ctx);
    sha256_update(&mac_ctx, mac_key, 32);

    uint8_t chunk[CHUNK_SZ];
    off_t remaining = payload_sz;
    while (remaining > 0) {
        size_t to_r = (remaining > (off_t)sizeof(chunk)) ? sizeof(chunk) : remaining;
        ssize_t n = read(in_fd, chunk, to_r);
        if (n <= 0) break;
        sha256_update(&mac_ctx, chunk, n);
        remaining -= n;
    }

    uint8_t calc_hmac[32], file_hmac[32];
    sha256_final(&mac_ctx, calc_hmac);
    read(in_fd, file_hmac, 32);

    if (memcmp(calc_hmac, file_hmac, 32) != 0) {
        fprintf(stderr, "\n  %s[ERRO DE AUTENTICACAO]%s Senha incorreta ou cofre adulterado!\n\n", COLOR_ERR, COLOR_RESET);
        close(in_fd);
        return -1;
    }

    // 2. Descriptografia para Arquivo Temporário
    const char *tmp_dir = getenv("TMPDIR");
    if (!tmp_dir || strlen(tmp_dir) == 0) tmp_dir = "/tmp";
    char dec_tmp[512];
    snprintf(dec_tmp, sizeof(dec_tmp), "%s/krypt_dec_%d.bin", tmp_dir, getpid());

    int tmp_fd = open(dec_tmp, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (tmp_fd < 0) {
        close(in_fd);
        return -1;
    }

    lseek(in_fd, 8 + SALT_SIZE + NONCE_SIZE, SEEK_SET);
    uint32_t counter = 1;
    remaining = payload_sz;
    while (remaining > 0) {
        size_t to_r = (remaining > (off_t)sizeof(chunk)) ? sizeof(chunk) : remaining;
        ssize_t n = read(in_fd, chunk, to_r);
        if (n <= 0) break;
        chacha20_crypt(key, nonce, &counter, chunk, n);
        write(tmp_fd, chunk, n);
        remaining -= n;
    }
    close(in_fd);

    // 3. Desempacotamento de Arquivo ou Diretório
    lseek(tmp_fd, 0, SEEK_SET);
    uint8_t vault_type = 0;
    read(tmp_fd, &vault_type, sizeof(vault_type));

    if (vault_type == VAULT_TYPE_FILE) {
        int out_fd = open(out_target, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd < 0) {
            close(tmp_fd);
            unlink(dec_tmp);
            return -1;
        }
        ssize_t n;
        while ((n = read(tmp_fd, chunk, sizeof(chunk))) > 0) {
            write(out_fd, chunk, n);
        }
        close(out_fd);
        printf("  %s[✔ ARQUIVO RECUPERADO]%s '%s'\n", COLOR_OK, COLOR_RESET, out_target);
    } else if (vault_type == VAULT_TYPE_DIR) {
        mkdir(out_target, 0755);
        printf("  Restaurando arvore de pastas em '%s/'...\n", out_target);

        while (1) {
            uint16_t path_len = 0;
            if (read(tmp_fd, &path_len, sizeof(path_len)) <= 0 || path_len == 0) break;

            char rel_path[1024];
            read(tmp_fd, rel_path, path_len);
            rel_path[path_len] = '\0';

            uint8_t is_dir = 0;
            uint32_t mode = 0;
            uint64_t fsize = 0;

            read(tmp_fd, &is_dir, sizeof(is_dir));
            read(tmp_fd, &mode, sizeof(mode));
            read(tmp_fd, &fsize, sizeof(fsize));

            char dest_item_path[2048];
            snprintf(dest_item_path, sizeof(dest_item_path), "%s/%s", out_target, rel_path);

            if (is_dir) {
                mkdir(dest_item_path, (mode_t)mode);
            } else {
                make_parent_dirs_for_file(dest_item_path);
                int out_f = open(dest_item_path, O_WRONLY | O_CREAT | O_TRUNC, (mode_t)mode);
                if (out_f >= 0) {
                    uint64_t file_rem = fsize;
                    while (file_rem > 0) {
                        size_t to_read = (file_rem > sizeof(chunk)) ? sizeof(chunk) : (size_t)file_rem;
                        ssize_t rn = read(tmp_fd, chunk, to_read);
                        if (rn <= 0) break;
                        write(out_f, chunk, rn);
                        file_rem -= rn;
                    }
                    close(out_f);
                }
            }
        }
        printf("  %s[✔ PASTA RECUPERADA]%s '%s/'\n", COLOR_OK, COLOR_RESET, out_target);
    }

    close(tmp_fd);
    unlink(dec_tmp);
    return 0;
}

static void print_help(void) {
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ krypt 2.0 - ChaCha20 + HMAC Vault & Auto-Shredder (rmd -r -f) ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  krypt -e <ARQUIVO|PASTA> [-s] [-o <COFRE.kr>]    (Criptografar)\n");
    printf("  krypt -d <COFRE.kr> [-o <DESTINO>]               (Descriptografar)\n\n");
    printf("Opcoes:\n");
    printf("  %s-e, --encrypt%s    Criptografar arquivo unico ou diretorio inteiro\n", COLOR_OK, COLOR_RESET);
    printf("  %s-d, --decrypt%s    Descriptografar cofre autenticado\n", COLOR_OK, COLOR_RESET);
    printf("  %s-s, --shred%s      Destruir a origem com rmd -r -f apos criptografar com sucesso\n", COLOR_SHRED, COLOR_RESET);
    printf("  %s-o, --output%s     Especificar nome/caminho de saida personalizado\n", COLOR_TAG, COLOR_RESET);
    printf("  %s--help%s           Exibir esta ajuda formatada\n\n", COLOR_TAG, COLOR_RESET);
    printf("Exemplos:\n");
    printf("  • %skrypt -e segredo.txt%s                     (Gera segredo.txt.kr)\n", COLOR_TAG, COLOR_RESET);
    printf("  • %skrypt -e ./minha_pasta/ -s%s               (Criptografa a pasta e destroi a origem com rmd)\n", COLOR_TAG, COLOR_RESET);
    printf("  • %skrypt -d minha_pasta.kr -o ./restaurado%s  (Restaura toda a pasta e arquivos)\n", COLOR_TAG, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 3 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help();
        utilipc_close();
        return (argc < 3) ? 1 : 0;
    }

    int mode_encrypt = 0, mode_decrypt = 0, auto_shred = 0;
    const char *in_target = NULL;
    char out_target[512] = "";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--encrypt") == 0) {
            mode_encrypt = 1;
            if (i + 1 < argc && argv[i+1][0] != '-') in_target = argv[++i];
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--decrypt") == 0) {
            mode_decrypt = 1;
            if (i + 1 < argc && argv[i+1][0] != '-') in_target = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--shred") == 0) {
            auto_shred = 1;
        } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            strncpy(out_target, argv[++i], sizeof(out_target) - 1);
        } else if (!in_target && argv[i][0] != '-') {
            in_target = argv[i];
        }
    }

    if (!in_target) {
        print_help();
        utilipc_close();
        return 1;
    }

    // Define nomes padrão de saída caso -o não seja fornecido
    if (strlen(out_target) == 0) {
        if (mode_encrypt) {
            char clean_target[512];
            strncpy(clean_target, in_target, sizeof(clean_target) - 1);
            size_t tl = strlen(clean_target);
            while (tl > 1 && clean_target[tl-1] == '/') clean_target[--tl] = '\0';
            snprintf(out_target, sizeof(out_target), "%s.kr", clean_target);
        } else {
            strncpy(out_target, in_target, sizeof(out_target) - 1);
            char *dot = strstr(out_target, ".kr");
            if (dot) *dot = '\0';
            else strcat(out_target, "_dec");
        }
    }

    char pass[128];
    get_hidden_password(pass, sizeof(pass));
    if (strlen(pass) == 0) {
        fprintf(stderr, "krypt: senha nao pode ser vazia!\n");
        utilipc_close();
        return 1;
    }

    int res = 0;
    if (mode_encrypt) {
        res = encrypt_vault(in_target, out_target, pass, auto_shred);
    } else if (mode_decrypt) {
        res = decrypt_vault(in_target, out_target, pass);
    }

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "krypt: %s '%s' (shred: %d)", mode_encrypt ? "encrypted" : "decrypted", in_target, auto_shred);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return res;
}
