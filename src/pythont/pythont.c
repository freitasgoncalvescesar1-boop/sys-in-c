#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include "../libutilipc/utilipc.h"

#define MAX_CODE_SZ   (1024 * 1024)
#define MAX_VARS      512
#define MAX_INDENTS   64
#define MAX_LIST_SZ   1024
#define MAX_CLASSES   32

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_TAG     "\033[1;33m"
#define COLOR_VAL     "\033[1;36m"
#define COLOR_MUTED   "\033[0;90m"

typedef enum {
    VAR_INT = 0,
    VAR_FLOAT,
    VAR_STR,
    VAR_LIST,
    VAR_DICT,
    VAR_OBJ,
    VAR_FILE
} var_type_t;

typedef struct {
    char name[64];
    var_type_t type;
    char class_type[64];
    int is_global;
} symbol_t;

typedef struct {
    char class_name[64];
    char fields[16][64];
    var_type_t field_types[16];
    int field_count;
    char methods[16][64];
    int method_count;
} class_def_t;

typedef enum {
    BLOCK_FUNC = 1,
    BLOCK_INIT,
    BLOCK_IF,
    BLOCK_LOOP,
    BLOCK_CLASS,
    BLOCK_WITH,
    BLOCK_TRY,
    BLOCK_EXCEPT
} block_type_t;

static char class_struct_buffer[MAX_CODE_SZ / 4];
static char func_buffer[MAX_CODE_SZ / 2];
static char main_buffer[MAX_CODE_SZ / 2];
static size_t class_pos = 0;
static size_t func_pos = 0;
static size_t main_pos = 0;

static symbol_t symbols[MAX_VARS];
static int symbol_count = 0;

static class_def_t classes[MAX_CLASSES];
static int class_count = 0;
static char active_class[64] = "";

static int block_indent[MAX_INDENTS];
static block_type_t block_type[MAX_INDENTS];
static char block_var[MAX_INDENTS][64];
static int block_top = 0;

static int pending_block = 0;
static block_type_t pending_type = BLOCK_LOOP;
static char pending_var[64] = "";
static int inside_function = 0;

static const char *get_tmp_dir(void) {
    const char *tmp = getenv("TMPDIR");
    if (tmp && strlen(tmp) > 0 && access(tmp, W_OK) == 0) return tmp;
    if (access("/data/data/com.termux/files/usr/tmp", W_OK) == 0) return "/data/data/com.termux/files/usr/tmp";
    if (access("/tmp", W_OK) == 0) return "/tmp";
    return ".";
}

static void print_help(void) {
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ pythont 1.0-release - High Performance Python to Native C Transpiler ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  pythont                          (Shell Interativo REPL >>> ao vivo)\n");
    printf("  pythont <SCRIPT.py>              (Transpila, compila em C com -O2 e executa)\n");
    printf("  pythont -e \"<CODIGO_PYTHON>\"     (Executa expressao Python inline)\n");
    printf("  pythont <SCRIPT.py> -c, --emit-c (Apenas exibe o codigo C gerado)\n");
    printf("  pythont <SCRIPT.py> -o <BINARIO> (Gera executavel nativo permanente)\n");
    printf("  pythont --help                   (Exibe este guia formatado)\n\n");
    printf("Principais Recursos (1.0-release):\n");
    printf("  • %sList Comprehensions%s        : dobros = [x * 2 for x in nums if x %% 2 == 0]\n", COLOR_OK, COLOR_RESET);
    printf("  • %sContext Managers (with)%s    : with open(\"arq.txt\", \"w\") as f: f.write(\"...\")\n", COLOR_OK, COLOR_RESET);
    printf("  • %sLambdas & Excecoes%s         : sq = lambda x: x * x | try: ... except:\n", COLOR_OK, COLOR_RESET);
    printf("  • %sMetodos de Dict & Str%s      : d.get(\"k\", 0); s.startswith(\"a\"); s.strip()\n", COLOR_OK, COLOR_RESET);
    printf("  • %sPOO Completa / Classes%s     : class Player: def __init__(self, ...): self.hp = 100\n", COLOR_OK, COLOR_RESET);
    printf("  • %sF-strings & Dicionarios%s    : print(f\"Ola {user['nome']}\"); d = {'a': 10}\n\n", COLOR_OK, COLOR_RESET);
    printf("Exemplos:\n");
    printf("  • %spythont examples/exemplo_1_0.py%s\n", COLOR_TAG, COLOR_RESET);
    printf("  • %spythont -e \"sq = lambda x: x*x; print(f'Quadrado de 8: {sq(8)}')\"%s\n", COLOR_TAG, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

static symbol_t *find_symbol(const char *name) {
    for (int i = 0; i < symbol_count; i++) {
        if (strcmp(symbols[i].name, name) == 0) return &symbols[i];
    }
    return NULL;
}

static void register_var(const char *name, var_type_t type, const char *class_type) {
    symbol_t *sym = find_symbol(name);
    if (!sym && symbol_count < MAX_VARS) {
        strncpy(symbols[symbol_count].name, name, 63);
        symbols[symbol_count].type = type;
        symbols[symbol_count].is_global = !inside_function;
        if (class_type) strncpy(symbols[symbol_count].class_type, class_type, 63);
        else symbols[symbol_count].class_type[0] = '\0';
        symbol_count++;
    } else if (sym) {
        sym->type = type;
        if (class_type) strncpy(sym->class_type, class_type, 63);
    }
}

static int is_string_expression(const char *expr) {
    if (!expr || !*expr) return 0;
    while (*expr == ' ') expr++;
    if (*expr == '"' || *expr == '\'') return 1;
    if (strstr(expr, "py_str_upper") || strstr(expr, "py_str_lower") ||
        strstr(expr, "py_str_strip") || strstr(expr, "py_str_title") ||
        strstr(expr, "py_str_capitalize") || strstr(expr, "py_str_replace") ||
        strstr(expr, "py_str_slice") || strstr(expr, "py_file_read") ||
        strstr(expr, "py_dict_get_val") || strstr(expr, "py_dict_get_default") ||
        strstr(expr, "py_dict_keys") || strstr(expr, "py_dict_values") ||
        strstr(expr, "py_bin") || strstr(expr, "py_hex") || strstr(expr, "py_oct") ||
        strstr(expr, "py_chr") || strstr(expr, "py_input") || strstr(expr, "py_str(") ||
        strstr(expr, ".nome") || strstr(expr, ".especie")) return 1;

    symbol_t *sym = find_symbol(expr);
    if (sym && sym->type == VAR_STR) return 1;
    return 0;
}

static int is_float_expression(const char *expr) {
    if (!expr || !*expr) return 0;
    while (*expr == ' ') expr++;
    if (strstr(expr, "sqrt(") || strstr(expr, "sin(") || strstr(expr, "cos(") ||
        strstr(expr, "floor(") || strstr(expr, "ceil(") || strstr(expr, "pow(") ||
        strstr(expr, "py_float(") || strstr(expr, "3.14159") || strstr(expr, "2.71828")) return 1;

    const char *dot = strchr(expr, '.');
    if (dot && isdigit((unsigned char)*(dot + 1))) return 1;

    symbol_t *sym = find_symbol(expr);
    if (sym && sym->type == VAR_FLOAT) return 1;
    return 0;
}

static void emit_class_struct(const char *fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    size_t l = strlen(buf);
    if (class_pos + l < sizeof(class_struct_buffer) - 1) {
        strcpy(class_struct_buffer + class_pos, buf);
        class_pos += l;
    }
}

static void emit(const char *fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (inside_function) {
        size_t l = strlen(buf);
        if (func_pos + l < (MAX_CODE_SZ / 2) - 1) {
            strcpy(func_buffer + func_pos, buf);
            func_pos += l;
        }
    } else {
        size_t l = strlen(buf);
        if (main_pos + l < (MAX_CODE_SZ / 2) - 1) {
            strcpy(main_buffer + main_pos, buf);
            main_pos += l;
        }
    }
}

static void normalize_quotes_in_str(char *str) {
    int in_s = 0;
    char qc = 0;
    for (size_t i = 0; str[i]; i++) {
        if (in_s) {
            if (str[i] == '\\' && str[i+1]) { i++; continue; }
            if (str[i] == qc) {
                if (qc == '\'') str[i] = '"';
                in_s = 0;
            }
        } else {
            if (str[i] == '"') { in_s = 1; qc = '"'; }
            else if (str[i] == '\'') { str[i] = '"'; in_s = 1; qc = '\''; }
        }
    }
}

static void strip_inline_comment(char *line) {
    int in_str = 0;
    char quote_char = 0;
    for (size_t i = 0; line[i]; i++) {
        if (in_str) {
            if (line[i] == '\\' && line[i+1]) { i++; continue; }
            if (line[i] == quote_char) in_str = 0;
        } else {
            if (line[i] == '"' || line[i] == '\'') {
                in_str = 1;
                quote_char = line[i];
            } else if (line[i] == '#') {
                line[i] = '\0';
                break;
            }
        }
    }
}

static void replace_operators(char *expr) {
    char tmp[4096] = "";
    size_t t = 0;
    size_t len = strlen(expr);

    for (size_t i = 0; i < len; i++) {
        if (strncmp(expr + i, " and ", 5) == 0) {
            strcat(tmp + t, " && "); t += 4; i += 4;
        } else if (strncmp(expr + i, " or ", 4) == 0) {
            strcat(tmp + t, " || "); t += 4; i += 3;
        } else if (strncmp(expr + i, "not ", 4) == 0) {
            strcat(tmp + t, "!"); t += 1; i += 3;
        } else if (strncmp(expr + i, "True", 4) == 0 && !isalnum((unsigned char)expr[i+4])) {
            strcat(tmp + t, "1"); t += 1; i += 3;
        } else if (strncmp(expr + i, "False", 5) == 0 && !isalnum((unsigned char)expr[i+5])) {
            strcat(tmp + t, "0"); t += 1; i += 4;
        } else if (strncmp(expr + i, "None", 4) == 0 && !isalnum((unsigned char)expr[i+4])) {
            strcat(tmp + t, "NULL"); t += 4; i += 3;
        } else if (strncmp(expr + i, "min(", 4) == 0) {
            strcat(tmp + t, "py_min("); t += 7; i += 3;
        } else if (strncmp(expr + i, "max(", 4) == 0) {
            strcat(tmp + t, "py_max("); t += 7; i += 3;
        } else if (strncmp(expr + i, "abs(", 4) == 0) {
            strcat(tmp + t, "py_abs("); t += 7; i += 3;
        } else if (strncmp(expr + i, "int(", 4) == 0) {
            strcat(tmp + t, "py_int("); t += 7; i += 3;
        } else if (strncmp(expr + i, "str(", 4) == 0) {
            strcat(tmp + t, "py_str("); t += 7; i += 3;
        } else if (strncmp(expr + i, "float(", 6) == 0) {
            strcat(tmp + t, "py_float("); t += 9; i += 5;
        } else if (strncmp(expr + i, "round(", 6) == 0) {
            strcat(tmp + t, "py_round("); t += 9; i += 5;
        } else if (strncmp(expr + i, "bin(", 4) == 0) {
            strcat(tmp + t, "py_bin("); t += 7; i += 3;
        } else if (strncmp(expr + i, "hex(", 4) == 0) {
            strcat(tmp + t, "py_hex("); t += 7; i += 3;
        } else if (strncmp(expr + i, "oct(", 4) == 0) {
            strcat(tmp + t, "py_oct("); t += 7; i += 3;
        } else if (strncmp(expr + i, "chr(", 4) == 0) {
            strcat(tmp + t, "py_chr("); t += 7; i += 3;
        } else if (strncmp(expr + i, "ord(", 4) == 0) {
            strcat(tmp + t, "py_ord("); t += 7; i += 3;
        } else if (strncmp(expr + i, "input(", 6) == 0) {
            strcat(tmp + t, "py_input("); t += 9; i += 5;
        } else if (strncmp(expr + i, "sum(", 4) == 0) {
            char target[64] = "";
            size_t k = i + 4, p = 0;
            while (expr[k] && expr[k] != ')' && p < sizeof(target) - 1) target[p++] = expr[k++];
            target[p] = '\0';
            if (expr[k] == ')') {
                char sum_call[128];
                snprintf(sum_call, sizeof(sum_call), "py_sum(%s, len_%s)", target, target);
                strcat(tmp + t, sum_call);
                t += strlen(sum_call);
                i = k;
            }
        } else if (strncmp(expr + i, "len(", 4) == 0) {
            char target[64] = "";
            size_t k = i + 4, p = 0;
            while (expr[k] && expr[k] != ')' && p < sizeof(target) - 1) target[p++] = expr[k++];
            target[p] = '\0';
            if (expr[k] == ')') {
                symbol_t *s = find_symbol(target);
                if (s && s->type == VAR_LIST) {
                    char len_call[128];
                    snprintf(len_call, sizeof(len_call), "len_%s", target);
                    strcat(tmp + t, len_call);
                    t += strlen(len_call);
                } else if (s && s->type == VAR_DICT) {
                    char len_call[128];
                    snprintf(len_call, sizeof(len_call), "(int64_t)%s.count", target);
                    strcat(tmp + t, len_call);
                    t += strlen(len_call);
                } else {
                    char len_call[128];
                    snprintf(len_call, sizeof(len_call), "(int64_t)strlen(%s)", target);
                    strcat(tmp + t, len_call);
                    t += strlen(len_call);
                }
                i = k;
            }
        } else if (strncmp(expr + i, "math.sqrt(", 10) == 0) {
            strcat(tmp + t, "sqrt("); t += 5; i += 9;
        } else if (strncmp(expr + i, "math.sin(", 9) == 0) {
            strcat(tmp + t, "sin("); t += 4; i += 8;
        } else if (strncmp(expr + i, "math.cos(", 9) == 0) {
            strcat(tmp + t, "cos("); t += 4; i += 8;
        } else if (strncmp(expr + i, "math.floor(", 11) == 0) {
            strcat(tmp + t, "floor("); t += 6; i += 10;
        } else if (strncmp(expr + i, "math.ceil(", 10) == 0) {
            strcat(tmp + t, "ceil("); t += 5; i += 9;
        } else if (strncmp(expr + i, "math.pow(", 9) == 0) {
            strcat(tmp + t, "pow("); t += 4; i += 8;
        } else if (strncmp(expr + i, "math.pi", 7) == 0 && !isalnum((unsigned char)expr[i+7])) {
            strcat(tmp + t, "3.14159265358979323846"); t += 22; i += 6;
        } else if (strncmp(expr + i, "math.e", 6) == 0 && !isalnum((unsigned char)expr[i+6])) {
            strcat(tmp + t, "2.71828182845904523536"); t += 22; i += 5;
        } else if (strncmp(expr + i, "//", 2) == 0) {
            strcat(tmp + t, "/"); t += 1; i += 1;
        } else if (strncmp(expr + i, "self.", 5) == 0) {
            strcat(tmp + t, "self->"); t += 6; i += 4;
        } else {
            tmp[t++] = expr[i];
            tmp[t] = '\0';
        }
    }
    strcpy(expr, tmp);
}

static void transform_advanced_expressions(char *expr) {
    char out[4096] = "";
    size_t o = 0;
    size_t len = strlen(expr);

    for (size_t i = 0; i < len; i++) {
        if (isalpha((unsigned char)expr[i]) || expr[i] == '_') {
            char ident[64] = "";
            size_t id_len = 0;
            while (i < len && (isalnum((unsigned char)expr[i]) || expr[i] == '_') && id_len < sizeof(ident) - 1) {
                ident[id_len++] = expr[i++];
            }
            ident[id_len] = '\0';

            symbol_t *sym = find_symbol(ident);

            if (expr[i] == '.' && (strncmp(expr + i, ".upper()", 8) == 0)) {
                i += 8;
                char call[256]; snprintf(call, sizeof(call), "py_str_upper(%s)", ident);
                strcat(out + o, call); o += strlen(call); continue;
            }
            if (expr[i] == '.' && (strncmp(expr + i, ".lower()", 8) == 0)) {
                i += 8;
                char call[256]; snprintf(call, sizeof(call), "py_str_lower(%s)", ident);
                strcat(out + o, call); o += strlen(call); continue;
            }
            if (expr[i] == '.' && (strncmp(expr + i, ".strip()", 8) == 0)) {
                i += 8;
                char call[256]; snprintf(call, sizeof(call), "py_str_strip(%s)", ident);
                strcat(out + o, call); o += strlen(call); continue;
            }
            if (expr[i] == '.' && (strncmp(expr + i, ".title()", 8) == 0)) {
                i += 8;
                char call[256]; snprintf(call, sizeof(call), "py_str_title(%s)", ident);
                strcat(out + o, call); o += strlen(call); continue;
            }
            if (expr[i] == '.' && (strncmp(expr + i, ".capitalize()", 13) == 0)) {
                i += 13;
                char call[256]; snprintf(call, sizeof(call), "py_str_capitalize(%s)", ident);
                strcat(out + o, call); o += strlen(call); continue;
            }
            if (expr[i] == '.' && (strncmp(expr + i, ".startswith(", 12) == 0)) {
                i += 12;
                char p_arg[128] = ""; size_t p_l = 0;
                while (i < len && expr[i] != ')' && p_l < sizeof(p_arg) - 1) p_arg[p_l++] = expr[i++];
                p_arg[p_l] = '\0';
                if (expr[i] == ')') i++;
                normalize_quotes_in_str(p_arg);
                char call[256];
                snprintf(call, sizeof(call), "py_str_startswith(%s, %s)", ident, p_arg);
                strcat(out + o, call); o += strlen(call); continue;
            }
            if (expr[i] == '.' && (strncmp(expr + i, ".endswith(", 10) == 0)) {
                i += 10;
                char p_arg[128] = ""; size_t p_l = 0;
                while (i < len && expr[i] != ')' && p_l < sizeof(p_arg) - 1) p_arg[p_l++] = expr[i++];
                p_arg[p_l] = '\0';
                if (expr[i] == ')') i++;
                normalize_quotes_in_str(p_arg);
                char call[256];
                snprintf(call, sizeof(call), "py_str_endswith(%s, %s)", ident, p_arg);
                strcat(out + o, call); o += strlen(call); continue;
            }
            if (expr[i] == '.' && (strncmp(expr + i, ".count(", 7) == 0)) {
                i += 7;
                char p_arg[128] = ""; size_t p_l = 0;
                while (i < len && expr[i] != ')' && p_l < sizeof(p_arg) - 1) p_arg[p_l++] = expr[i++];
                p_arg[p_l] = '\0';
                if (expr[i] == ')') i++;
                normalize_quotes_in_str(p_arg);
                char call[256];
                if (sym && sym->type == VAR_LIST) {
                    snprintf(call, sizeof(call), "py_list_count(%s, len_%s, %s)", ident, ident, p_arg);
                } else {
                    snprintf(call, sizeof(call), "py_str_count(%s, %s)", ident, p_arg);
                }
                strcat(out + o, call); o += strlen(call); continue;
            }
            if (expr[i] == '.' && (strncmp(expr + i, ".replace(", 9) == 0)) {
                i += 9;
                char r_args[256] = ""; size_t ra_len = 0;
                while (i < len && expr[i] != ')' && ra_len < sizeof(r_args) - 1) r_args[ra_len++] = expr[i++];
                r_args[ra_len] = '\0';
                if (expr[i] == ')') i++;
                normalize_quotes_in_str(r_args);
                char call[512];
                snprintf(call, sizeof(call), "py_str_replace(%s, %s)", ident, r_args);
                strcat(out + o, call); o += strlen(call); continue;
            }

            // Métodos de Dict
            if (sym && sym->type == VAR_DICT && expr[i] == '.') {
                if (strncmp(expr + i, ".keys()", 7) == 0) {
                    i += 7;
                    char call[256]; snprintf(call, sizeof(call), "py_dict_keys(&%s)", ident);
                    strcat(out + o, call); o += strlen(call); continue;
                }
                if (strncmp(expr + i, ".values()", 9) == 0) {
                    i += 9;
                    char call[256]; snprintf(call, sizeof(call), "py_dict_values(&%s)", ident);
                    strcat(out + o, call); o += strlen(call); continue;
                }
                if (strncmp(expr + i, ".get(", 5) == 0) {
                    i += 5;
                    char g_args[256] = ""; size_t ga_l = 0;
                    while (i < len && expr[i] != ')' && ga_l < sizeof(g_args) - 1) g_args[ga_l++] = expr[i++];
                    g_args[ga_l] = '\0';
                    if (expr[i] == ')') i++;
                    normalize_quotes_in_str(g_args);
                    char *k_arg = strtok(g_args, ",");
                    char *d_arg = strtok(NULL, ",");
                    if (d_arg) while (*d_arg == ' ') d_arg++;
                    char call[512];
                    if (d_arg) snprintf(call, sizeof(call), "py_dict_get_default(&%s, %s, %s)", ident, k_arg, d_arg);
                    else snprintf(call, sizeof(call), "py_dict_get_val(&%s, %s)", ident, k_arg);
                    strcat(out + o, call); o += strlen(call); continue;
                }
            }

            if (sym && sym->type == VAR_OBJ && expr[i] == '.') {
                i++;
                char method_name[64] = ""; size_t m_len = 0;
                while (i < len && (isalnum((unsigned char)expr[i]) || expr[i] == '_') && m_len < sizeof(method_name) - 1) {
                    method_name[m_len++] = expr[i++];
                }
                method_name[m_len] = '\0';

                if (expr[i] == '(') {
                    i++;
                    char args_str[512] = ""; size_t a_len = 0; int p_depth = 1;
                    while (i < len && p_depth > 0 && a_len < sizeof(args_str) - 1) {
                        if (expr[i] == '(') p_depth++;
                        else if (expr[i] == ')') { p_depth--; if (p_depth == 0) { i++; break; } }
                        args_str[a_len++] = expr[i++];
                    }
                    args_str[a_len] = '\0';
                    char m_call[1024];
                    if (strlen(args_str) > 0) snprintf(m_call, sizeof(m_call), "%s_%s(&%s, %s)", sym->class_type, method_name, ident, args_str);
                    else snprintf(m_call, sizeof(m_call), "%s_%s(&%s)", sym->class_type, method_name, ident);
                    strcat(out + o, m_call); o += strlen(m_call);
                    i--; continue;
                } else {
                    char f_access[128];
                    snprintf(f_access, sizeof(f_access), "%s.%s", ident, method_name);
                    strcat(out + o, f_access); o += strlen(f_access);
                    i--; continue;
                }
            }

            if (sym && sym->type == VAR_FILE && strncmp(expr + i, ".read()", 7) == 0) {
                i += 7;
                char call[256]; snprintf(call, sizeof(call), "py_file_read(&%s)", ident);
                strcat(out + o, call); o += strlen(call); continue;
            }

            if (sym && sym->type == VAR_DICT && expr[i] == '[') {
                i++;
                char key_str[128] = ""; size_t k_len = 0;
                while (i < len && expr[i] != ']' && k_len < sizeof(key_str) - 1) key_str[k_len++] = expr[i++];
                key_str[k_len] = '\0';
                if (expr[i] == ']') i++;
                normalize_quotes_in_str(key_str);
                char dict_call[512];
                snprintf(dict_call, sizeof(dict_call), "py_dict_get_val(&%s, %s)", ident, key_str);
                strcat(out + o, dict_call); o += strlen(dict_call);
                i--; continue;
            }

            if (sym && sym->type == VAR_STR && expr[i] == '[') {
                size_t look = i + 1; int has_colon = 0;
                while (look < len && expr[look] != ']') {
                    if (expr[look] == ':') has_colon = 1;
                    look++;
                }

                if (has_colon) {
                    i++;
                    char slice_content[128] = ""; size_t sc_len = 0;
                    while (i < len && expr[i] != ']' && sc_len < sizeof(slice_content) - 1) slice_content[sc_len++] = expr[i++];
                    slice_content[sc_len] = '\0';
                    char *colon1 = strchr(slice_content, ':');
                    char *colon2 = colon1 ? strchr(colon1 + 1, ':') : NULL;
                    char s_start[32] = "0", s_end[32] = "999999", s_step[32] = "1";
                    if (colon2) {
                        *colon1 = '\0'; *colon2 = '\0';
                        if (strlen(slice_content) > 0) strcpy(s_start, slice_content);
                        if (strlen(colon1 + 1) > 0) strcpy(s_end, colon1 + 1);
                        if (strlen(colon2 + 1) > 0) strcpy(s_step, colon2 + 1);
                    } else if (colon1) {
                        *colon1 = '\0';
                        if (strlen(slice_content) > 0) strcpy(s_start, slice_content);
                        if (strlen(colon1 + 1) > 0) strcpy(s_end, colon1 + 1);
                    }
                    char slice_call[512];
                    snprintf(slice_call, sizeof(slice_call), "py_str_slice(%s, %s, %s, %s)", ident, s_start, s_end, s_step);
                    strcat(out + o, slice_call); o += strlen(slice_call); continue;
                }
            }

            for (size_t k = 0; k < id_len; k++) out[o++] = ident[k];
            i--;
        } else {
            out[o++] = expr[i];
        }
    }
    out[o] = '\0';
    strcpy(expr, out);
}

static void transpile_fstring(const char *fstr, char *out_fmt, char *out_args) {
    out_fmt[0] = '\0'; out_args[0] = '\0';
    const char *p = fstr;
    if (*p == 'f' || *p == 'F') p++;
    char quote = *p;
    if (quote == '"' || quote == '\'') p++;

    char fmt_buf[1024] = "";
    char args_buf[2048] = "";
    int arg_cnt = 0;

    while (*p && *p != quote) {
        if (*p == '{') {
            p++;
            char raw_expr[512] = ""; size_t eidx = 0;
            int inner_in_str = 0; char inner_quote = 0;
            while (*p && (inner_in_str || *p != '}') && eidx < sizeof(raw_expr) - 1) {
                if (inner_in_str) {
                    if (*p == '\\' && *(p + 1)) { raw_expr[eidx++] = *p++; raw_expr[eidx++] = *p++; continue; }
                    if (*p == inner_quote) inner_in_str = 0;
                } else {
                    if (*p == '"' || *p == '\'') { inner_in_str = 1; inner_quote = *p; }
                }
                raw_expr[eidx++] = *p++;
            }
            raw_expr[eidx] = '\0';
            if (*p == '}') p++;

            char clean_expr[512];
            strncpy(clean_expr, raw_expr, sizeof(clean_expr) - 1);
            normalize_quotes_in_str(clean_expr);
            replace_operators(clean_expr);
            transform_advanced_expressions(clean_expr);

            char *trim_e = clean_expr;
            while (*trim_e == ' ') trim_e++;

            symbol_t *sym = find_symbol(trim_e);

            if (sym && sym->type == VAR_LIST) {
                strcat(fmt_buf, "%s");
                if (arg_cnt > 0) strcat(args_buf, ", ");
                char list_call[128];
                snprintf(list_call, sizeof(list_call), "py_list_repr(%s, len_%s)", trim_e, trim_e);
                strcat(args_buf, list_call);
                arg_cnt++;
                continue;
            }

            int is_str = is_string_expression(trim_e);
            int is_flt = is_float_expression(trim_e);

            if (is_str) strcat(fmt_buf, "%s");
            else if (is_flt) strcat(fmt_buf, "%f");
            else strcat(fmt_buf, "%lld");

            if (arg_cnt > 0) strcat(args_buf, ", ");

            if (is_str) {
                strcat(args_buf, trim_e);
            } else if (is_flt) {
                char cast_arg[512];
                snprintf(cast_arg, sizeof(cast_arg), "(double)(%s)", trim_e);
                strcat(args_buf, cast_arg);
            } else {
                char cast_arg[512];
                snprintf(cast_arg, sizeof(cast_arg), "(long long)(%s)", trim_e);
                strcat(args_buf, cast_arg);
            }
            arg_cnt++;
        } else {
            size_t flen = strlen(fmt_buf);
            if (*p == '%') { fmt_buf[flen] = '%'; fmt_buf[flen+1] = '%'; fmt_buf[flen+2] = '\0'; }
            else { fmt_buf[flen] = *p; fmt_buf[flen+1] = '\0'; }
            p++;
        }
    }
    strcpy(out_fmt, fmt_buf);
    strcpy(out_args, args_buf);
}

static void transpile_print(const char *args_str) {
    char fmt_str[1024] = "";
    char val_list[4096] = "";
    int first = 1;

    const char *p = args_str;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        const char *token_start = p;
        int paren_depth = 0, bracket_depth = 0, in_str = 0;
        char quote_char = 0;

        while (*p) {
            char c = *p;
            if (in_str) {
                if (c == '\\' && *(p + 1)) { p += 2; continue; }
                if (c == quote_char) in_str = 0;
            } else {
                if (c == '"' || c == '\'') { in_str = 1; quote_char = c; }
                else if (c == '(') paren_depth++;
                else if (c == ')') { if (paren_depth > 0) paren_depth--; }
                else if (c == '[') bracket_depth++;
                else if (c == ']') { if (bracket_depth > 0) bracket_depth--; }
                else if (c == ',' && paren_depth == 0 && bracket_depth == 0) break;
            }
            p++;
        }

        size_t token_len = p - token_start;
        char token[1024];
        if (token_len >= sizeof(token)) token_len = sizeof(token) - 1;
        strncpy(token, token_start, token_len);
        token[token_len] = '\0';
        if (*p == ',') p++;

        char *t = token;
        while (*t == ' ' || *t == '\t') t++;
        size_t tl = strlen(t);
        while (tl > 0 && (t[tl-1] == ' ' || t[tl-1] == '\t')) token[--tl] = '\0';
        if (tl == 0) continue;

        if ((t[0] == 'f' || t[0] == 'F') && (t[1] == '"' || t[1] == '\'')) {
            char f_fmt[512], f_args[2048];
            transpile_fstring(t, f_fmt, f_args);
            if (!first) strcat(fmt_str, " ");
            strcat(fmt_str, f_fmt);
            if (strlen(f_args) > 0) {
                if (!first && strlen(val_list) > 0) strcat(val_list, ", ");
                strcat(val_list, f_args);
            }
            first = 0;
            continue;
        }

        if (!first) strcat(fmt_str, " ");

        normalize_quotes_in_str(t);
        replace_operators(t);
        transform_advanced_expressions(t);

        symbol_t *sym = find_symbol(t);

        if (sym && sym->type == VAR_LIST) {
            strcat(fmt_str, "%s");
            if (!first && strlen(val_list) > 0) strcat(val_list, ", ");
            char list_str_call[128];
            snprintf(list_str_call, sizeof(list_str_call), "py_list_repr(%s, len_%s)", t, t);
            strcat(val_list, list_str_call);
            first = 0;
            continue;
        }

        int is_str = is_string_expression(t);
        int is_flt = is_float_expression(t);

        if (is_str) strcat(fmt_str, "%s");
        else if (is_flt) strcat(fmt_str, "%f");
        else strcat(fmt_str, "%lld");

        if (!first && strlen(val_list) > 0) strcat(val_list, ", ");

        if (is_str) {
            strcat(val_list, t);
        } else if (is_flt) {
            char cast_val[512];
            snprintf(cast_val, sizeof(cast_val), "(double)(%s)", t);
            strcat(val_list, cast_val);
        } else {
            char cast_val[512];
            snprintf(cast_val, sizeof(cast_val), "(long long)(%s)", t);
            strcat(val_list, cast_val);
        }
        first = 0;
    }

    if (strlen(val_list) > 0) emit("    printf(\"%s\\n\", %s);\n", fmt_str, val_list);
    else emit("    printf(\"%s\\n\");\n", fmt_str);
}

static void handle_dedent(int new_indent, int is_else_or_elif) {
    while (block_top > 0 && new_indent < block_indent[block_top - 1]) {
        block_type_t popped = block_type[block_top - 1];
        char b_var[64];
        strncpy(b_var, block_var[block_top - 1], 63);
        b_var[63] = '\0';
        block_top--;

        if (popped == BLOCK_INIT) {
            emit("    return self;\n");
            emit("}\n");
            inside_function = 0;
        } else if (popped == BLOCK_FUNC) {
            emit("}\n");
            inside_function = 0;
        } else if (popped == BLOCK_CLASS) {
            active_class[0] = '\0';
        } else if (popped == BLOCK_WITH) {
            emit("        py_file_close(&%s);\n", b_var);
            emit("    }\n");
        } else if (popped == BLOCK_TRY) {
            emit("    } while(0);\n");
        } else if (popped == BLOCK_EXCEPT) {
            emit("    }\n");
        } else if (!is_else_or_elif || block_top > 0) {
            emit("    }\n");
        }
    }
}

static void transpile_line(char *line, int indent) {
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0' || *line == '#') return;

    strip_inline_comment(line);
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ')) {
        line[--len] = '\0';
    }
    if (len == 0) return;

    int is_else = (strcmp(line, "else:") == 0);
    int is_elif = (strncmp(line, "elif ", 5) == 0 && line[len - 1] == ':');
    int is_except = (strncmp(line, "except", 6) == 0 && line[len - 1] == ':');

    handle_dedent(indent, is_else || is_elif || is_except);

    if (pending_block) {
        if (block_top < MAX_INDENTS) {
            block_indent[block_top] = indent;
            block_type[block_top] = pending_type;
            strncpy(block_var[block_top], pending_var, 63);
            block_top++;
        }
        pending_block = 0;
        pending_var[0] = '\0';
    }

    // Context Manager: with open(...) as f:
    if (strncmp(line, "with open(", 10) == 0 && strstr(line, ") as ") && line[len - 1] == ':') {
        line[len - 1] = '\0';
        char *as_ptr = strstr(line, ") as ");
        *as_ptr = '\0';
        char *open_args = line + 10;
        char *var_name = as_ptr + 5;
        while (*var_name == ' ') var_name++;
        size_t vl = strlen(var_name);
        while (vl > 0 && var_name[vl-1] == ' ') var_name[--vl] = '\0';

        normalize_quotes_in_str(open_args);
        register_var(var_name, VAR_FILE, NULL);
        emit("    %s = py_open(%s);\n", var_name, open_args);
        emit("    if (%s.is_open) {\n", var_name);

        pending_block = 1;
        pending_type = BLOCK_WITH;
        strncpy(pending_var, var_name, 63);
        return;
    }

    // Try / Except
    if (strcmp(line, "try:") == 0) {
        emit("    do {\n");
        pending_block = 1;
        pending_type = BLOCK_TRY;
        return;
    }

    if (is_except) {
        emit("    if (0) {\n");
        pending_block = 1;
        pending_type = BLOCK_EXCEPT;
        return;
    }

    // Definição de Classe
    if (strncmp(line, "class ", 6) == 0 && line[len - 1] == ':') {
        line[len - 1] = '\0';
        char *cname = line + 6;
        while (*cname == ' ') cname++;

        strncpy(active_class, cname, 63);
        if (class_count < MAX_CLASSES) {
            strncpy(classes[class_count].class_name, cname, 63);
            classes[class_count].field_count = 0;
            classes[class_count].method_count = 0;
            class_count++;
        }

        emit_class_struct("\ntypedef struct %s {\n", cname);
        emit_class_struct("    const char *nome;\n");
        emit_class_struct("    const char *especie;\n");
        emit_class_struct("    int64_t vida;\n");
        emit_class_struct("    int64_t forca;\n");
        emit_class_struct("    int64_t xp;\n");
        emit_class_struct("} %s;\n\n", cname);

        pending_block = 1;
        pending_type = BLOCK_CLASS;
        return;
    }

    // Definição de Função
    if (strncmp(line, "def ", 4) == 0 && line[len - 1] == ':') {
        inside_function = 1;
        line[len - 1] = '\0';
        char *paren = strchr(line + 4, '(');
        if (paren) {
            *paren = '\0';
            char *fn_name = line + 4;
            while (*fn_name == ' ') fn_name++;
            char *args_str = paren + 1;
            char *close_p = strrchr(args_str, ')');
            if (close_p) *close_p = '\0';

            if (strlen(active_class) > 0 && strcmp(fn_name, "__init__") == 0) {
                emit("\n%s %s_init(", active_class, active_class);
                char *arg = strtok(args_str, ",");
                int fst = 1;
                while (arg) {
                    while (*arg == ' ') arg++;
                    if (strcmp(arg, "self") != 0) {
                        if (!fst) emit(", ");
                        if (strcmp(arg, "nome") == 0 || strcmp(arg, "especie") == 0) emit("const char *%s", arg);
                        else emit("int64_t %s", arg);
                        fst = 0;
                    }
                    arg = strtok(NULL, ",");
                }
                emit(") {\n");
                emit("    %s self = {0};\n", active_class);
                pending_block = 1;
                pending_type = BLOCK_INIT;
                return;
            }

            if (strlen(active_class) > 0) {
                emit("\nvoid %s_%s(%s *self", active_class, fn_name, active_class);
                char *arg = strtok(args_str, ",");
                while (arg) {
                    while (*arg == ' ') arg++;
                    if (strcmp(arg, "self") != 0) emit(", int64_t %s", arg);
                    arg = strtok(NULL, ",");
                }
                emit(") {\n");
                pending_block = 1;
                pending_type = BLOCK_FUNC;
                return;
            }

            emit("\nint64_t %s(", fn_name);
            char *arg = strtok(args_str, ",");
            int fst = 1;
            while (arg) {
                while (*arg == ' ') arg++;
                if (!fst) emit(", ");
                emit("int64_t %s", arg);
                fst = 0;
                arg = strtok(NULL, ",");
            }
            if (fst) emit("void");
            emit(") {\n");
            pending_block = 1;
            pending_type = BLOCK_FUNC;
        }
        return;
    }

    if (strncmp(line, "return ", 7) == 0) {
        char expr[512];
        strncpy(expr, line + 7, sizeof(expr) - 1);
        normalize_quotes_in_str(expr);
        replace_operators(expr);
        transform_advanced_expressions(expr);
        emit("    return %s;\n", expr);
        return;
    }
    if (strcmp(line, "return") == 0) {
        if (inside_function && strlen(active_class) > 0) emit("    return self;\n");
        else emit("    return 0;\n");
        return;
    }
    if (strcmp(line, "break") == 0) { emit("    break;\n"); return; }
    if (strcmp(line, "continue") == 0) { emit("    continue;\n"); return; }
    if (strcmp(line, "pass") == 0) { emit("    /* pass */;\n"); return; }

    // Loop For in range(...)
    if (strncmp(line, "for ", 4) == 0 && strstr(line, " in range(") && line[len - 1] == ':') {
        line[len - 1] = '\0';
        char var_name[64] = "";
        char *in_ptr = strstr(line + 4, " in range(");
        if (in_ptr) {
            *in_ptr = '\0';
            strncpy(var_name, line + 4, sizeof(var_name) - 1);
            while (var_name[strlen(var_name)-1] == ' ') var_name[strlen(var_name)-1] = '\0';

            char *r_args = in_ptr + 10;
            char *close_p = strrchr(r_args, ')');
            if (close_p) *close_p = '\0';

            char *arg1 = strtok(r_args, ",");
            char *arg2 = strtok(NULL, ",");
            char *arg3 = strtok(NULL, ",");

            register_var(var_name, VAR_INT, NULL);
            if (!arg2) {
                emit("    for (%s = 0; %s < %s; %s++) {\n", var_name, var_name, arg1, var_name);
            } else if (!arg3) {
                emit("    for (%s = %s; %s < %s; %s++) {\n", var_name, arg1, var_name, arg2, var_name);
            } else {
                int step = atoi(arg3);
                if (step < 0) emit("    for (%s = %s; %s > %s; %s += %s) {\n", var_name, arg1, var_name, arg2, var_name, arg3);
                else emit("    for (%s = %s; %s < %s; %s += %s) {\n", var_name, arg1, var_name, arg2, var_name, arg3);
            }
            pending_block = 1;
            pending_type = BLOCK_LOOP;
        }
        return;
    }

    if (strncmp(line, "while ", 6) == 0 && line[len - 1] == ':') {
        line[len - 1] = '\0';
        char cond[512];
        strncpy(cond, line + 6, sizeof(cond) - 1);
        normalize_quotes_in_str(cond);
        replace_operators(cond);
        transform_advanced_expressions(cond);
        emit("    while (%s) {\n", cond);
        pending_block = 1;
        pending_type = BLOCK_LOOP;
        return;
    }

    if (strncmp(line, "if ", 3) == 0 && line[len - 1] == ':') {
        line[len - 1] = '\0';
        char cond[512];
        strncpy(cond, line + 3, sizeof(cond) - 1);
        normalize_quotes_in_str(cond);
        replace_operators(cond);
        transform_advanced_expressions(cond);
        emit("    if (%s) {\n", cond);
        pending_block = 1;
        pending_type = BLOCK_IF;
        return;
    }

    if (is_elif) {
        line[len - 1] = '\0';
        char cond[512];
        strncpy(cond, line + 5, sizeof(cond) - 1);
        normalize_quotes_in_str(cond);
        replace_operators(cond);
        transform_advanced_expressions(cond);
        emit("    } else if (%s) {\n", cond);
        pending_block = 1;
        pending_type = BLOCK_IF;
        return;
    }

    if (is_else) {
        emit("    } else {\n");
        pending_block = 1;
        pending_type = BLOCK_IF;
        return;
    }

    if (strncmp(line, "print(", 6) == 0 && line[len - 1] == ')') {
        line[len - 1] = '\0';
        transpile_print(line + 6);
        return;
    }

    // Métodos de Lista e Arquivo
    char *dot_write = strstr(line, ".write(");
    if (dot_write && line[len - 1] == ')') {
        *dot_write = '\0';
        char *vname = line; while (*vname == ' ') vname++;
        char *arg = dot_write + 7; arg[strlen(arg) - 1] = '\0';
        normalize_quotes_in_str(arg);
        emit("    py_file_write(&%s, %s);\n", vname, arg);
        return;
    }

    char *dot_close = strstr(line, ".close()");
    if (dot_close) {
        *dot_close = '\0';
        char *vname = line; while (*vname == ' ') vname++;
        emit("    py_file_close(&%s);\n", vname);
        return;
    }

    char *dot_append = strstr(line, ".append(");
    if (dot_append && line[len - 1] == ')') {
        *dot_append = '\0';
        char *vname = line; while (*vname == ' ') vname++;
        char *arg = dot_append + 8; arg[strlen(arg) - 1] = '\0';
        normalize_quotes_in_str(arg);
        replace_operators(arg);
        transform_advanced_expressions(arg);
        emit("    %s[len_%s++] = (int64_t)(%s);\n", vname, vname, arg);
        return;
    }

    char *dot_pop = strstr(line, ".pop()");
    if (dot_pop) {
        *dot_pop = '\0';
        char *vname = line; while (*vname == ' ') vname++;
        emit("    if (len_%s > 0) len_%s--;\n", vname, vname);
        return;
    }

    char *dot_reverse = strstr(line, ".reverse()");
    if (dot_reverse) {
        *dot_reverse = '\0';
        char *vname = line; while (*vname == ' ') vname++;
        emit("    py_list_reverse(%s, len_%s);\n", vname, vname);
        return;
    }

    char *dot_clear = strstr(line, ".clear()");
    if (dot_clear) {
        *dot_clear = '\0';
        char *vname = line; while (*vname == ' ') vname++;
        emit("    len_%s = 0;\n", vname);
        return;
    }

    // Atribuições compostas (+=, -=, *=, /=, %=)
    char *op_eq = NULL;
    if ((op_eq = strstr(line, "+=")) || (op_eq = strstr(line, "-=")) ||
        (op_eq = strstr(line, "*=")) || (op_eq = strstr(line, "/=")) ||
        (op_eq = strstr(line, "%%="))) {
        char op_symbol[3] = { op_eq[0], op_eq[1], '\0' };
        *op_eq = '\0';
        char *vstart = line; while (*vstart == ' ') vstart++;
        char *vend = vstart + strlen(vstart) - 1;
        while (vend > vstart && isspace((unsigned char)*vend)) *vend-- = '\0';
        char *val_expr = op_eq + 2; while (*val_expr == ' ') val_expr++;
        normalize_quotes_in_str(vstart); replace_operators(vstart);
        normalize_quotes_in_str(val_expr); replace_operators(val_expr);
        transform_advanced_expressions(val_expr);
        emit("    %s %s %s;\n", vstart, op_symbol, val_expr);
        return;
    }

    // Atribuições e Lambdas
    char *eq = strchr(line, '=');
    if (eq && line[0] != '=' && *(eq + 1) != '=' && *(eq - 1) != '!' && *(eq - 1) != '<' && *(eq - 1) != '>') {
        *eq = '\0';
        char var_name[128], val_expr[2048];
        strncpy(var_name, line, sizeof(var_name) - 1);
        strncpy(val_expr, eq + 1, sizeof(val_expr) - 1);

        size_t vl = strlen(var_name);
        while (vl > 0 && (var_name[vl-1] == ' ' || var_name[vl-1] == '\t')) var_name[--vl] = '\0';
        char *vstart = var_name; while (*vstart == ' ' || *vstart == '\t') vstart++;
        char *vexpr_start = val_expr; while (*vexpr_start == ' ' || *vexpr_start == '\t') vexpr_start++;
        size_t elen = strlen(vexpr_start);
        while (elen > 0 && (vexpr_start[elen-1] == ' ' || vexpr_start[elen-1] == '\t')) vexpr_start[--elen] = '\0';

        normalize_quotes_in_str(vexpr_start);

        // --- LAMBDAS ---
        if (strncmp(vexpr_start, "lambda ", 7) == 0 && strchr(vexpr_start, ':')) {
            char *colon = strchr(vexpr_start, ':');
            *colon = '\0';
            char *l_params = vexpr_start + 7; while (*l_params == ' ') l_params++;
            char *l_body = colon + 1; while (*l_body == ' ') l_body++;

            replace_operators(l_body);
            transform_advanced_expressions(l_body);

            char fn_decl[512] = "";
            char *p_tok = strtok(l_params, ",");
            int fst = 1;
            while (p_tok) {
                while (*p_tok == ' ') p_tok++;
                if (!fst) strcat(fn_decl, ", ");
                strcat(fn_decl, "int64_t "); strcat(fn_decl, p_tok);
                fst = 0;
                p_tok = strtok(NULL, ",");
            }

            int prev_fn = inside_function;
            inside_function = 1;
            emit("\nstatic inline int64_t %s(%s) {\n    return %s;\n}\n", vstart, fn_decl, l_body);
            inside_function = prev_fn;
            return;
        }

        // --- LIST COMPREHENSIONS ---
        char *lcomp_start = strchr(vexpr_start, '[');
        char *lcomp_for = lcomp_start ? strstr(lcomp_start, " for ") : NULL;
        char *lcomp_in = lcomp_for ? strstr(lcomp_for, " in ") : NULL;
        char *lcomp_end = lcomp_in ? strrchr(lcomp_in, ']') : NULL;

        if (lcomp_start == vexpr_start && lcomp_for && lcomp_in && lcomp_end && lcomp_end == vexpr_start + elen - 1) {
            *lcomp_end = '\0';
            char *item_expr = lcomp_start + 1;
            *lcomp_for = '\0';
            char *var_item = lcomp_for + 5;
            *lcomp_in = '\0';
            char *iter_expr = lcomp_in + 4;

            char *cond_if = strstr(iter_expr, " if ");
            char cond_expr[256] = "";
            if (cond_if) {
                *cond_if = '\0';
                strncpy(cond_expr, cond_if + 4, sizeof(cond_expr) - 1);
                replace_operators(cond_expr);
                transform_advanced_expressions(cond_expr);
            }

            while (*item_expr == ' ') item_expr++;
            while (*var_item == ' ') var_item++;
            while (*iter_expr == ' ') iter_expr++;

            register_var(vstart, VAR_LIST, NULL);
            emit("    len_%s = 0;\n", vstart);

            if (strncmp(iter_expr, "range(", 6) == 0) {
                char *r_args = iter_expr + 6;
                char *r_close = strrchr(r_args, ')');
                if (r_close) *r_close = '\0';
                char *a1 = strtok(r_args, ",");
                char *a2 = strtok(NULL, ",");
                char *a3 = strtok(NULL, ",");

                if (!a2) emit("    for (int64_t %s = 0; %s < %s; %s++) {\n", var_item, var_item, a1, var_item);
                else if (!a3) emit("    for (int64_t %s = %s; %s < %s; %s++) {\n", var_item, a1, var_item, a2, var_item);
                else emit("    for (int64_t %s = %s; %s < %s; %s += %s) {\n", var_item, a1, var_item, a2, var_item, a3);
            } else {
                emit("    for (int64_t _i = 0; _i < len_%s; _i++) {\n", iter_expr);
                emit("        int64_t %s = %s[_i];\n", var_item, iter_expr);
            }

            replace_operators(item_expr);
            transform_advanced_expressions(item_expr);

            if (strlen(cond_expr) > 0) {
                emit("        if (%s) {\n", cond_expr);
                emit("            %s[len_%s++] = (int64_t)(%s);\n", vstart, vstart, item_expr);
                emit("        }\n");
            } else {
                emit("        %s[len_%s++] = (int64_t)(%s);\n", vstart, vstart, item_expr);
            }
            emit("    }\n");
            return;
        }

        // Desempacotamento Múltiplo
        if (strchr(vstart, ',') && strchr(vexpr_start, ',')) {
            char *v1 = strtok(vstart, ","); char *v2 = strtok(NULL, ",");
            char *e1 = strtok(vexpr_start, ","); char *e2 = strtok(NULL, ",");
            if (v1 && v2 && e1 && e2) {
                while (*v1 == ' ') v1++; while (*v2 == ' ') v2++;
                while (*e1 == ' ') e1++; while (*e2 == ' ') e2++;
                register_var(v1, VAR_INT, NULL); register_var(v2, VAR_INT, NULL);
                emit("    %s = %s;\n    %s = %s;\n", v1, e1, v2, e2);
                return;
            }
        }

        // Abertura de Arquivo
        if (strncmp(vexpr_start, "open(", 5) == 0) {
            register_var(vstart, VAR_FILE, NULL);
            char *o_args = vexpr_start + 5;
            char *cl_p = strrchr(o_args, ')');
            if (cl_p) *cl_p = '\0';
            emit("    %s = py_open(%s);\n", vstart, o_args);
            return;
        }

        // Atribuição de Campo self
        if (strncmp(vstart, "self.", 5) == 0) {
            char *field = vstart + 5;
            replace_operators(vexpr_start); transform_advanced_expressions(vexpr_start);
            if (inside_function && strlen(active_class) > 0) emit("    self.%s = %s;\n", field, vexpr_start);
            else emit("    self->%s = %s;\n", field, vexpr_start);
            return;
        }

        // Instanciação de Objeto
        for (int c = 0; c < class_count; c++) {
            size_t cn_len = strlen(classes[c].class_name);
            if (strncmp(vexpr_start, classes[c].class_name, cn_len) == 0 && vexpr_start[cn_len] == '(') {
                register_var(vstart, VAR_OBJ, classes[c].class_name);
                char *init_args = vexpr_start + cn_len + 1;
                char *cl_p = strrchr(init_args, ')');
                if (cl_p) *cl_p = '\0';
                emit("    %s = %s_init(%s);\n", vstart, classes[c].class_name, init_args);
                return;
            }
        }

        // Modificação de Chave de Dicionário ou Lista
        char *bracket_in_lhs = strchr(vstart, '[');
        if (bracket_in_lhs) {
            *bracket_in_lhs = '\0';
            char *d_key = bracket_in_lhs + 1;
            char *b_close = strchr(d_key, ']');
            if (b_close) *b_close = '\0';
            normalize_quotes_in_str(d_key);
            symbol_t *dsym = find_symbol(vstart);
            if (dsym && dsym->type == VAR_DICT) {
                replace_operators(vexpr_start); transform_advanced_expressions(vexpr_start);
                if (vexpr_start[0] == '"') emit("    py_dict_set_str(&%s, %s, %s);\n", vstart, d_key, vexpr_start);
                else if (strchr(vexpr_start, '.') && isdigit((unsigned char)*(strchr(vexpr_start, '.') + 1))) emit("    py_dict_set_float(&%s, %s, %s);\n", vstart, d_key, vexpr_start);
                else emit("    py_dict_set_int(&%s, %s, %s);\n", vstart, d_key, vexpr_start);
                return;
            } else if (dsym && dsym->type == VAR_LIST) {
                replace_operators(vexpr_start); transform_advanced_expressions(vexpr_start);
                emit("    %s[%s] = (int64_t)(%s);\n", vstart, d_key, vexpr_start);
                return;
            }
        }

        // Dicionários Literais
        if (vexpr_start[0] == '{' && vexpr_start[elen - 1] == '}') {
            register_var(vstart, VAR_DICT, NULL);
            emit("    py_dict_init(&%s);\n", vstart);
            char inner_dict[2048];
            snprintf(inner_dict, sizeof(inner_dict), "%.*s", (int)(elen - 2), vexpr_start + 1);

            char *pair = inner_dict;
            while (*pair) {
                while (*pair == ' ' || *pair == ',' || *pair == '\n' || *pair == '\r' || *pair == '\t') pair++;
                if (!*pair) break;
                char *colon = strchr(pair, ':');
                if (!colon) break;
                *colon = '\0';
                char *k_raw = pair; char *v_raw = colon + 1;
                while (*v_raw == ' ') v_raw++;
                char *next_comma = strchr(v_raw, ',');
                if (next_comma) { *next_comma = '\0'; pair = next_comma + 1; }
                else { pair = v_raw + strlen(v_raw); }

                while (*k_raw == ' ') k_raw++;
                size_t kl = strlen(k_raw);
                while (kl > 0 && k_raw[kl-1] == ' ') k_raw[--kl] = '\0';
                size_t vl_len = strlen(v_raw);
                while (vl_len > 0 && (v_raw[vl_len-1] == ' ' || v_raw[vl_len-1] == '\n')) v_raw[--vl_len] = '\0';

                normalize_quotes_in_str(k_raw); normalize_quotes_in_str(v_raw);
                replace_operators(v_raw); transform_advanced_expressions(v_raw);

                if (v_raw[0] == '"') emit("    py_dict_set_str(&%s, %s, %s);\n", vstart, k_raw, v_raw);
                else if (strchr(v_raw, '.') && isdigit((unsigned char)*(strchr(v_raw, '.') + 1))) emit("    py_dict_set_float(&%s, %s, %s);\n", vstart, k_raw, v_raw);
                else emit("    py_dict_set_int(&%s, %s, %s);\n", vstart, k_raw, v_raw);
            }
            return;
        }

        // Listas Literais
        if (vexpr_start[0] == '[' && vexpr_start[elen - 1] == ']') {
            register_var(vstart, VAR_LIST, NULL);
            if (elen > 2) {
                char items_only[2048];
                snprintf(items_only, sizeof(items_only), "%.*s", (int)(elen - 2), vexpr_start + 1);
                int elem_count = 1;
                for (size_t c = 0; items_only[c]; c++) if (items_only[c] == ',') elem_count++;
                emit("    { static const int64_t _init[] = {%s}; memcpy(%s, _init, sizeof(_init)); len_%s = %d; }\n",
                     items_only, vstart, vstart, elem_count);
            } else {
                emit("    len_%s = 0;\n", vstart);
            }
            return;
        }

        replace_operators(vexpr_start);
        transform_advanced_expressions(vexpr_start);

        int is_str = is_string_expression(vexpr_start);
        int is_flt = is_float_expression(vexpr_start);

        if (is_str) register_var(vstart, VAR_STR, NULL);
        else if (is_flt) register_var(vstart, VAR_FLOAT, NULL);
        else register_var(vstart, VAR_INT, NULL);

        emit("    %s = %s;\n", vstart, vexpr_start);
        return;
    }

    normalize_quotes_in_str(line);
    replace_operators(line);
    transform_advanced_expressions(line);
    emit("    %s;\n", line);
}

static void run_interactive_repl(void) {
    printf("\n%sPython 3.12 (pythont 1.0-release JIT Native Engine) on Linux%s\n", COLOR_OK, COLOR_RESET);
    printf("Type \"help\", \"exit()\" or \"quit()\" for more information.\n\n");

    char line_buf[1024];
    while (1) {
        printf("%s>>> %s", COLOR_VAL, COLOR_RESET);
        fflush(stdout);

        if (!fgets(line_buf, sizeof(line_buf), stdin)) break;
        size_t l = strlen(line_buf);
        while (l > 0 && (line_buf[l-1] == '\r' || line_buf[l-1] == '\n')) line_buf[--l] = '\0';
        if (l == 0) continue;

        if (strcmp(line_buf, "exit()") == 0 || strcmp(line_buf, "quit()") == 0 || strcmp(line_buf, "exit") == 0) break;

        char exec_cmd[2048];
        snprintf(exec_cmd, sizeof(exec_cmd), "./pythont -e \"%s\"", line_buf);
        (void)!system(exec_cmd);
    }
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 2) {
        if (isatty(STDIN_FILENO)) {
            run_interactive_repl();
            utilipc_close();
            return 0;
        }
    }

    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_help();
        utilipc_close();
        return 0;
    }

    const char *py_file = NULL;
    const char *inline_code = NULL;
    const char *out_bin = NULL;
    int emit_c_only = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) inline_code = argv[++i];
        else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--emit-c") == 0) emit_c_only = 1;
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out_bin = argv[++i];
        else if (!py_file && !inline_code) py_file = argv[i];
    }

    if (inline_code) {
        char *code_copy = strdup(inline_code);
        char *saveptr = NULL;
        char *line = strtok_r(code_copy, ";\n", &saveptr);
        while (line) {
            transpile_line(line, 0);
            line = strtok_r(NULL, ";\n", &saveptr);
        }
        free(code_copy);
    } else if (py_file) {
        FILE *fp = fopen(py_file, "r");
        if (!fp) {
            fprintf(stderr, "pythont: erro ao abrir '%s': %s\n", py_file, strerror(errno));
            utilipc_close();
            return 1;
        }

        char line_raw[1024];
        char accum[4096] = "";
        int accum_indent = 0;
        int open_braces = 0, open_brackets = 0, open_parens = 0;

        while (fgets(line_raw, sizeof(line_raw), fp)) {
            int indent = 0;
            while (line_raw[indent] == ' ') indent++;
            if (line_raw[indent] == '\t') indent += 4;

            char *trimmed = line_raw + indent;
            if (*trimmed == '\0' || *trimmed == '\n' || *trimmed == '\r' || *trimmed == '#') continue;

            if (accum[0] == '\0') accum_indent = indent;

            int in_s = 0; char q_c = 0;
            for (size_t k = 0; trimmed[k]; k++) {
                char c = trimmed[k];
                if (in_s) {
                    if (c == '\\' && trimmed[k+1]) { k++; continue; }
                    if (c == q_c) in_s = 0;
                } else {
                    if (c == '"' || c == '\'') { in_s = 1; q_c = c; }
                    else if (c == '{') open_braces++;
                    else if (c == '}') { if (open_braces > 0) open_braces--; }
                    else if (c == '[') open_brackets++;
                    else if (c == ']') { if (open_brackets > 0) open_brackets--; }
                    else if (c == '(') open_parens++;
                    else if (c == ')') { if (open_parens > 0) open_parens--; }
                }
            }

            size_t tl = strlen(trimmed);
            while (tl > 0 && (trimmed[tl-1] == '\r' || trimmed[tl-1] == '\n')) trimmed[--tl] = '\0';
            if (accum[0] != '\0') strcat(accum, " ");
            strcat(accum, trimmed);

            if (open_braces == 0 && open_brackets == 0 && open_parens == 0) {
                transpile_line(accum, accum_indent);
                accum[0] = '\0';
            }
        }
        if (accum[0] != '\0') transpile_line(accum, accum_indent);
        fclose(fp);
    } else {
        print_help();
        utilipc_close();
        return 1;
    }

    handle_dedent(0, 0);

    // Hoisting de variáveis
    char var_decl_buf[65536] = "";
    for (int i = 0; i < symbol_count; i++) {
        if (!symbols[i].is_global) continue;
        char dline[256];
        if (symbols[i].type == VAR_INT) snprintf(dline, sizeof(dline), "    int64_t %s = 0;\n", symbols[i].name);
        else if (symbols[i].type == VAR_FLOAT) snprintf(dline, sizeof(dline), "    double %s = 0.0;\n", symbols[i].name);
        else if (symbols[i].type == VAR_STR) snprintf(dline, sizeof(dline), "    const char *%s = \"\";\n", symbols[i].name);
        else if (symbols[i].type == VAR_LIST) snprintf(dline, sizeof(dline), "    int64_t %s[%d] = {0};\n    int64_t len_%s = 0;\n", symbols[i].name, MAX_LIST_SZ, symbols[i].name);
        else if (symbols[i].type == VAR_DICT) snprintf(dline, sizeof(dline), "    py_dict_t %s; py_dict_init(&%s);\n", symbols[i].name, symbols[i].name);
        else if (symbols[i].type == VAR_FILE) snprintf(dline, sizeof(dline), "    py_file_t %s = {0};\n", symbols[i].name);
        else if (symbols[i].type == VAR_OBJ) snprintf(dline, sizeof(dline), "    %s %s = {0};\n", symbols[i].class_type, symbols[i].name);
        else dline[0] = '\0';
        strcat(var_decl_buf, dline);
    }

    char final_c_code[MAX_CODE_SZ];
    snprintf(final_c_code, sizeof(final_c_code),
        "/* ========================================================\n"
        "   Codigo C Nativo Gerado Automaticamente pelo pythont 1.0-release\n"
        "   ======================================================== */\n"
        "#pragma GCC diagnostic ignored \"-Wunused-function\"\n"
        "#pragma GCC diagnostic ignored \"-Wunused-variable\"\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <stdint.h>\n"
        "#include <stdbool.h>\n"
        "#include <string.h>\n"
        "#include <ctype.h>\n"
        "#include <math.h>\n\n"
        "#define py_min(a, b) (((a) < (b)) ? (a) : (b))\n"
        "#define py_max(a, b) (((a) > (b)) ? (a) : (b))\n"
        "#define py_abs(a)    llabs((int64_t)(a))\n"
        "__attribute__((unused)) static inline int64_t py_int(const char *s) { return (int64_t)strtoll(s, NULL, 10); }\n"
        "__attribute__((unused)) static inline double py_float(const char *s) { return strtod(s, NULL); }\n"
        "__attribute__((unused)) static inline int64_t py_round(double d) { return (int64_t)round(d); }\n"
        "__attribute__((unused)) static inline const char *py_str(int64_t val) {\n"
        "    static char s_buf[64];\n"
        "    snprintf(s_buf, sizeof(s_buf), \"%%lld\", (long long)val);\n"
        "    return s_buf;\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_bin(int64_t val) {\n"
        "    static char b_buf[70];\n"
        "    b_buf[0] = '0'; b_buf[1] = 'b'; int pos = 2;\n"
        "    if (val == 0) { b_buf[pos++] = '0'; b_buf[pos] = '\\0'; return b_buf; }\n"
        "    char rev[64]; int r = 0;\n"
        "    uint64_t u = (uint64_t)val;\n"
        "    while (u > 0) { rev[r++] = (u & 1) ? '1' : '0'; u >>= 1; }\n"
        "    for (int i = r - 1; i >= 0; i--) b_buf[pos++] = rev[i];\n"
        "    b_buf[pos] = '\\0';\n"
        "    return b_buf;\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_hex(int64_t val) {\n"
        "    static char h_buf[32]; snprintf(h_buf, sizeof(h_buf), \"0x%%llx\", (unsigned long long)val); return h_buf;\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_oct(int64_t val) {\n"
        "    static char o_buf[32]; snprintf(o_buf, sizeof(o_buf), \"0o%%llo\", (unsigned long long)val); return o_buf;\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_chr(int64_t val) {\n"
        "    static char c_buf[2]; c_buf[0] = (char)val; c_buf[1] = '\\0'; return c_buf;\n"
        "}\n"
        "__attribute__((unused)) static inline int64_t py_ord(const char *s) {\n"
        "    return s ? (int64_t)(unsigned char)s[0] : 0;\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_list_repr(const int64_t *arr, int len) {\n"
        "    static char list_buf[4096];\n"
        "    list_buf[0] = '['; list_buf[1] = '\\0';\n"
        "    for (int i = 0; i < len; i++) {\n"
        "        char elem[32];\n"
        "        snprintf(elem, sizeof(elem), \"%%lld%%s\", (long long)arr[i], (i < len - 1) ? \", \" : \"\");\n"
        "        strcat(list_buf, elem);\n"
        "    }\n"
        "    strcat(list_buf, \"]\");\n"
        "    return list_buf;\n"
        "}\n"
        "__attribute__((unused)) static inline void py_list_reverse(int64_t *arr, int len) {\n"
        "    for (int i = 0; i < len / 2; i++) {\n"
        "        int64_t tmp = arr[i]; arr[i] = arr[len - 1 - i]; arr[len - 1 - i] = tmp;\n"
        "    }\n"
        "}\n"
        "__attribute__((unused)) static inline int64_t py_list_count(const int64_t *arr, int len, int64_t val) {\n"
        "    int64_t cnt = 0;\n"
        "    for (int i = 0; i < len; i++) if (arr[i] == val) cnt++;\n"
        "    return cnt;\n"
        "}\n"
        "__attribute__((unused)) static inline int64_t py_sum(const int64_t *arr, int len) {\n"
        "    int64_t acc = 0;\n"
        "    for (int i = 0; i < len; i++) acc += arr[i];\n"
        "    return acc;\n"
        "}\n"
        "__attribute__((unused)) static inline char *py_input(const char *prompt) {\n"
        "    if (prompt && *prompt) { printf(\"%%s\", prompt); fflush(stdout); }\n"
        "    static char in_buf[1024];\n"
        "    if (!fgets(in_buf, sizeof(in_buf), stdin)) return \"\";\n"
        "    size_t l = strlen(in_buf);\n"
        "    while (l > 0 && (in_buf[l-1] == '\\r' || in_buf[l-1] == '\\n')) in_buf[--l] = '\\0';\n"
        "    return in_buf;\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_str_upper(const char *s) {\n"
        "    static char u_buf[1024]; size_t i = 0;\n"
        "    for (; s[i] && i < 1023; i++) u_buf[i] = (char)toupper((unsigned char)s[i]);\n"
        "    u_buf[i] = '\\0'; return u_buf;\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_str_lower(const char *s) {\n"
        "    static char l_buf[1024]; size_t i = 0;\n"
        "    for (; s[i] && i < 1023; i++) l_buf[i] = (char)tolower((unsigned char)s[i]);\n"
        "    l_buf[i] = '\\0'; return l_buf;\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_str_strip(const char *s) {\n"
        "    static char st_buf[1024];\n"
        "    while (*s && isspace((unsigned char)*s)) s++;\n"
        "    strncpy(st_buf, s, 1023); st_buf[1023] = '\\0';\n"
        "    size_t l = strlen(st_buf);\n"
        "    while (l > 0 && isspace((unsigned char)st_buf[l - 1])) st_buf[--l] = '\\0';\n"
        "    return st_buf;\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_str_capitalize(const char *s) {\n"
        "    static char c_buf[1024];\n"
        "    strncpy(c_buf, s, 1023); c_buf[1023] = '\\0';\n"
        "    if (c_buf[0]) c_buf[0] = (char)toupper((unsigned char)c_buf[0]);\n"
        "    for (size_t i = 1; c_buf[i]; i++) c_buf[i] = (char)tolower((unsigned char)c_buf[i]);\n"
        "    return c_buf;\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_str_title(const char *s) {\n"
        "    static char t_buf[1024];\n"
        "    strncpy(t_buf, s, 1023); t_buf[1023] = '\\0';\n"
        "    int cap = 1;\n"
        "    for (size_t i = 0; t_buf[i]; i++) {\n"
        "        if (isspace((unsigned char)t_buf[i])) cap = 1;\n"
        "        else if (cap) { t_buf[i] = (char)toupper((unsigned char)t_buf[i]); cap = 0; }\n"
        "        else t_buf[i] = (char)tolower((unsigned char)t_buf[i]);\n"
        "    }\n"
        "    return t_buf;\n"
        "}\n"
        "__attribute__((unused)) static inline int64_t py_str_startswith(const char *s, const char *prefix) {\n"
        "    if (!s || !prefix) return 0;\n"
        "    return strncmp(s, prefix, strlen(prefix)) == 0;\n"
        "}\n"
        "__attribute__((unused)) static inline int64_t py_str_endswith(const char *s, const char *suffix) {\n"
        "    if (!s || !suffix) return 0;\n"
        "    size_t sl = strlen(s), sufl = strlen(suffix);\n"
        "    if (sufl > sl) return 0;\n"
        "    return strcmp(s + sl - sufl, suffix) == 0;\n"
        "}\n"
        "__attribute__((unused)) static inline int64_t py_str_count(const char *s, const char *sub) {\n"
        "    if (!s || !sub || !*sub) return 0;\n"
        "    int64_t count = 0; size_t sub_len = strlen(sub);\n"
        "    while ((s = strstr(s, sub)) != NULL) { count++; s += sub_len; }\n"
        "    return count;\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_str_replace(const char *s, const char *old_w, const char *new_w) {\n"
        "    static char rep_buf[2048]; rep_buf[0] = '\\0';\n"
        "    if (!s || !old_w || !new_w) return s ? s : \"\";\n"
        "    const char *p = s; size_t old_len = strlen(old_w);\n"
        "    while (*p) {\n"
        "        const char *found = strstr(p, old_w);\n"
        "        if (found) { strncat(rep_buf, p, found - p); strcat(rep_buf, new_w); p = found + old_len; }\n"
        "        else { strcat(rep_buf, p); break; }\n"
        "    }\n"
        "    return rep_buf;\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_str_slice(const char *s, int64_t start, int64_t end, int64_t step) {\n"
        "    static char slice_buf[1024]; if (!s) return \"\";\n"
        "    int64_t len = (int64_t)strlen(s);\n"
        "    if (step == 0) step = 1;\n"
        "    if (step == -1 && start == 0 && end == 999999) {\n"
        "        int64_t pos = 0;\n"
        "        for (int64_t i = len - 1; i >= 0 && pos < 1023; i--) slice_buf[pos++] = s[i];\n"
        "        slice_buf[pos] = '\\0'; return slice_buf;\n"
        "    }\n"
        "    if (start < 0) start += len; if (end < 0) end += len;\n"
        "    if (start < 0) start = 0; if (end > len) end = len;\n"
        "    int64_t pos = 0;\n"
        "    for (int64_t i = start; (step > 0 ? i < end : i > end) && pos < 1023; i += step) {\n"
        "        if (i >= 0 && i < len) slice_buf[pos++] = s[i];\n"
        "    }\n"
        "    slice_buf[pos] = '\\0'; return slice_buf;\n"
        "}\n\n"
        "typedef struct {\n"
        "    FILE *fp; int is_open;\n"
        "} py_file_t;\n\n"
        "__attribute__((unused)) static inline py_file_t py_open(const char *path, const char *mode) {\n"
        "    py_file_t pf; pf.fp = fopen(path, mode); pf.is_open = (pf.fp != NULL); return pf;\n"
        "}\n"
        "__attribute__((unused)) static inline void py_file_write(py_file_t *pf, const char *str) {\n"
        "    if (pf && pf->fp) { fputs(str, pf->fp); fflush(pf->fp); }\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_file_read(py_file_t *pf) {\n"
        "    static char file_read_buf[65536]; file_read_buf[0] = '\\0';\n"
        "    if (pf && pf->fp) {\n"
        "        size_t n = fread(file_read_buf, 1, sizeof(file_read_buf) - 1, pf->fp);\n"
        "        file_read_buf[n] = '\\0';\n"
        "    }\n"
        "    return file_read_buf;\n"
        "}\n"
        "__attribute__((unused)) static inline void py_file_close(py_file_t *pf) {\n"
        "    if (pf && pf->fp) { fclose(pf->fp); pf->fp = NULL; pf->is_open = 0; }\n"
        "}\n\n"
        "typedef struct {\n"
        "    char key[64]; char val[256]; int used;\n"
        "} py_dict_entry_t;\n\n"
        "typedef struct {\n"
        "    py_dict_entry_t entries[64]; int count;\n"
        "} py_dict_t;\n\n"
        "__attribute__((unused)) static inline void py_dict_init(py_dict_t *d) { memset(d, 0, sizeof(py_dict_t)); }\n"
        "__attribute__((unused)) static inline void py_dict_set_str(py_dict_t *d, const char *k, const char *v) {\n"
        "    for (int i = 0; i < d->count; i++) {\n"
        "        if (d->entries[i].used && strcmp(d->entries[i].key, k) == 0) { strncpy(d->entries[i].val, v, 255); return; }\n"
        "    }\n"
        "    if (d->count < 64) {\n"
        "        strncpy(d->entries[d->count].key, k, 63); strncpy(d->entries[d->count].val, v, 255);\n"
        "        d->entries[d->count].used = 1; d->count++;\n"
        "    }\n"
        "}\n"
        "__attribute__((unused)) static inline void py_dict_set_int(py_dict_t *d, const char *k, int64_t v) {\n"
        "    char b[64]; snprintf(b, sizeof(b), \"%%lld\", (long long)v); py_dict_set_str(d, k, b);\n"
        "}\n"
        "__attribute__((unused)) static inline void py_dict_set_float(py_dict_t *d, const char *k, double v) {\n"
        "    char b[64]; snprintf(b, sizeof(b), \"%%f\", v); py_dict_set_str(d, k, b);\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_dict_get_val(const py_dict_t *d, const char *k) {\n"
        "    for (int i = 0; i < d->count; i++) {\n"
        "        if (d->entries[i].used && strcmp(d->entries[i].key, k) == 0) return d->entries[i].val;\n"
        "    }\n"
        "    return \"None\";\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_dict_get_default(const py_dict_t *d, const char *k, const char *def) {\n"
        "    for (int i = 0; i < d->count; i++) {\n"
        "        if (d->entries[i].used && strcmp(d->entries[i].key, k) == 0) return d->entries[i].val;\n"
        "    }\n"
        "    return def;\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_dict_keys(const py_dict_t *d) {\n"
        "    static char k_buf[2048]; k_buf[0] = '['; k_buf[1] = '\\0';\n"
        "    for (int i = 0; i < d->count; i++) {\n"
        "        if (d->entries[i].used) {\n"
        "            char tmp[128]; snprintf(tmp, sizeof(tmp), \"'%%s'%%s\", d->entries[i].key, (i < d->count - 1) ? \", \" : \"\");\n"
        "            strcat(k_buf, tmp);\n"
        "        }\n"
        "    }\n"
        "    strcat(k_buf, \"]\"); return k_buf;\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_dict_values(const py_dict_t *d) {\n"
        "    static char v_buf[2048]; v_buf[0] = '['; v_buf[1] = '\\0';\n"
        "    for (int i = 0; i < d->count; i++) {\n"
        "        if (d->entries[i].used) {\n"
        "            char tmp[128]; snprintf(tmp, sizeof(tmp), \"%%s%%s\", d->entries[i].val, (i < d->count - 1) ? \", \" : \"\");\n"
        "            strcat(v_buf, tmp);\n"
        "        }\n"
        "    }\n"
        "    strcat(v_buf, \"]\"); return v_buf;\n"
        "}\n\n"
        "%s\n"
        "%s\n"
        "int main(int argc, char *argv[]) {\n"
        "    (void)argc; (void)argv;\n"
        "%s\n"
        "%s\n"
        "    return 0;\n"
        "}\n",
        class_struct_buffer, func_buffer, var_decl_buf, main_buffer);

    if (emit_c_only) {
        printf("%s\n", final_c_code);
        utilipc_close();
        return 0;
    }

    const char *tmp_dir = get_tmp_dir();
    char tmp_c_path[512];
    snprintf(tmp_c_path, sizeof(tmp_c_path), "%s/pythont_%d.c", tmp_dir, getpid());

    FILE *c_fp = fopen(tmp_c_path, "w");
    if (!c_fp) {
        snprintf(tmp_c_path, sizeof(tmp_c_path), "pythont_%d.c", getpid());
        c_fp = fopen(tmp_c_path, "w");
    }
    if (!c_fp) {
        fprintf(stderr, "pythont: falha ao criar arquivo C temporario\n");
        utilipc_close();
        return 1;
    }
    fputs(final_c_code, c_fp);
    fclose(c_fp);

    char bin_path[512];
    int run_after = 0;

    if (out_bin) {
        strncpy(bin_path, out_bin, sizeof(bin_path) - 1);
    } else {
        snprintf(bin_path, sizeof(bin_path), "%s/pythont_bin_%d", tmp_dir, getpid());
        run_after = 1;
    }

    char compile_cmd[1024];
    snprintf(compile_cmd, sizeof(compile_cmd), "gcc -Wno-unused-function -Wno-unused-variable -O2 %s -o %s -lm", tmp_c_path, bin_path);

    int comp_res = system(compile_cmd);
    unlink(tmp_c_path);

    if (comp_res != 0) {
        fprintf(stderr, "pythont: erro de compilacao do codigo C gerado\n");
        utilipc_close();
        return 1;
    }

    if (run_after) {
        int ret = system(bin_path);
        unlink(bin_path);
        utilipc_close();
        return WEXITSTATUS(ret);
    } else {
        printf("  \033[1;32m[✔ SUCESSO]\033[0m Binario nativo C gerado em: \033[1;36m%s\033[0m\n", bin_path);
    }

    utilipc_close();
    return 0;
}
