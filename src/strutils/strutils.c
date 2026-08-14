#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../libutilipc/utilipc.h"

static void to_snake_case(const char *in) {
    size_t max_len = strlen(in) * 2 + 1;
    char *out = malloc(max_len);
    if (!out) return;
    
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j < max_len - 2; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c)) {
            if (i > 0 && isupper(c) && islower((unsigned char)in[i-1])) {
                out[j++] = '_';
            }
            out[j++] = tolower(c);
        } else if (c == ' ' || c == '-' || c == '_') {
            if (j > 0 && out[j-1] != '_') out[j++] = '_';
        }
    }
    out[j] = '\0';
    printf("%s\n", out);
    free(out);
}

static void to_camel_case(const char *in) {
    size_t max_len = strlen(in) + 1;
    char *out = malloc(max_len);
    if (!out) return;
    
    size_t j = 0;
    int capitalize_next = 0;
    for (size_t i = 0; in[i] != '\0' && j < max_len - 1; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c)) {
            if (capitalize_next && j > 0) {
                out[j++] = toupper(c);
                capitalize_next = 0;
            } else if (j == 0) {
                out[j++] = tolower(c);
            } else {
                out[j++] = c;
            }
        } else if (c == ' ' || c == '-' || c == '_') {
            capitalize_next = 1;
        }
    }
    out[j] = '\0';
    printf("%s\n", out);
    free(out);
}

static void url_encode(const char *in) {
    size_t max_len = strlen(in) * 3 + 1;
    char *out = malloc(max_len);
    if (!out) return;

    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j < max_len - 4; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out[j++] = c;
        } else if (c == ' ') {
            out[j++] = '+';
        } else {
            snprintf(out + j, 4, "%%%02X", c);
            j += 3;
        }
    }
    out[j] = '\0';
    printf("%s\n", out);
    free(out);
}

static void url_decode(const char *in) {
    size_t max_len = strlen(in) + 1;
    char *out = malloc(max_len);
    if (!out) return;

    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j < max_len - 1; i++) {
        if (in[i] == '+') {
            out[j++] = ' ';
        } else if (in[i] == '%' && in[i+1] && in[i+2]) {
            char hex[3] = { in[i+1], in[i+2], '\0' };
            out[j++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            out[j++] = in[i];
        }
    }
    out[j] = '\0';
    printf("%s\n", out);
    free(out);
}

static void rot13(const char *in) {
    size_t max_len = strlen(in) + 1;
    char *out = malloc(max_len);
    if (!out) return;

    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j < max_len - 1; i++) {
        char c = in[i];
        if (c >= 'a' && c <= 'z') out[j++] = 'a' + (c - 'a' + 13) % 26;
        else if (c >= 'A' && c <= 'Z') out[j++] = 'A' + (c - 'A' + 13) % 26;
        else out[j++] = c;
    }
    out[j] = '\0';
    printf("%s\n", out);
    free(out);
}

static void count_stats(const char *in) {
    size_t chars = strlen(in);
    size_t words = 0;
    size_t lines = 1;
    int in_word = 0;

    for (size_t i = 0; i < chars; i++) {
        if (in[i] == '\n') lines++;
        if (isspace((unsigned char)in[i])) {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }

    printf("======================\n");
    printf("[String Statistics]\n");
    printf("======================\n");
    printf("  • Characters : %lu\n", (unsigned long)chars);
    printf("  • Words      : %lu\n", (unsigned long)words);
    printf("  • Lines      : %lu\n", (unsigned long)lines);
    printf("======================\n");
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 3) {
        printf("Usage:\n");
        printf("  strutils --case <snake|camel> \"string\"\n");
        printf("  strutils --url-encode \"string\"\n");
        printf("  strutils --url-decode \"encoded_string\"\n");
        printf("  strutils --rot13 \"string\"\n");
        printf("  strutils --count \"string\"\n");
        utilipc_close();
        return 1;
    }

    const char *opt = argv[1];
    const char *str = argv[argc - 1];

    if (strcmp(opt, "--case") == 0 && argc >= 4) {
        if (strcmp(argv[2], "snake") == 0) to_snake_case(str);
        else if (strcmp(argv[2], "camel") == 0) to_camel_case(str);
        else printf("Error: Unknown case '%s'\n", argv[2]);
    } else if (strcmp(opt, "--url-encode") == 0) {
        url_encode(str);
    } else if (strcmp(opt, "--url-decode") == 0) {
        url_decode(str);
    } else if (strcmp(opt, "--rot13") == 0) {
        rot13(str);
    } else if (strcmp(opt, "--count") == 0) {
        count_stats(str);
    } else {
        printf("Error: Unknown option '%s'\n", opt);
    }

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "strutils: processed string (%s)", opt);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
