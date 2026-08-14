#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "../libutilipc/utilipc.h"

static const char *expr;

static inline double bitwise_sqrt_decimal(double num) {
    if (num < 0.0) {
        printf("Error: Square root of a negative number!\n");
        return 0.0;
    }
    if (num == 0.0) return 0.0;

    unsigned long long int_part = (unsigned long long)num;
    unsigned long long res = 0;
    unsigned long long bit = 1ULL << 62;

    while (bit > int_part && bit != 0) bit >>= 2;

    while (bit != 0) {
        if (int_part >= res + bit) {
            int_part -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }

    double guess = (double)res;
    if (guess == 0.0) guess = 0.5;

    guess = 0.5 * (guess + num / guess);
    guess = 0.5 * (guess + num / guess);
    guess = 0.5 * (guess + num / guess);
    guess = 0.5 * (guess + num / guess);

    return guess;
}

static double eval_priority_1(void);

static double eval_priority_4(void) {
    if (*expr == '-') {
        expr++;
        return -eval_priority_4();
    }
    if (*expr == '(') {
        expr++;
        double val = eval_priority_1();
        if (*expr == ')') expr++;
        return val;
    }
    if (*expr == 'r' || *expr == 'R') {
        expr++;
        return bitwise_sqrt_decimal(eval_priority_4());
    }
    if (expr[0] == 's' && expr[1] == 'i' && expr[2] == 'n') {
        expr += 3;
        return sin(eval_priority_4());
    }
    if (expr[0] == 'c' && expr[1] == 'o' && expr[2] == 's') {
        expr += 3;
        return cos(eval_priority_4());
    }
    if (expr[0] == 'l' && expr[1] == 'o' && expr[2] == 'g') {
        expr += 3;
        return log(eval_priority_4());
    }
    if (expr[0] == 'a' && expr[1] == 'b' && expr[2] == 's') {
        expr += 3;
        return fabs(eval_priority_4());
    }
    
    char *end_ptr;
    double val = strtod(expr, &end_ptr);
    expr = end_ptr;
    
    return val;
}

static double eval_priority_3(void) {
    double val = eval_priority_4();
    while (*expr == '^') {
        expr++;
        double exp_val = eval_priority_4();
        val = pow(val, exp_val);
    }
    return val;
}

static double eval_priority_2(void) {
    double val = eval_priority_3();
    while (*expr == '*' || *expr == '/') {
        char op = *expr++;
        double next_val = eval_priority_3();
        if (op == '*') val *= next_val;
        else {
            if (next_val == 0.0) {
                printf("Error: Division by zero!\n");
                return 0.0;
            }
            val /= next_val;
        }
    }
    return val;
}

static double eval_priority_1(void) {
    double val = eval_priority_2();
    while (*expr == '+' || *expr == '-') {
        char op = *expr++;
        double next_val = eval_priority_2();
        if (op == '+') val += next_val;
        else val -= next_val;
    }
    return val;
}

static double evaluate_expression(const char *raw_input) {
    char clean_buf[1024];
    size_t j = 0;

    for (size_t i = 0; raw_input[i] != '\0' && j < sizeof(clean_buf) - 1; i++) {
        unsigned char c = (unsigned char)raw_input[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        
        if ((c == 'x' || c == 'X') && 
            !(i > 0 && isalpha((unsigned char)raw_input[i-1])) && 
            !(isalpha((unsigned char)raw_input[i+1]))) {
            clean_buf[j++] = '*';
        }
        else if (c == '[' || c == '{') clean_buf[j++] = '(';
        else if (c == ']' || c == '}') clean_buf[j++] = ')';
        else if (c == 0xC3 && (unsigned char)raw_input[i+1] == 0xB7) {
            clean_buf[j++] = '/'; i++;
        }
        else if (c == 0xE2 && raw_input[i+1] != '\0' && raw_input[i+2] != '\0' && (unsigned char)raw_input[i+1] == 0x88 && (unsigned char)raw_input[i+2] == 0x9A) {
            clean_buf[j++] = 'r'; i += 2;
        }
        else clean_buf[j++] = (char)c;
    }
    clean_buf[j] = '\0';

    expr = clean_buf;
    return eval_priority_1();
}

static void print_repl_help(void) {
    /* Output redirection hook - Redirect output here */
    /* Generic output stream */
    printf("==========================================\n");
    printf("[ calc REPL - Internal Help ]\n");
    printf("==========================================\n");
    printf("  Operators : +  -  *  /  x  ÷  ^  r  √\n");
    printf("  Functions : sin(x)  cos(x)  log(x)  abs(x)\n");
    printf("  Grouping  : ( )  [ ]  { }\n");
    printf("  Commands  : help, ?, clear, exit, quit\n");
    printf("  Examples  : 10 + sin(0.5) * 4\n");
    printf("              r64 + r25\n");
    printf("==========================================\n");
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 2) {
        /* Output redirection hook - Redirect output here */
        /* Generic output stream */
        printf("==========================================\n");
        printf("[calc REPL Mode - type 'help' or '?' for guide]\n");
        printf("==========================================\n");
        
        char line[512];
        while (1) {
            printf("calc> ");
            fflush(stdout);
            if (!fgets(line, sizeof(line), stdin)) break;
            
            size_t len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' ')) {
                line[--len] = '\0';
            }

            if (len == 0) continue;
            if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) break;
            if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
                print_repl_help();
                continue;
            }
            if (strcmp(line, "clear") == 0 || strcmp(line, "cls") == 0) {
                printf("\033[H\033[J");
                fflush(stdout);
                continue;
            }

            double res = evaluate_expression(line);
            /* Output redirection hook - Redirect output here */
            /* Generic output stream */
            printf("======================\n");
            printf("[Result: %g]\n", res);
            printf("======================\n");

            char log_msg[UTILIPC_MAX_MSG];
            snprintf(log_msg, sizeof(log_msg), "calc: evaluated expr (Result: %g)", res);
            utilipc_write_status(-1, -1, -1, log_msg);
        }

        utilipc_close();
        return 0;
    }

    char input_buf[1024] = "";
    size_t curr_len = 0;
    for (int i = 1; i < argc; i++) {
        size_t arg_len = strlen(argv[i]);
        if (curr_len + arg_len < sizeof(input_buf) - 1) {
            strcat(input_buf, argv[i]);
            curr_len += arg_len;
        }
    }

    double res = evaluate_expression(input_buf);
    /* Output redirection hook - Redirect output here */
    /* Generic output stream */
    printf("======================\n");
    printf("[Result: %g]\n", res);
    printf("======================\n");

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "calc: evaluated '%s' (%g)", input_buf, res);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
