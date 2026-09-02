#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_TAG     "\033[1;33m"
#define COLOR_VAL     "\033[1;36m"
#define COLOR_MUTED   "\033[0;90m"

// --- TABELAS RFC 4648 ---
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char b32_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static void print_help(const char *prog) {
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ b64 - Base64 & Base32 Codec Engine (RFC 4648) ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  %s -e [OPTIONS] <STRING | -f FILE>    (Codificar)\n", prog);
    printf("  %s -d [OPTIONS] <STRING | -f FILE>    (Decodificar)\n", prog);
    printf("  echo \"texto\" | %s -e [OPTIONS]        (Stream via Pipe)\n\n", prog);
    printf("Options:\n");
    printf("  -64, --b64, --base64     Usar padrao Base64 [Padrao]\n");
    printf("  -32, --b32, --base32     Usar padrao Base32 (RFC 4648 A-Z, 2-7)\n");
    printf("  -f <FILE>                Ler dados de um arquivo em vez de string\n");
    printf("  -h, --help               Exibir esta ajuda formatada\n\n");
    printf("Exemplos:\n");
    printf("  • %s -e \"Mensagem Secreta\"\n", prog);
    printf("  • %s -e -32 \"Mensagem Secreta\"         (Codifica em Base32)\n", prog);
    printf("  • %s -d -32 \"JVUWY3BAMNWGSZDTEB2GQ2DBHU======\"\n", prog);
    printf("  • %s -e -f foto.png -o foto.b64\n", prog);
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

// --- BASE64 ---
static char *base64_encode(const unsigned char *data, size_t len, size_t *out_len) {
    *out_len = 4 * ((len + 2) / 3);
    char *out = malloc(*out_len + 1);
    if (!out) return NULL;

    for (size_t i = 0, j = 0; i < len;) {
        uint32_t oct_a = (i < len) ? data[i++] : 0;
        uint32_t oct_b = (i < len) ? data[i++] : 0;
        uint32_t oct_c = (i < len) ? data[i++] : 0;
        uint32_t triple = (oct_a << 16) | (oct_b << 8) | oct_c;

        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = b64_table[(triple >> 6) & 0x3F];
        out[j++] = b64_table[triple & 0x3F];
    }

    int mod = len % 3;
    if (mod == 1) { out[*out_len - 1] = '='; out[*out_len - 2] = '='; }
    else if (mod == 2) { out[*out_len - 1] = '='; }
    out[*out_len] = '\0';
    return out;
}

static unsigned char *base64_decode(const char *data, size_t len, size_t *out_len) {
    while (len > 0 && (data[len-1] == '\r' || data[len-1] == '\n' || data[len-1] == ' ')) len--;
    if (len % 4 != 0) return NULL;

    *out_len = (len / 4) * 3;
    if (len > 0 && data[len - 1] == '=') (*out_len)--;
    if (len > 1 && data[len - 2] == '=') (*out_len)--;

    unsigned char *out = malloc(*out_len + 1);
    if (!out) return NULL;

    static int d_table[256];
    static int init = 0;
    if (!init) {
        memset(d_table, 0x80, sizeof(d_table));
        for (int i = 0; i < 64; i++) d_table[(unsigned char)b64_table[i]] = i;
        init = 1;
    }

    for (size_t i = 0, j = 0; i < len;) {
        uint32_t s_a = data[i] == '=' ? 0 : d_table[(unsigned char)data[i]]; i++;
        uint32_t s_b = data[i] == '=' ? 0 : d_table[(unsigned char)data[i]]; i++;
        uint32_t s_c = data[i] == '=' ? 0 : d_table[(unsigned char)data[i]]; i++;
        uint32_t s_d = data[i] == '=' ? 0 : d_table[(unsigned char)data[i]]; i++;

        uint32_t triple = (s_a << 18) | (s_b << 12) | (s_c << 6) | s_d;

        if (j < *out_len) out[j++] = (triple >> 16) & 0xFF;
        if (j < *out_len) out[j++] = (triple >> 8) & 0xFF;
        if (j < *out_len) out[j++] = triple & 0xFF;
    }
    out[*out_len] = '\0';
    return out;
}

// --- BASE32 (RFC 4648) ---
static char *base32_encode(const unsigned char *data, size_t len, size_t *out_len) {
    *out_len = ((len + 4) / 5) * 8;
    char *out = malloc(*out_len + 1);
    if (!out) return NULL;

    size_t o = 0;
    for (size_t i = 0; i < len; i += 5) {
        uint64_t b = 0;
        int chunk_len = (len - i > 5) ? 5 : (int)(len - i);

        for (int k = 0; k < 5; k++) {
            uint64_t val = (k < chunk_len) ? data[i + k] : 0;
            b = (b << 8) | val;
        }

        // 40 bits divididos em 8 blocos de 5 bits
        for (int k = 7; k >= 0; k--) {
            int bit_idx = k * 5;
            uint8_t index = (uint8_t)((b >> bit_idx) & 0x1F);
            out[o++] = b32_table[index];
        }

        // Adiciona padding '=' baseado no tamanho do bloco restante
        if (chunk_len == 1) {
            out[o - 6] = '='; out[o - 5] = '='; out[o - 4] = '=';
            out[o - 3] = '='; out[o - 2] = '='; out[o - 1] = '=';
        } else if (chunk_len == 2) {
            out[o - 4] = '='; out[o - 3] = '='; out[o - 2] = '='; out[o - 1] = '=';
        } else if (chunk_len == 3) {
            out[o - 3] = '='; out[o - 2] = '='; out[o - 1] = '=';
        } else if (chunk_len == 4) {
            out[o - 1] = '=';
        }
    }
    out[o] = '\0';
    return out;
}

static unsigned char *base32_decode(const char *data, size_t len, size_t *out_len) {
    while (len > 0 && (data[len-1] == '\r' || data[len-1] == '\n' || data[len-1] == ' ')) len--;
    if (len % 8 != 0) return NULL;

    int pad_count = 0;
    for (int k = 1; k <= 6 && len >= (size_t)k; k++) {
        if (data[len - k] == '=') pad_count++;
        else break;
    }

    size_t full_blocks = len / 8;
    *out_len = full_blocks * 5;
    if (pad_count == 6) *out_len -= 4;
    else if (pad_count == 4) *out_len -= 3;
    else if (pad_count == 3) *out_len -= 2;
    else if (pad_count == 1) *out_len -= 1;

    unsigned char *out = malloc(*out_len + 1);
    if (!out) return NULL;

    static int d32[256];
    static int init32 = 0;
    if (!init32) {
        memset(d32, -1, sizeof(d32));
        for (int i = 0; i < 32; i++) {
            d32[(unsigned char)b32_table[i]] = i;
            d32[tolower((unsigned char)b32_table[i])] = i;
        }
        init32 = 1;
    }

    size_t o = 0;
    for (size_t i = 0; i < len; i += 8) {
        uint64_t b = 0;
        for (int k = 0; k < 8; k++) {
            char c = data[i + k];
            int val = (c == '=') ? 0 : d32[(unsigned char)c];
            if (val < 0) { free(out); return NULL; }
            b = (b << 5) | (uint64_t)val;
        }

        for (int k = 4; k >= 0; k--) {
            if (o < *out_len) {
                out[o++] = (uint8_t)((b >> (k * 8)) & 0xFF);
            }
        }
    }
    out[*out_len] = '\0';
    return out;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }

    int mode_encode = 1; // 1 = encode, 0 = decode
    int use_base32 = 0;  // 0 = b64, 1 = b32
    const char *input_file = NULL;
    const char *raw_str = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--encode") == 0) mode_encode = 1;
        else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--decode") == 0) mode_encode = 0;
        else if (strcmp(argv[i], "-32") == 0 || strcmp(argv[i], "--b32") == 0 || strcmp(argv[i], "--base32") == 0) use_base32 = 1;
        else if (strcmp(argv[i], "-64") == 0 || strcmp(argv[i], "--b64") == 0 || strcmp(argv[i], "--base64") == 0) use_base32 = 0;
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) input_file = argv[++i];
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) raw_str = argv[++i];
        else if (!raw_str && argv[i][0] != '-') raw_str = argv[i];
    }

    unsigned char *data_in = NULL;
    size_t data_len = 0;

    if (input_file) {
        FILE *fp = fopen(input_file, "rb");
        if (!fp) {
            fprintf(stderr, "b64: erro ao abrir arquivo '%s': %s\n", input_file, strerror(errno));
            return 1;
        }
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (sz < 0) { fclose(fp); return 1; }

        data_in = malloc(sz + 1);
        if (data_in) {
            data_len = fread(data_in, 1, sz, fp);
            data_in[data_len] = '\0';
        }
        fclose(fp);
    } else if (raw_str) {
        data_len = strlen(raw_str);
        data_in = malloc(data_len + 1);
        if (data_in) memcpy(data_in, raw_str, data_len + 1);
    } else {
        // Leitura de Pipe / STDIN
        if (isatty(STDIN_FILENO)) {
            print_help(argv[0]);
            return 0;
        }
        size_t cap = 4096;
        data_in = malloc(cap);
        size_t n = 0;
        int ch;
        while ((ch = getchar()) != EOF) {
            if (n + 2 >= cap) { cap *= 2; data_in = realloc(data_in, cap); }
            data_in[n++] = (unsigned char)ch;
        }
        data_in[n] = '\0';
        data_len = n;
    }

    if (!data_in || data_len == 0) {
        if (data_in) free(data_in);
        return 0;
    }

    size_t out_len = 0;
    if (mode_encode) {
        char *res = use_base32 ? base32_encode(data_in, data_len, &out_len)
                               : base64_encode(data_in, data_len, &out_len);
        if (res) {
            printf("\n%s╭────────────────────────────────────────────────────────────╮%s\n", COLOR_TITLE, COLOR_RESET);
            printf("%s│%s  %s[ %s ENCODED ]%s (%zu bytes in -> %zu bytes out)%*s %s│%s\n",
                   COLOR_TITLE, COLOR_RESET, COLOR_OK, use_base32 ? "BASE32" : "BASE64", COLOR_RESET,
                   data_len, out_len, (int)(25 - snprintf(NULL, 0, "%zu%zu", data_len, out_len)), "", COLOR_TITLE, COLOR_RESET);
            printf("%s├────────────────────────────────────────────────────────────┤%s\n", COLOR_TITLE, COLOR_RESET);
            printf("  %s%s%s\n", COLOR_VAL, res, COLOR_RESET);
            printf("%s╰────────────────────────────────────────────────────────────╯%s\n\n", COLOR_TITLE, COLOR_RESET);
            free(res);
        }
    } else {
        unsigned char *res = use_base32 ? base32_decode((const char *)data_in, data_len, &out_len)
                                       : base64_decode((const char *)data_in, data_len, &out_len);
        if (res) {
            printf("\n%s╭────────────────────────────────────────────────────────────╮%s\n", COLOR_TITLE, COLOR_RESET);
            printf("%s│%s  %s[ %s DECODED ]%s (%zu bytes out)%*s %s│%s\n",
                   COLOR_TITLE, COLOR_RESET, COLOR_OK, use_base32 ? "BASE32" : "BASE64", COLOR_RESET,
                   out_len, (int)(32 - snprintf(NULL, 0, "%zu", out_len)), "", COLOR_TITLE, COLOR_RESET);
            printf("%s├────────────────────────────────────────────────────────────┤%s\n", COLOR_TITLE, COLOR_RESET);
            printf("  %s%s%s\n", COLOR_VAL, res, COLOR_RESET);
            printf("%s╰────────────────────────────────────────────────────────────╯%s\n\n", COLOR_TITLE, COLOR_RESET);
            free(res);
        } else {
            fprintf(stderr, "\n  %s[ERRO]: String %s inválida ou corrompida!%s\n\n",
                    COLOR_ERR, use_base32 ? "Base32" : "Base64", COLOR_RESET);
        }
    }

    free(data_in);
    return 0;
}
