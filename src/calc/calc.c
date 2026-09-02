#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>
#include <termios.h>
#include <assert.h>
#include "../libutilipc/utilipc.h"

/* =========================================================================
 *  CALCULADORA DE ALTA CONFIABILIDADE (NASA POWER OF 10 COMPLIANT)
 *  - Histórico com setas UP/DOWN via raw termios
 *  - Guia geral ('help' / '?') + Mini-manual específico por função
 *  - Parsing iterativo não-recursivo (Shunting-Yard)
 *  - Loops com limites estáticos e zero malloc em runtime
 * ========================================================================= */

#define CALC_MAX_EXPR     1024
#define CALC_MAX_STACK    128
#define CALC_MAX_VARS     64
#define CALC_MAX_HISTORY  64
#define CALC_MAX_STEPS    2048

#define COLOR_RESET       "\033[0m"
#define COLOR_TITLE       "\033[1;35m"
#define COLOR_OK          "\033[1;32m"
#define COLOR_ERR         "\033[1;31m"
#define COLOR_TAG         "\033[1;33m"
#define COLOR_VAL         "\033[1;36m"
#define COLOR_MUTED       "\033[0;90m"

typedef enum {
    TOK_END = 0,
    TOK_NUM,
    TOK_VAR,
    TOK_PLUS,
    TOK_MINUS,
    TOK_MUL,
    TOK_DIV,
    TOK_MOD,
    TOK_POW,
    TOK_BAND,
    TOK_BOR,
    TOK_BXOR,
    TOK_SHL,
    TOK_SHR,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_FUNC
} TokenType;

typedef struct {
    char name[32];
    double value;
    bool is_constant;
} CalcVar;

typedef struct {
    const char *name;
    const char *syntax;
    const char *desc;
    const char *domain;
    const char *example;
} FuncDoc;

typedef struct {
    CalcVar vars[CALC_MAX_VARS];
    int var_count;
    double last_ans;
    char history[CALC_MAX_HISTORY][CALC_MAX_EXPR];
    int history_count;
} CalcEngine;

static CalcEngine g_calc;

/* --- BASE DE DADOS DOS MINI-MANUAIS DE FUNÇÕES --- */
static const FuncDoc g_func_manuals[] = {
    {"sqrt",  "sqrt(x)",  "Calcula a raiz quadrada principal de x",               "x >= 0",                   "sqrt(144) = 12 | sqrt(2) = 1.4142"},
    {"cbrt",  "cbrt(x)",  "Calcula a raiz cubica de x",                           "Todos os reais",           "cbrt(27) = 3 | cbrt(-8) = -2"},
    {"fact",  "fact(n)",  "Calcula o fatorial de um numero inteiro n (n!)",        "Inteiros 0 <= n <= 170",   "fact(5) = 120 | fact(0) = 1"},
    {"exp",   "exp(x)",   "Calcula o exponencial natural e elevado a x (e^x)",    "Todos os reais (x < 709)", "exp(1) = 2.71828 | exp(2) = 7.3890"},
    {"log",   "log(x)",   "Calcula o logaritmo natural base e (ln x)",            "x > 0",                    "log(e) = 1 | log(10) = 2.3025"},
    {"ln",    "ln(x)",    "Equivalente ao logaritmo natural base e (log x)",      "x > 0",                    "ln(e) = 1 | ln(2.71828) = 1"},
    {"log10", "log10(x)", "Calcula o logaritmo comum na base 10",                "x > 0",                    "log10(100) = 2 | log10(1000) = 3"},
    {"log2",  "log2(x)",  "Calcula o logaritmo binario na base 2",                "x > 0",                    "log2(8) = 3 | log2(1024) = 10"},
    {"sin",   "sin(rad)", "Calcula o seno trigonometrico do angulo em radianos",  "Todos os reais",           "sin(pi / 2) = 1 | sin(0) = 0"},
    {"cos",   "cos(rad)", "Calcula o cosseno do angulo em radianos",             "Todos os reais",           "cos(0) = 1 | cos(pi) = -1"},
    {"tan",   "tan(rad)", "Calcula a tangente do angulo em radianos",            "rad != pi/2 + k*pi",       "tan(pi / 4) = 1 | tan(0) = 0"},
    {"asin",  "asin(x)",  "Calcula o arco seno (retorna angulo em radianos)",     "-1.0 <= x <= 1.0",         "asin(1) = 1.57079 (pi/2)"},
    {"acos",  "acos(x)",  "Calcula o arco cosseno (retorna angulo em radianos)",  "-1.0 <= x <= 1.0",         "acos(1) = 0 | acos(0) = 1.57079"},
    {"atan",  "atan(x)",  "Calcula o arco tangente (retorna angulo em radianos)", "Todos os reais",           "atan(1) = 0.78539 (pi/4)"},
    {"abs",   "abs(x)",   "Calcula o valor absoluto / modulo de x (|x|)",         "Todos os reais",           "abs(-42) = 42 | abs(5.5) = 5.5"},
    {"floor", "floor(x)", "Arredonda para o maior inteiro menor ou igual a x",    "Todos os reais",           "floor(3.9) = 3 | floor(-1.2) = -2"},
    {"ceil",  "ceil(x)",  "Arredonda para o menor inteiro maior ou igual a x",    "Todos os reais",           "ceil(3.1) = 4 | ceil(-1.9) = -1"},
    {"round", "round(x)", "Arredonda para o inteiro mais proximo",                "Todos os reais",           "round(3.5) = 4 | round(3.4) = 3"},
    {NULL, NULL, NULL, NULL, NULL}
};

/* --- GUIA GERAL DO CALC (HELP / ?) --- */
static void print_repl_help(void) {
    printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s│%s  🧮 %sGUIA GERAL DE OPERAÇÕES & COMANDOS (calc 3.5)%s                           %s│%s\n",
           COLOR_TITLE, COLOR_RESET, COLOR_TAG, COLOR_RESET, COLOR_TITLE, COLOR_RESET);
    printf("%s├────────────────────────────────────────────────────────────────────────────┤%s\n", COLOR_TITLE, COLOR_RESET);
    printf("  %s• Operadores Matemáticos:%s +  -  *  /  %%  ^ (potência)\n", COLOR_VAL, COLOR_RESET);
    printf("  %s• Operadores Bitwise    :%s & (AND)  | (OR)  << (Shift Left)  >> (Shift Right)\n", COLOR_VAL, COLOR_RESET);
    printf("  %s• Bases Numéricas       :%s 0xFF (Hex), 0b1010 (Binário), 0o777 (Octal)\n", COLOR_VAL, COLOR_RESET);
    printf("  %s• Funções Matemáticas   :%s sqrt, cbrt, sin, cos, tan, asin, acos, atan\n", COLOR_VAL, COLOR_RESET);
    printf("                             log, log10, log2, exp, abs, fact, floor, ceil, round\n");
    printf("  %s• Constantes Embutidas  :%s pi, e, tau, phi (áurea), c (luz), ans (anterior)\n", COLOR_VAL, COLOR_RESET);
    printf("  %s• Variáveis Personaliz. :%s x = 10 + 5, raio = 4, area = pi * raio^2\n", COLOR_VAL, COLOR_RESET);
    printf("  %s• Mini-Manuais Específ. :%s Digite apenas o nome da função (ex: exp, sqrt, fact)\n", COLOR_VAL, COLOR_RESET);
    printf("  %s• Navegação de Histórico:%s Use as setas CIMA (↑) e BAIXO (↓)\n", COLOR_VAL, COLOR_RESET);
    printf("  %s• Comandos do Console   :%s vars, clear, cls, help, ?, exit, quit\n", COLOR_VAL, COLOR_RESET);
    printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n", COLOR_TITLE, COLOR_RESET);
}

/* --- EXIBE O MINI-MANUAL FORMATADO DE UMA FUNÇÃO --- */
static bool print_func_manual(const char *fname) {
    assert(fname != NULL);
    for (int i = 0; g_func_manuals[i].name != NULL; i++) {
        if (strcasecmp(g_func_manuals[i].name, fname) == 0) {
            printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", COLOR_TITLE, COLOR_RESET);
            printf("%s│%s  📖 %sMANUAL DA FUNÇÃO:%s %s%-47.47s%s %s│%s\n",
                   COLOR_TITLE, COLOR_RESET, COLOR_TAG, COLOR_RESET, COLOR_VAL, g_func_manuals[i].syntax, COLOR_RESET, COLOR_TITLE, COLOR_RESET);
            printf("%s├────────────────────────────────────────────────────────────────────────────┤%s\n", COLOR_TITLE, COLOR_RESET);
            printf("  %s• Descrição :%s %s\n", COLOR_VAL, COLOR_RESET, g_func_manuals[i].desc);
            printf("  %s• Domínio   :%s %s\n", COLOR_VAL, COLOR_RESET, g_func_manuals[i].domain);
            printf("  %s• Exemplos  :%s %s%s%s\n", COLOR_VAL, COLOR_RESET, COLOR_OK, g_func_manuals[i].example, COLOR_RESET);
            printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n", COLOR_TITLE, COLOR_RESET);
            return true;
        }
    }
    return false;
}

/* --- GERENCIAMENTO DE HISTÓRICO --- */
static void calc_add_history(const char *line) {
    assert(line != NULL);
    if (strlen(line) == 0) return;
    if (g_calc.history_count > 0 && strcmp(g_calc.history[g_calc.history_count - 1], line) == 0) return;

    if (g_calc.history_count < CALC_MAX_HISTORY) {
        strncpy(g_calc.history[g_calc.history_count], line, CALC_MAX_EXPR - 1);
        g_calc.history[g_calc.history_count][CALC_MAX_EXPR - 1] = '\0';
        g_calc.history_count++;
    } else {
        for (int i = 0; i < CALC_MAX_HISTORY - 1; i++) {
            strncpy(g_calc.history[i], g_calc.history[i + 1], CALC_MAX_EXPR - 1);
        }
        strncpy(g_calc.history[CALC_MAX_HISTORY - 1], line, CALC_MAX_EXPR - 1);
        g_calc.history[CALC_MAX_HISTORY - 1][CALC_MAX_EXPR - 1] = '\0';
    }
}

/* --- LEITOR DE LINHA INTERATIVO COM SETAS (CUSTOM READLINE) --- */
static bool calc_custom_readline(const char *prompt, char *out_buf, size_t max_len) {
    assert(prompt != NULL);
    assert(out_buf != NULL);

    struct termios orig_term, raw_term;
    if (tcgetattr(STDIN_FILENO, &orig_term) != 0 || !isatty(STDIN_FILENO)) {
        printf("%s", prompt);
        fflush(stdout);
        if (!fgets(out_buf, max_len, stdin)) return false;
        out_buf[strcspn(out_buf, "\r\n")] = '\0';
        return true;
    }

    raw_term = orig_term;
    raw_term.c_lflag &= ~(ICANON | ECHO);
    raw_term.c_cc[VMIN] = 1;
    raw_term.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw_term);

    printf("%s", prompt);
    fflush(stdout);

    size_t len = 0;
    out_buf[0] = '\0';
    int hist_pos = g_calc.history_count;
    char temp_edit[CALC_MAX_EXPR] = "";

    while (1) {
        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) {
            tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
            return false;
        }

        if (c == '\r' || c == '\n') {
            printf("\n");
            break;
        } else if (c == 127 || c == '\b') {
            if (len > 0) {
                len--;
                out_buf[len] = '\0';
                printf("\b \b");
                fflush(stdout);
            }
        } else if (c == 3) { // Ctrl+C
            tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
            printf("^C\n");
            out_buf[0] = '\0';
            return true;
        } else if (c == 4) { // Ctrl+D
            if (len == 0) {
                tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
                return false;
            }
        } else if (c == 27) { // Sequência de Escape / Setas
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;

            if (seq[0] == '[') {
                if (seq[1] == 'A') { // Seta CIMA (Histórico Anterior)
                    if (g_calc.history_count > 0) {
                        if (hist_pos == g_calc.history_count) {
                            strncpy(temp_edit, out_buf, sizeof(temp_edit) - 1);
                        }
                        if (hist_pos > 0) hist_pos--;
                        strncpy(out_buf, g_calc.history[hist_pos], max_len - 1);
                        out_buf[max_len - 1] = '\0';
                        len = strlen(out_buf);
                        printf("\r\033[K%s%s", prompt, out_buf);
                        fflush(stdout);
                    }
                } else if (seq[1] == 'B') { // Seta BAIXO (Histórico Posterior)
                    if (hist_pos < g_calc.history_count) {
                        hist_pos++;
                        if (hist_pos == g_calc.history_count) {
                            strncpy(out_buf, temp_edit, max_len - 1);
                        } else {
                            strncpy(out_buf, g_calc.history[hist_pos], max_len - 1);
                        }
                        out_buf[max_len - 1] = '\0';
                        len = strlen(out_buf);
                        printf("\r\033[K%s%s", prompt, out_buf);
                        fflush(stdout);
                    }
                }
            }
        } else if (isprint((unsigned char)c)) {
            if (len < max_len - 1) {
                out_buf[len++] = c;
                out_buf[len] = '\0';
                putchar(c);
                fflush(stdout);
            }
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
    return true;
}

/* --- INICIALIZAÇÃO ESTÁTICA DAS CONSTANTES DA NASA / JPL --- */
static void calc_init_engine(void) {
    memset(&g_calc, 0, sizeof(CalcEngine));

    strncpy(g_calc.vars[0].name, "pi", 31);   g_calc.vars[0].value = 3.14159265358979323846; g_calc.vars[0].is_constant = true;
    strncpy(g_calc.vars[1].name, "e", 31);    g_calc.vars[1].value = 2.71828182845904523536; g_calc.vars[1].is_constant = true;
    strncpy(g_calc.vars[2].name, "tau", 31);  g_calc.vars[2].value = 6.28318530717958647692; g_calc.vars[2].is_constant = true;
    strncpy(g_calc.vars[3].name, "phi", 31);  g_calc.vars[3].value = 1.61803398874989484820; g_calc.vars[3].is_constant = true;
    strncpy(g_calc.vars[4].name, "c", 31);    g_calc.vars[4].value = 299792458.0;             g_calc.vars[4].is_constant = true;
    g_calc.var_count = 5;
}

static bool calc_set_var(const char *name, double val) {
    assert(name != NULL);
    if (strlen(name) == 0 || strlen(name) >= 32) return false;

    for (int i = 0; i < g_calc.var_count; i++) {
        if (strcmp(g_calc.vars[i].name, name) == 0) {
            if (g_calc.vars[i].is_constant) return false;
            g_calc.vars[i].value = val;
            return true;
        }
    }

    if (g_calc.var_count < CALC_MAX_VARS) {
        strncpy(g_calc.vars[g_calc.var_count].name, name, 31);
        g_calc.vars[g_calc.var_count].name[31] = '\0';
        g_calc.vars[g_calc.var_count].value = val;
        g_calc.vars[g_calc.var_count].is_constant = false;
        g_calc.var_count++;
        return true;
    }
    return false;
}

static bool calc_get_var(const char *name, double *out_val) {
    assert(name != NULL);
    assert(out_val != NULL);

    if (strcmp(name, "ans") == 0) {
        *out_val = g_calc.last_ans;
        return true;
    }

    for (int i = 0; i < g_calc.var_count; i++) {
        if (strcmp(g_calc.vars[i].name, name) == 0) {
            *out_val = g_calc.vars[i].value;
            return true;
        }
    }
    return false;
}

static int op_precedence(TokenType type) {
    switch (type) {
        case TOK_BOR:   return 1;
        case TOK_BXOR:  return 2;
        case TOK_BAND:  return 3;
        case TOK_SHL:
        case TOK_SHR:   return 4;
        case TOK_PLUS:
        case TOK_MINUS: return 5;
        case TOK_MUL:
        case TOK_DIV:
        case TOK_MOD:   return 6;
        case TOK_POW:   return 7;
        case TOK_FUNC:  return 8;
        default:        return 0;
    }
}

static bool is_right_associative(TokenType type) {
    return (type == TOK_POW);
}

static double safe_factorial(double n) {
    if (n < 0.0 || n > 170.0 || floor(n) != n) return 0.0;
    double acc = 1.0;
    int limit = (int)n;
    for (int i = 2; i <= limit && i <= 170; i++) {
        acc *= (double)i;
    }
    return acc;
}

static bool apply_math_op(TokenType op, double a, double b, double *result) {
    assert(result != NULL);

    switch (op) {
        case TOK_PLUS:  *result = a + b; return true;
        case TOK_MINUS: *result = a - b; return true;
        case TOK_MUL:   *result = a * b; return true;
        case TOK_DIV:
            if (fabs(b) < 1e-15) {
                printf("  %s[ERRO NASA]: Divisão por zero detectada!%s\n", COLOR_ERR, COLOR_RESET);
                return false;
            }
            *result = a / b;
            return true;
        case TOK_MOD:
            if (fabs(b) < 1e-15) return false;
            *result = fmod(a, b);
            return true;
        case TOK_POW:   *result = pow(a, b); return true;
        case TOK_BAND:  *result = (double)((int64_t)a & (int64_t)b); return true;
        case TOK_BOR:   *result = (double)((int64_t)a | (int64_t)b); return true;
        case TOK_BXOR:  *result = (double)((int64_t)a ^ (int64_t)b); return true;
        case TOK_SHL:   *result = (double)((int64_t)a << (int64_t)b); return true;
        case TOK_SHR:   *result = (double)((int64_t)a >> (int64_t)b); return true;
        default:        return false;
    }
}

static bool apply_func_op(const char *fname, double arg, double *result) {
    assert(fname != NULL);
    assert(result != NULL);

    if (strcmp(fname, "sqrt") == 0) {
        if (arg < 0.0) { printf("  %s[ERRO]: Raiz quadrada de número negativo!%s\n", COLOR_ERR, COLOR_RESET); return false; }
        *result = sqrt(arg);
    } else if (strcmp(fname, "cbrt") == 0) {
        *result = cbrt(arg);
    } else if (strcmp(fname, "sin") == 0) {
        *result = sin(arg);
    } else if (strcmp(fname, "cos") == 0) {
        *result = cos(arg);
    } else if (strcmp(fname, "tan") == 0) {
        *result = tan(arg);
    } else if (strcmp(fname, "asin") == 0) {
        if (arg < -1.0 || arg > 1.0) return false;
        *result = asin(arg);
    } else if (strcmp(fname, "acos") == 0) {
        if (arg < -1.0 || arg > 1.0) return false;
        *result = acos(arg);
    } else if (strcmp(fname, "atan") == 0) {
        *result = atan(arg);
    } else if (strcmp(fname, "log") == 0 || strcmp(fname, "ln") == 0) {
        if (arg <= 0.0) return false;
        *result = log(arg);
    } else if (strcmp(fname, "log10") == 0) {
        if (arg <= 0.0) return false;
        *result = log10(arg);
    } else if (strcmp(fname, "log2") == 0) {
        if (arg <= 0.0) return false;
        *result = log2(arg);
    } else if (strcmp(fname, "exp") == 0) {
        *result = exp(arg);
    } else if (strcmp(fname, "abs") == 0) {
        *result = fabs(arg);
    } else if (strcmp(fname, "floor") == 0) {
        *result = floor(arg);
    } else if (strcmp(fname, "ceil") == 0) {
        *result = ceil(arg);
    } else if (strcmp(fname, "round") == 0) {
        *result = round(arg);
    } else if (strcmp(fname, "fact") == 0) {
        *result = safe_factorial(arg);
    } else {
        printf("  %s[ERRO]: Função desconhecida '%s'%s\n", COLOR_ERR, fname, COLOR_RESET);
        return false;
    }
    return true;
}

static bool evaluate_expression_iterative(const char *input_str, double *out_result) {
    assert(input_str != NULL);
    assert(out_result != NULL);

    double val_stack[CALC_MAX_STACK];
    int val_top = 0;

    TokenType op_stack[CALC_MAX_STACK];
    char func_stack[CALC_MAX_STACK][32];
    int op_top = 0;

    const char *p = input_str;
    bool expect_unary = true;
    int step_guard = 0;

    while (*p && step_guard++ < CALC_MAX_STEPS) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)*(p + 1)))) {
            double val = 0.0;
            if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
                char *endp;
                val = (double)strtoull(p + 2, &endp, 16);
                p = endp;
            } else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) {
                char *endp;
                val = (double)strtoull(p + 2, &endp, 2);
                p = endp;
            } else if (p[0] == '0' && (p[1] == 'o' || p[1] == 'O')) {
                char *endp;
                val = (double)strtoull(p + 2, &endp, 8);
                p = endp;
            } else {
                char *endp;
                val = strtod(p, &endp);
                p = endp;
            }

            assert(val_top < CALC_MAX_STACK);
            val_stack[val_top++] = val;
            expect_unary = false;
            continue;
        }

        if (isalpha((unsigned char)*p) || *p == '_') {
            char name[32] = "";
            size_t nlen = 0;
            while ((isalnum((unsigned char)*p) || *p == '_') && nlen < 31) {
                name[nlen++] = *p++;
            }
            name[nlen] = '\0';

            while (*p == ' ') p++;

            if (*p == '(') {
                p++;
                assert(op_top < CALC_MAX_STACK);
                op_stack[op_top] = TOK_FUNC;
                strncpy(func_stack[op_top], name, 31);
                op_top++;
                expect_unary = true;
                continue;
            }

            double v = 0.0;
            if (!calc_get_var(name, &v)) {
                printf("  %s[ERRO]: Variável não encontrada '%s'%s\n", COLOR_ERR, name, COLOR_RESET);
                return false;
            }
            assert(val_top < CALC_MAX_STACK);
            val_stack[val_top++] = v;
            expect_unary = false;
            continue;
        }

        if (*p == '(' || *p == '[' || *p == '{') {
            p++;
            assert(op_top < CALC_MAX_STACK);
            op_stack[op_top++] = TOK_LPAREN;
            expect_unary = true;
            continue;
        }

        if (*p == ')' || *p == ']' || *p == '}') {
            p++;
            while (op_top > 0 && op_stack[op_top - 1] != TOK_LPAREN && op_stack[op_top - 1] != TOK_FUNC) {
                TokenType top_op = op_stack[--op_top];
                if (val_top < 2) return false;
                double b = val_stack[--val_top];
                double a = val_stack[--val_top];
                double res = 0.0;
                if (!apply_math_op(top_op, a, b, &res)) return false;
                val_stack[val_top++] = res;
            }

            if (op_top > 0) {
                if (op_stack[op_top - 1] == TOK_LPAREN) {
                    op_top--;
                } else if (op_stack[op_top - 1] == TOK_FUNC) {
                    char fname[32];
                    strncpy(fname, func_stack[op_top - 1], 31);
                    op_top--;
                    if (val_top < 1) return false;
                    double arg = val_stack[--val_top];
                    double res = 0.0;
                    if (!apply_func_op(fname, arg, &res)) return false;
                    val_stack[val_top++] = res;
                }
            }
            expect_unary = false;
            continue;
        }

        TokenType current_tok = TOK_END;

        if (*p == '+' && expect_unary) { p++; continue; }
        if (*p == '-' && expect_unary) {
            p++;
            val_stack[val_top++] = 0.0;
            current_tok = TOK_MINUS;
        } else if (p[0] == '<' && p[1] == '<') { current_tok = TOK_SHL; p += 2; }
        else if (p[0] == '>' && p[1] == '>')   { current_tok = TOK_SHR; p += 2; }
        else if (*p == '+') { current_tok = TOK_PLUS;  p++; }
        else if (*p == '-') { current_tok = TOK_MINUS; p++; }
        else if (*p == '*' || *p == 'x') { current_tok = TOK_MUL;   p++; }
        else if (*p == '/') { current_tok = TOK_DIV;   p++; }
        else if (*p == '%') { current_tok = TOK_MOD;   p++; }
        else if (*p == '^') { current_tok = TOK_POW;   p++; }
        else if (*p == '&') { current_tok = TOK_BAND;  p++; }
        else if (*p == '|') { current_tok = TOK_BOR;   p++; }
        else {
            printf("  %s[ERRO]: Símbolo inválido '%c'%s\n", COLOR_ERR, *p, COLOR_RESET);
            return false;
        }

        while (op_top > 0 && op_stack[op_top - 1] != TOK_LPAREN && op_stack[op_top - 1] != TOK_FUNC) {
            TokenType top_op = op_stack[op_top - 1];
            int prec_top = op_precedence(top_op);
            int prec_cur = op_precedence(current_tok);

            if ((!is_right_associative(current_tok) && prec_top >= prec_cur) ||
                (is_right_associative(current_tok) && prec_top > prec_cur)) {
                op_top--;
                if (val_top < 2) return false;
                double b = val_stack[--val_top];
                double a = val_stack[--val_top];
                double res = 0.0;
                if (!apply_math_op(top_op, a, b, &res)) return false;
                val_stack[val_top++] = res;
            } else {
                break;
            }
        }

        assert(op_top < CALC_MAX_STACK);
        op_stack[op_top++] = current_tok;
        expect_unary = true;
    }

    while (op_top > 0) {
        TokenType top_op = op_stack[--op_top];
        if (top_op == TOK_LPAREN || top_op == TOK_FUNC) return false;
        if (val_top < 2) return false;
        double b = val_stack[--val_top];
        double a = val_stack[--val_top];
        double res = 0.0;
        if (!apply_math_op(top_op, a, b, &res)) return false;
        val_stack[val_top++] = res;
    }

    if (val_top == 1) {
        *out_result = val_stack[0];
        g_calc.last_ans = val_stack[0];
        return true;
    }

    return false;
}

static void print_result_card(double res) {
    printf("\n%s╔══════════════════════════════════════════════════════╗%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s║  RESULTADO: %s%-41g%s║%s\n", COLOR_TITLE, COLOR_OK, res, COLOR_TITLE, COLOR_RESET);
    printf("%s╚══════════════════════════════════════════════════════╝%s\n", COLOR_TITLE, COLOR_RESET);

    if (res >= 0.0 && res <= 18446744073709551615.0 && fabs(res - floor(res)) < 1e-9) {
        uint64_t uval = (uint64_t)res;
        char bin_buf[70] = "";
        int bidx = 0;
        uint64_t temp = uval;

        if (temp == 0) {
            strcpy(bin_buf, "0");
        } else {
            char rev[70];
            int ridx = 0;
            for (int i = 0; i < 64 && temp > 0; i++) {
                rev[ridx++] = (temp & 1) ? '1' : '0';
                temp >>= 1;
            }
            for (int i = ridx - 1; i >= 0; i--) bin_buf[bidx++] = rev[i];
            bin_buf[bidx] = '\0';
        }

        printf("  %s• Hexadecimal :%s %s0x%llX%s\n", COLOR_TAG, COLOR_RESET, COLOR_VAL, (unsigned long long)uval, COLOR_RESET);
        printf("  %s• Binário     :%s %s0b%s%s\n", COLOR_TAG, COLOR_RESET, COLOR_VAL, bin_buf, COLOR_RESET);
        printf("  %s• Octal       :%s %s0o%llo%s\n\n", COLOR_TAG, COLOR_RESET, COLOR_VAL, (unsigned long long)uval, COLOR_RESET);
    } else {
        printf("  %s• Notação Científica:%s %e\n\n", COLOR_TAG, COLOR_RESET, res);
    }
}

static void list_all_constants_and_vars(void) {
    printf("\n%s╭──────────────────────────────────────────────────────╮%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s│  VARIÁVEIS & CONSTANTES DA NASA DISPONÍVEIS          │%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s├──────────────────────────────────────────────────────┤%s\n", COLOR_TITLE, COLOR_RESET);
    printf("  • %sans%s  = %g (Último Resultado Calculado)\n", COLOR_VAL, COLOR_RESET, g_calc.last_ans);
    for (int i = 0; i < g_calc.var_count; i++) {
        printf("  • %s%-6s%s = %-16g %s\n",
               COLOR_VAL, g_calc.vars[i].name, COLOR_RESET, g_calc.vars[i].value,
               g_calc.vars[i].is_constant ? "\033[0;90m[Constante NASA]\033[0m" : "");
    }
    printf("%s╰──────────────────────────────────────────────────────╯%s\n\n", COLOR_TITLE, COLOR_RESET);
}

static void process_calc_line(const char *raw_line) {
    assert(raw_line != NULL);
    char line[CALC_MAX_EXPR];
    strncpy(line, raw_line, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';

    char *trim = line;
    while (*trim == ' ') trim++;
    size_t tl = strlen(trim);
    while (tl > 0 && isspace((unsigned char)trim[tl - 1])) trim[--tl] = '\0';

    if (strcmp(trim, "help") == 0 || strcmp(trim, "?") == 0) {
        print_repl_help();
        return;
    }

    if (strncmp(trim, "help ", 5) == 0 || strncmp(trim, "? ", 2) == 0) {
        char *arg = strchr(trim, ' ');
        while (arg && *arg == ' ') arg++;
        if (arg && print_func_manual(arg)) return;
    }

    if (print_func_manual(trim)) {
        return;
    }

    char *eq = strchr(line, '=');
    if (eq && line[0] != '=' && *(eq + 1) != '=') {
        *eq = '\0';
        char var_name[32] = "";
        char *vstart = line;
        while (*vstart == ' ') vstart++;
        size_t vl = strlen(vstart);
        while (vl > 0 && isspace((unsigned char)vstart[vl - 1])) vstart[--vl] = '\0';
        strncpy(var_name, vstart, 31);

        char *vexpr = eq + 1;
        while (*vexpr == ' ') vexpr++;

        double res = 0.0;
        if (evaluate_expression_iterative(vexpr, &res)) {
            if (calc_set_var(var_name, res)) {
                printf("  %s[✔ Salvo]: %s = %g%s\n", COLOR_OK, var_name, res, COLOR_RESET);
                print_result_card(res);
            } else {
                printf("  %s[ERRO]: Não é permitido sobrescrever constantes de missão!%s\n", COLOR_ERR, COLOR_RESET);
            }
        }
        return;
    }

    double res = 0.0;
    if (evaluate_expression_iterative(line, &res)) {
        print_result_card(res);
    }
}

int main(int argc, char *argv[]) {
    utilipc_init();
    calc_init_engine();

    if (argc < 2) {
        printf("\n%s╔══════════════════════════════════════════════════════════════════════════════╗%s\n", COLOR_TITLE, COLOR_RESET);
        printf("%s║  🧮 CALC 3.5 — NASA Math Engine, Histórico por Setas & Manual Interativo     ║%s\n", COLOR_TITLE, COLOR_RESET);
        printf("%s╚══════════════════════════════════════════════════════════════════════════════╝%s\n", COLOR_TITLE, COLOR_RESET);
        printf("  • Digite 'help' ou '?' para ver todos os recursos disponíveis\n");
        printf("  • Navegue pelas contas anteriores usando as setas CIMA (↑) e BAIXO (↓)\n");
        printf("  • Digite o nome de qualquer função (ex: exp, sqrt, fact) para ver o manual\n\n");

        char line[CALC_MAX_EXPR];
        while (1) {
            if (!calc_custom_readline("calc> ", line, sizeof(line))) break;

            size_t l = strlen(line);
            while (l > 0 && (line[l - 1] == '\r' || line[l - 1] == '\n' || line[l - 1] == ' ')) line[--l] = '\0';
            if (l == 0) continue;

            if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) break;
            if (strcmp(line, "clear") == 0 || strcmp(line, "cls") == 0) { printf("\033[H\033[J"); continue; }
            if (strcmp(line, "vars") == 0) { list_all_constants_and_vars(); continue; }

            calc_add_history(line);
            process_calc_line(line);

            char log_msg[UTILIPC_MAX_MSG];
            snprintf(log_msg, sizeof(log_msg), "calc: evaluated (Result: %g)", g_calc.last_ans);
            utilipc_write_status(-1, -1, -1, log_msg);
        }

        utilipc_close();
        return 0;
    }

    char full_expr[CALC_MAX_EXPR] = "";
    for (int i = 1; i < argc; i++) {
        strncat(full_expr, argv[i], sizeof(full_expr) - strlen(full_expr) - 1);
        if (i < argc - 1) strncat(full_expr, " ", sizeof(full_expr) - strlen(full_expr) - 1);
    }

    process_calc_line(full_expr);

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "calc: evaluated '%s' (%g)", full_expr, g_calc.last_ans);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
