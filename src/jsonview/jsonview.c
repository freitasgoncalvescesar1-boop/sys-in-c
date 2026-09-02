#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_KEY     "\033[1;33m" // Amarelo
#define COLOR_STR     "\033[1;32m" // Verde
#define COLOR_NUM     "\033[1;36m" // Ciano
#define COLOR_BOOL    "\033[1;35m" // Magenta
#define COLOR_NULL    "\033[0;35m" // Magenta escuro
#define COLOR_PUNCT   "\033[1;37m" // Branco

static void print_help(void) {
    printf("======================\n");
    printf("[ jsonview - JSON Syntax Highlighter & Formatter ]\n");
    printf("======================\n");
    printf("Usage:\n");
    printf("  jsonview '<json_ou_shorthand>'\n");
    printf("  jsonview arquivo.json\n");
    printf("  cat dados.json | jsonview\n\n");
    printf("Exemplos:\n");
    printf("  jsonview 'usuarios: \"1,4,5\"'\n");
    printf("  jsonview 'nome: \"Carlos\", idade: 25, ativo: true'\n");
    printf("  jsonview '{\"status\": \"ok\", \"code\": 200}'\n");
    printf("======================\n");
}

static void print_indent(int indent) {
    for (int i = 0; i < indent * 2; i++) putchar(' ');
}

static char *convert_shorthand_to_json(const char *input) {
    size_t in_len = strlen(input);
    char *out = malloc(in_len * 3 + 64);
    if (!out) return NULL;

    size_t o = 0;
    int is_object_wrapped = 0;

    size_t start = 0;
    while (start < in_len && isspace((unsigned char)input[start])) start++;

    if (input[start] != '{' && input[start] != '[') {
        out[o++] = '{';
        is_object_wrapped = 1;
    }

    for (size_t i = start; i < in_len; i++) {
        unsigned char c = (unsigned char)input[i];

        if (isalnum(c) || c == '_' || c >= 128) {
            size_t k_start = i;
            while (i < in_len && (isalnum((unsigned char)input[i]) || input[i] == '_' || (unsigned char)input[i] >= 128)) {
                i++;
            }
            size_t k_end = i;

            size_t look = i;
            while (look < in_len && isspace((unsigned char)input[look])) look++;

            if (look < in_len && input[look] == ':') {
                out[o++] = '"';
                for (size_t k = k_start; k < k_end; k++) out[o++] = input[k];
                out[o++] = '"';
                i = look - 1;
            } else {
                for (size_t k = k_start; k < k_end; k++) out[o++] = input[k];
                i--;
            }
        } else if (c == '\'') {
            out[o++] = '"';
        } else {
            out[o++] = c;
        }
    }

    if (is_object_wrapped) {
        out[o++] = '}';
    }
    out[o] = '\0';
    return out;
}

static int format_and_colorize_json(const char *json) {
    int indent = 0;
    int in_string = 0;
    size_t len = strlen(json);

    printf("\n");

    for (size_t i = 0; i < len; i++) {
        char c = json[i];

        if (in_string && c == '\\' && i + 1 < len) {
            putchar(c);
            putchar(json[++i]);
            continue;
        }

        if (c == '"') {
            in_string = !in_string;
            if (in_string) {
                size_t look = i + 1;
                while (look < len && json[look] != '"') {
                    if (json[look] == '\\') look++;
                    look++;
                }
                if (look < len) look++;
                while (look < len && isspace((unsigned char)json[look])) look++;
                if (look < len && json[look] == ':') {
                    printf("%s\"", COLOR_KEY);
                } else {
                    printf("%s\"", COLOR_STR);
                }
            } else {
                printf("\"%s", COLOR_RESET);
            }
            continue;
        }

        if (in_string) {
            putchar(c);
            continue;
        }

        if (isspace((unsigned char)c)) continue;

        if (c == '{' || c == '[') {
            printf("%s%c%s\n", COLOR_PUNCT, c, COLOR_RESET);
            indent++;
            print_indent(indent);
        } else if (c == '}' || c == ']') {
            printf("\n");
            indent--;
            if (indent < 0) indent = 0;
            print_indent(indent);
            printf("%s%c%s", COLOR_PUNCT, c, COLOR_RESET);
        } else if (c == ',') {
            printf("%s,%s\n", COLOR_PUNCT, COLOR_RESET);
            print_indent(indent);
        } else if (c == ':') {
            printf("%s: %s", COLOR_PUNCT, COLOR_RESET);
        } else {
            if (strncmp(json + i, "true", 4) == 0) {
                printf("%strue%s", COLOR_BOOL, COLOR_RESET);
                i += 3;
            } else if (strncmp(json + i, "false", 5) == 0) {
                printf("%sfalse%s", COLOR_BOOL, COLOR_RESET);
                i += 4;
            } else if (strncmp(json + i, "null", 4) == 0) {
                printf("%snull%s", COLOR_NULL, COLOR_RESET);
                i += 3;
            } else if (isdigit((unsigned char)c) || c == '-' || c == '.') {
                printf("%s%c", COLOR_NUM, c);
                while (i + 1 < len && (isdigit((unsigned char)json[i + 1]) || json[i + 1] == '.' || json[i + 1] == 'e' || json[i + 1] == 'E' || json[i + 1] == '-' || json[i + 1] == '+')) {
                    putchar(json[++i]);
                }
                printf("%s", COLOR_RESET);
            } else {
                putchar(c);
            }
        }
    }
    printf("\n\n");
    return 0;
}

int main(int argc, char *argv[]) {
    utilipc_init();
    char *input_buffer = NULL;

    if (argc >= 2) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print_help();
            utilipc_close();
            return 0;
        }

        FILE *fp = fopen(argv[1], "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long fsz = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            if (fsz > 0) {
                input_buffer = malloc(fsz + 1);
                if (input_buffer) {
                    fread(input_buffer, 1, fsz, fp);
                    input_buffer[fsz] = '\0';
                }
            }
            fclose(fp);
        }

        if (!input_buffer) {
            size_t total_len = 0;
            for (int i = 1; i < argc; i++) total_len += strlen(argv[i]) + 1;
            input_buffer = malloc(total_len + 16);
            input_buffer[0] = '\0';
            for (int i = 1; i < argc; i++) {
                strcat(input_buffer, argv[i]);
                if (i < argc - 1) strcat(input_buffer, " ");
            }
        }
    } else {
        // Se está num terminal interativo e NÃO recebeu pipes (|), não bloqueia no getchar()
        if (isatty(STDIN_FILENO)) {
            print_help();
            utilipc_close();
            return 0;
        }

        // Lê do STDIN apenas se vier de um Pipe ou Redirecionamento
        size_t cap = 4096;
        input_buffer = malloc(cap);
        size_t n = 0;
        int ch;
        while ((ch = getchar()) != EOF) {
            if (n + 2 >= cap) {
                cap *= 2;
                input_buffer = realloc(input_buffer, cap);
            }
            input_buffer[n++] = (char)ch;
        }
        if (n > 0) {
            input_buffer[n] = '\0';
        } else {
            free(input_buffer);
            input_buffer = NULL;
        }
    }

    if (!input_buffer || strlen(input_buffer) == 0) {
        print_help();
        if (input_buffer) free(input_buffer);
        utilipc_close();
        return 0;
    }

    char *formatted_json = convert_shorthand_to_json(input_buffer);
    if (formatted_json) {
        format_and_colorize_json(formatted_json);
        free(formatted_json);
    } else {
        format_and_colorize_json(input_buffer);
    }

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "jsonview: formatted json (%zu bytes)", strlen(input_buffer));
    utilipc_write_status(-1, -1, -1, log_msg);

    free(input_buffer);
    utilipc_close();
    return 0;
}
