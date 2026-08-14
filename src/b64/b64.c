#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *b64_encode(const unsigned char *data, size_t input_length, size_t *output_length) {
    *output_length = 4 * ((input_length + 2) / 3);
    char *encoded_data = malloc(*output_length + 1);
    if (encoded_data == NULL) return NULL;

    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        encoded_data[j++] = b64_table[(triple >> 18) & 0x3F];
        encoded_data[j++] = b64_table[(triple >> 12) & 0x3F];
        encoded_data[j++] = b64_table[(triple >> 6) & 0x3F];
        encoded_data[j++] = b64_table[triple & 0x3F];
    }

    int mod = input_length % 3;
    if (mod == 1) {
        encoded_data[*output_length - 1] = '=';
        encoded_data[*output_length - 2] = '=';
    } else if (mod == 2) {
        encoded_data[*output_length - 1] = '=';
    }

    encoded_data[*output_length] = '\0';
    return encoded_data;
}

static unsigned char *b64_decode(const char *data, size_t input_length, size_t *output_length) {
    if (input_length % 4 != 0) return NULL;

    *output_length = input_length / 4 * 3;
    if (data[input_length - 1] == '=') (*output_length)--;
    if (data[input_length - 2] == '=') (*output_length)--;

    unsigned char *decoded_data = malloc(*output_length + 1);
    if (decoded_data == NULL) return NULL;

    static int d_table[256];
    memset(d_table, 0x80, sizeof(d_table));
    for (int i = 0; i < 64; i++) d_table[(unsigned char)b64_table[i]] = i;

    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t sextet_a = data[i] == '=' ? 0 & i++ : d_table[(unsigned char)data[i++]];
        uint32_t sextet_b = data[i] == '=' ? 0 & i++ : d_table[(unsigned char)data[i++]];
        uint32_t sextet_c = data[i] == '=' ? 0 & i++ : d_table[(unsigned char)data[i++]];
        uint32_t sextet_d = data[i] == '=' ? 0 & i++ : d_table[(unsigned char)data[i++]];

        uint32_t triple = (sextet_a << 18) + (sextet_b << 12) + (sextet_c << 6) + sextet_d;

        if (j < *output_length) decoded_data[j++] = (triple >> 16) & 0xFF;
        if (j < *output_length) decoded_data[j++] = (triple >> 8) & 0xFF;
        if (j < *output_length) decoded_data[j++] = triple & 0xFF;
    }

    decoded_data[*output_length] = '\0';
    return decoded_data;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage:\n  %s -e \"string\"\n  %s -d \"base64_string\"\n", argv[0], argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-e") == 0) {
        size_t out_len;
        char *res = b64_encode((const unsigned char *)argv[2], strlen(argv[2]), &out_len);
        if (res) {
            printf("======================\n");
            printf("[Encoded]: %s\n", res);
            printf("======================\n");
            free(res);
        }
    } else if (strcmp(argv[1], "-d") == 0) {
        size_t out_len;
        unsigned char *res = b64_decode(argv[2], strlen(argv[2]), &out_len);
        if (res) {
            printf("======================\n");
            printf("[Decoded]: %s\n", res);
            printf("======================\n");
            free(res);
        } else {
            printf("Error: Invalid Base64 string.\n");
        }
    }

    return 0;
}
