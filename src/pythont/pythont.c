#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <sys/wait.h>
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

/*
 * Simple lexer/tokenizer foundation.
 *
 * The current transpiler still uses its legacy line parser. This lexer is
 * intentionally isolated so tokenization can be adopted incrementally.
 */
#define LEX_TOKEN_TEXT 128

typedef enum {
    TOK_EOF = 0,
    TOK_IDENTIFIER,
    TOK_INTEGER,
    TOK_FLOAT,
    TOK_STRING,
    TOK_NEWLINE,
    TOK_INDENT,
    TOK_DEDENT,
    TOK_OPERATOR,
    TOK_DELIMITER,
    TOK_KEYWORD,
    TOK_ERROR
} token_type_t;

typedef struct {
    token_type_t type;
    char text[LEX_TOKEN_TEXT];
    size_t line;
    size_t column;
} token_t;

typedef struct {
    const char *source;
    size_t pos;
    size_t line;
    size_t column;
    int at_line_start;
    int pending_dedents;
    int indent_stack[MAX_INDENTS];
    int indent_top;
} lexer_t;

static int lexer_is_identifier_start(unsigned char c) {
    return isalpha(c) || c == '_';
}

static int lexer_is_identifier_char(unsigned char c) {
    return isalnum(c) || c == '_';
}

static int lexer_is_keyword(const char *text) {
    static const char *const keywords[] = {
        "and", "as", "assert", "break", "class", "continue",
        "def", "elif", "else", "except", "False", "finally",
        "for", "from", "if", "import", "in", "is", "lambda",
        "None", "not", "or", "pass", "raise", "return",
        "True", "try", "while", "with", "yield"
    };

    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i) {
        if (strcmp(text, keywords[i]) == 0) return 1;
    }
    return 0;
}

static void lexer_init(lexer_t *lexer, const char *source) {
    lexer->source = source ? source : "";
    lexer->pos = 0;
    lexer->line = 1;
    lexer->column = 1;
    lexer->at_line_start = 1;
    lexer->pending_dedents = 0;
    lexer->indent_stack[0] = 0;
    lexer->indent_top = 0;
}

static void lexer_advance(lexer_t *lexer) {
    char c = lexer->source[lexer->pos];

    if (!c)
        return;

    lexer->pos++;

    if (c == '\n') {
        lexer->line++;
        lexer->column = 1;
        lexer->at_line_start = 1;
    } else {
        lexer->column++;
    }
}

static char lexer_peek(const lexer_t *lexer, size_t offset) {
    if (!lexer->source)
        return '\0';

    return lexer->source[lexer->pos + offset];
}

static int lexer_read_quoted(lexer_t *lexer, token_t *token, char quote) {
    size_t out = 0;

    lexer_advance(lexer);

    while (lexer_peek(lexer, 0) && lexer_peek(lexer, 0) != quote) {
        char c = lexer_peek(lexer, 0);

        if (c == '\n') return 0;

        if (c == '\\' && lexer_peek(lexer, 1)) {
            if (out + 2 >= sizeof(token->text)) return 0;
            token->text[out++] = c;
            lexer_advance(lexer);
            token->text[out++] = lexer_peek(lexer, 0);
            lexer_advance(lexer);
            continue;
        }

        if (out + 1 >= sizeof(token->text)) return 0;
        token->text[out++] = c;
        lexer_advance(lexer);
    }

    if (lexer_peek(lexer, 0) != quote) return 0;
    lexer_advance(lexer);

    token->text[out] = '\0';
    return 1;
}

static token_t lexer_make_token(token_type_t type, const char *text,
                                size_t line, size_t column) {
    token_t token;
    token.type = type;
    token.line = line;
    token.column = column;

    if (!text) text = "";
    snprintf(token.text, sizeof(token.text), "%s", text);
    return token;
}

static token_t lexer_next(lexer_t *lexer) {
    if (lexer->pending_dedents > 0) {
        lexer->pending_dedents--;
        return lexer_make_token(TOK_DEDENT, "", lexer->line, lexer->column);
    }

    if (lexer->at_line_start) {
        int indent = 0;

        while (lexer_peek(lexer, 0) == ' ' || lexer_peek(lexer, 0) == '\t') {
            indent += (lexer_peek(lexer, 0) == '\t') ? 4 : 1;
            lexer_advance(lexer);
        }

        char first = lexer_peek(lexer, 0);

        if (first == '#') {
            while (lexer_peek(lexer, 0) && lexer_peek(lexer, 0) != '\n')
                lexer_advance(lexer);
            return lexer_next(lexer);
        }

        if (first == '\n') {
            size_t line = lexer->line;
            size_t column = lexer->column;
            lexer_advance(lexer);
            return lexer_make_token(TOK_NEWLINE, "\\n", line, column);
        }

        int current = lexer->indent_stack[lexer->indent_top];

        if (indent > current) {
            if (lexer->indent_top + 1 >= MAX_INDENTS)
                return lexer_make_token(TOK_ERROR, "<indent-too-deep>", lexer->line, lexer->column);

            lexer->indent_stack[++lexer->indent_top] = indent;
            lexer->at_line_start = 0;
            return lexer_make_token(TOK_INDENT, "", lexer->line, 1);
        }

        if (indent < current) {
            while (lexer->indent_top > 0 &&
                   indent < lexer->indent_stack[lexer->indent_top]) {
                lexer->indent_top--;
                lexer->pending_dedents++;
            }

            if (indent != lexer->indent_stack[lexer->indent_top]) {
                return lexer_make_token(TOK_ERROR, "<invalid-indent>", lexer->line, lexer->column);
            }

            if (lexer->pending_dedents > 0) {
                lexer->at_line_start = 0;
                lexer->pending_dedents--;
                return lexer_make_token(TOK_DEDENT, "", lexer->line, 1);
            }
        }

        lexer->at_line_start = 0;
    }

    while (lexer_peek(lexer, 0) == ' ' || lexer_peek(lexer, 0) == '\t' ||
           lexer_peek(lexer, 0) == '\r') {
        lexer_advance(lexer);
    }

    size_t line = lexer->line;
    size_t column = lexer->column;
    char c = lexer_peek(lexer, 0);

    if (!c) {
        if (lexer->indent_top > 0) {
            lexer->indent_top--;
            return lexer_make_token(TOK_DEDENT, "", line, column);
        }
        return lexer_make_token(TOK_EOF, "", line, column);
    }

    if (c == '#') {
        while (lexer_peek(lexer, 0) && lexer_peek(lexer, 0) != '\n')
            lexer_advance(lexer);
        return lexer_next(lexer);
    }

    if (c == '\n') {
        lexer_advance(lexer);
        lexer->at_line_start = 1;
        return lexer_make_token(TOK_NEWLINE, "\\n", line, column);
    }

    if (lexer_is_identifier_start((unsigned char)c)) {
        char text[LEX_TOKEN_TEXT];
        size_t n = 0;

        while (lexer_is_identifier_char((unsigned char)lexer_peek(lexer, 0))) {
            if (n + 1 >= sizeof(text)) break;
            text[n++] = lexer_peek(lexer, 0);
            lexer_advance(lexer);
        }
        text[n] = '\0';

        return lexer_make_token(
            lexer_is_keyword(text) ? TOK_KEYWORD : TOK_IDENTIFIER,
            text, line, column);
    }

    if (isdigit((unsigned char)c) ||
        (c == '.' && isdigit((unsigned char)lexer_peek(lexer, 1)))) {
        char text[LEX_TOKEN_TEXT];
        size_t n = 0;
        int is_float = 0;

        if (c == '.') is_float = 1;

        while (isdigit((unsigned char)lexer_peek(lexer, 0))) {
            if (n + 1 >= sizeof(text)) break;
            text[n++] = lexer_peek(lexer, 0);
            lexer_advance(lexer);
        }

        if (lexer_peek(lexer, 0) == '.' && !is_float) {
            is_float = 1;
            if (n + 1 < sizeof(text)) {
                text[n++] = '.';
                lexer_advance(lexer);
            }
            while (isdigit((unsigned char)lexer_peek(lexer, 0))) {
                if (n + 1 >= sizeof(text)) break;
                text[n++] = lexer_peek(lexer, 0);
                lexer_advance(lexer);
            }
        }

        if (lexer_peek(lexer, 0) == 'e' || lexer_peek(lexer, 0) == 'E') {
            is_float = 1;
            if (n + 1 < sizeof(text)) {
                text[n++] = lexer_peek(lexer, 0);
                lexer_advance(lexer);
            }
            if (lexer_peek(lexer, 0) == '+' || lexer_peek(lexer, 0) == '-') {
                if (n + 1 < sizeof(text)) {
                    text[n++] = lexer_peek(lexer, 0);
                    lexer_advance(lexer);
                }
            }
            while (isdigit((unsigned char)lexer_peek(lexer, 0))) {
                if (n + 1 >= sizeof(text)) break;
                text[n++] = lexer_peek(lexer, 0);
                lexer_advance(lexer);
            }
        }

        text[n] = '\0';
        return lexer_make_token(is_float ? TOK_FLOAT : TOK_INTEGER,
                                text, line, column);
    }

    if (c == '\'' || c == '\"') {
        token_t token;
        token.type = TOK_STRING;
        token.line = line;
        token.column = column;
        token.text[0] = '\0';

        if (!lexer_read_quoted(lexer, &token, c))
            return lexer_make_token(TOK_ERROR, "<invalid-string>", line, column);
        return token;
    }

    {
        char text[3] = { c, '\0', '\0' };
        char next = lexer_peek(lexer, 1);

        if ((c == '=' || c == '!' || c == '<' || c == '>') && next == '=') {
            text[1] = '=';
        } else if ((c == '*' && next == '*') || (c == '/' && next == '/') ||
                   (c == '-' && next == '>') || (c == '=' && next == '>')) {
            text[1] = next;
        }

        lexer_advance(lexer);
        if (text[1]) lexer_advance(lexer);

        token_type_t type =
            strchr("()[]{}:,.;", text[0]) ? TOK_DELIMITER : TOK_OPERATOR;
        return lexer_make_token(type, text, line, column);
    }
}

static const char *token_type_name(token_type_t type);

static void lexer_dump(const char *source) {
    lexer_t lexer;
    lexer_init(&lexer, source);

    for (;;) {
        token_t token = lexer_next(&lexer);

        printf("%zu:%zu  %-10s  %s\n",
               token.line, token.column,
               token_type_name(token.type), token.text);

        if (token.type == TOK_ERROR || token.type == TOK_EOF)
            break;
    }
}

static const char *token_type_name(token_type_t type) {
    switch (type) {
        case TOK_EOF: return "EOF";
        case TOK_IDENTIFIER: return "IDENTIFIER";
        case TOK_INTEGER: return "INTEGER";
        case TOK_FLOAT: return "FLOAT";
        case TOK_STRING: return "STRING";
        case TOK_NEWLINE: return "NEWLINE";
        case TOK_INDENT: return "INDENT";
        case TOK_DEDENT: return "DEDENT";
        case TOK_OPERATOR: return "OPERATOR";
        case TOK_DELIMITER: return "DELIMITER";
        case TOK_KEYWORD: return "KEYWORD";
        case TOK_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}


/*
 * Incremental AST foundation.
 *
 * The legacy transpiler remains the execution path for now. The AST parser
 * is deliberately independent so the frontend can migrate statement by
 * statement without breaking existing programs.
 */
#define AST_MAX_NODES 4096
#define AST_MAX_ARGS 16

typedef enum {
    AST_NUMBER,
    AST_STRING,
    AST_IDENTIFIER,
    AST_UNARY,
    AST_BINARY,
    AST_CALL,
    AST_LIST,
    AST_DICT,
    AST_INDEX,
    AST_MEMBER,
    AST_ASSIGN
} ast_kind_t;

typedef struct ast_node ast_node_t;

struct ast_node {
    ast_kind_t kind;
    char text[LEX_TOKEN_TEXT];
    ast_node_t *left;
    ast_node_t *right;
    ast_node_t *callee;
    ast_node_t *args[AST_MAX_ARGS];
    size_t arg_count;
};

typedef struct {
    lexer_t lexer;
    token_t current;
    int error;
} ast_parser_t;

static ast_node_t ast_nodes[AST_MAX_NODES];
static size_t ast_node_count = 0;

static ast_node_t *ast_new(ast_kind_t kind, const char *text) {
    if (ast_node_count >= AST_MAX_NODES) return NULL;
    ast_node_t *node = &ast_nodes[ast_node_count++];
    memset(node, 0, sizeof(*node));
    node->kind = kind;
    if (text) snprintf(node->text, sizeof(node->text), "%s", text);
    return node;
}

static void ast_next(ast_parser_t *parser) {
    parser->current = lexer_next(&parser->lexer);
    if (parser->current.type == TOK_ERROR) parser->error = 1;
}

static int ast_is_operator(const ast_parser_t *parser, const char *op) {
    return parser->current.type == TOK_OPERATOR &&
           strcmp(parser->current.text, op) == 0;
}

static int ast_is_delimiter(const ast_parser_t *parser, const char *delim) {
    return parser->current.type == TOK_DELIMITER &&
           strcmp(parser->current.text, delim) == 0;
}

static int ast_precedence(const token_t *token) {
    if (token->type != TOK_OPERATOR && token->type != TOK_KEYWORD) return -1;
    if (!strcmp(token->text, "or")) return 1;
    if (!strcmp(token->text, "and")) return 2;
    if (!strcmp(token->text, "==") || !strcmp(token->text, "!=") ||
        !strcmp(token->text, "<") || !strcmp(token->text, "<=") ||
        !strcmp(token->text, ">") || !strcmp(token->text, ">=") ||
        !strcmp(token->text, "in") || !strcmp(token->text, "is")) return 3;
    if (!strcmp(token->text, "+") || !strcmp(token->text, "-")) return 4;
    if (!strcmp(token->text, "*") || !strcmp(token->text, "/") ||
        !strcmp(token->text, "//") || !strcmp(token->text, "%")) return 5;
    if (!strcmp(token->text, "**")) return 6;
    return -1;
}

static ast_node_t *ast_parse_expression(ast_parser_t *parser, int min_prec);

static ast_node_t *ast_parse_primary(ast_parser_t *parser) {
    token_t token = parser->current;

    if (token.type == TOK_INTEGER || token.type == TOK_FLOAT) {
        ast_next(parser);
        return ast_new(AST_NUMBER, token.text);
    }

    if (token.type == TOK_STRING) {
        ast_next(parser);
        return ast_new(AST_STRING, token.text);
    }

    if (token.type == TOK_IDENTIFIER ||
        (token.type == TOK_KEYWORD &&
         (strcmp(token.text, "True") == 0 ||
          strcmp(token.text, "False") == 0 ||
          strcmp(token.text, "None") == 0))) {
        ast_next(parser);
        return ast_new(AST_IDENTIFIER, token.text);
    }

    if (ast_is_delimiter(parser, "[")) {
        ast_node_t *list = ast_new(AST_LIST, "list");
        if (!list) return NULL;
        ast_next(parser);

        if (!ast_is_delimiter(parser, "]")) {
            while (!parser->error) {
                if (list->arg_count >= AST_MAX_ARGS) {
                    parser->error = 1;
                    return NULL;
                }
                ast_node_t *item = ast_parse_expression(parser, 0);
                if (!item) return NULL;
                list->args[list->arg_count++] = item;

                if (ast_is_delimiter(parser, ",")) {
                    ast_next(parser);
                    if (ast_is_delimiter(parser, "]")) break;
                    continue;
                }
                break;
            }
        }

        if (!ast_is_delimiter(parser, "]")) {
            parser->error = 1;
            return NULL;
        }
        ast_next(parser);
        return list;
    }

    if (ast_is_delimiter(parser, "{")) {
        ast_node_t *dict = ast_new(AST_DICT, "dict");
        if (!dict) return NULL;
        ast_next(parser);

        if (!ast_is_delimiter(parser, "}")) {
            while (!parser->error) {
                if (dict->arg_count + 1 >= AST_MAX_ARGS) {
                    parser->error = 1;
                    return NULL;
                }

                ast_node_t *key = ast_parse_expression(parser, 0);
                if (!key) return NULL;

                if (!ast_is_delimiter(parser, ":")) {
                    parser->error = 1;
                    return NULL;
                }
                ast_next(parser);

                ast_node_t *value = ast_parse_expression(parser, 0);
                if (!value) return NULL;

                dict->args[dict->arg_count++] = key;
                dict->args[dict->arg_count++] = value;

                if (ast_is_delimiter(parser, ",")) {
                    ast_next(parser);
                    if (ast_is_delimiter(parser, "}")) break;
                    continue;
                }
                break;
            }
        }

        if (!ast_is_delimiter(parser, "}")) {
            parser->error = 1;
            return NULL;
        }
        ast_next(parser);
        return dict;
    }

    if (ast_is_delimiter(parser, "(")) {
        ast_next(parser);
        ast_node_t *node = ast_parse_expression(parser, 0);
        if (!node || !ast_is_delimiter(parser, ")")) {
            parser->error = 1;
            return NULL;
        }
        ast_next(parser);
        return node;
    }

    if ((parser->current.type == TOK_OPERATOR &&
         (!strcmp(parser->current.text, "+") ||
          !strcmp(parser->current.text, "-") ||
          !strcmp(parser->current.text, "~"))) ||
        (parser->current.type == TOK_KEYWORD &&
         strcmp(parser->current.text, "not") == 0)) {
        char op[LEX_TOKEN_TEXT];
        snprintf(op, sizeof(op), "%s", parser->current.text);
        ast_next(parser);
        ast_node_t *operand = ast_parse_expression(parser, 7);
        if (!operand) return NULL;
        ast_node_t *node = ast_new(AST_UNARY, op);
        if (!node) return NULL;
        node->left = operand;
        return node;
    }

    parser->error = 1;
    return NULL;
}

static ast_node_t *ast_parse_postfix(ast_parser_t *parser) {
    ast_node_t *node = ast_parse_primary(parser);
    if (!node) return NULL;

    while (!parser->error) {
        if (ast_is_delimiter(parser, "(")) {
            ast_node_t *call = ast_new(AST_CALL, "call");
            if (!call) return NULL;
            call->callee = node;
            ast_next(parser);

            if (!ast_is_delimiter(parser, ")")) {
                while (!parser->error) {
                    if (call->arg_count >= AST_MAX_ARGS) {
                        parser->error = 1;
                        return NULL;
                    }
                    ast_node_t *arg = ast_parse_expression(parser, 0);
                    if (!arg) return NULL;
                    call->args[call->arg_count++] = arg;

                    if (ast_is_delimiter(parser, ",")) {
                        ast_next(parser);
                        if (ast_is_delimiter(parser, ")")) break;
                        continue;
                    }
                    break;
                }
            }

            if (!ast_is_delimiter(parser, ")")) {
                parser->error = 1;
                return NULL;
            }
            ast_next(parser);
            node = call;
            continue;
        }

        if (ast_is_delimiter(parser, "[")) {
            ast_next(parser);
            ast_node_t *index = ast_parse_expression(parser, 0);
            if (!index || !ast_is_delimiter(parser, "]")) {
                parser->error = 1;
                return NULL;
            }
            ast_next(parser);

            ast_node_t *indexed = ast_new(AST_INDEX, "[]");
            if (!indexed) return NULL;
            indexed->left = node;
            indexed->right = index;
            node = indexed;
            continue;
        }

        if (ast_is_delimiter(parser, ".")) {
            ast_next(parser);
            if (parser->current.type != TOK_IDENTIFIER) {
                parser->error = 1;
                return NULL;
            }

            ast_node_t *member = ast_new(AST_MEMBER, parser->current.text);
            if (!member) return NULL;
            member->left = node;
            ast_next(parser);
            node = member;
            continue;
        }

        break;
    }

    return node;
}

static ast_node_t *ast_parse_expression(ast_parser_t *parser, int min_prec) {
    ast_node_t *left = ast_parse_postfix(parser);
    if (!left) return NULL;

    while (!parser->error) {
        int prec = ast_precedence(&parser->current);
        if (prec < min_prec) break;

        char op[LEX_TOKEN_TEXT];
        snprintf(op, sizeof(op), "%s", parser->current.text);
        ast_next(parser);

        int next_min = prec + (strcmp(op, "**") != 0);
        ast_node_t *right = ast_parse_expression(parser, next_min);
        if (!right) return NULL;

        ast_node_t *node = ast_new(AST_BINARY, op);
        if (!node) return NULL;
        node->left = left;
        node->right = right;
        left = node;
    }

    return left;
}

static ast_node_t *ast_parse_statement(ast_parser_t *parser) {
    ast_node_t *left = ast_parse_expression(parser, 0);
    if (!left) return NULL;

    if (ast_is_operator(parser, "=")) {
        if (left->kind != AST_IDENTIFIER) {
            parser->error = 1;
            return NULL;
        }
        ast_next(parser);
        ast_node_t *value = ast_parse_expression(parser, 0);
        if (!value) return NULL;

        ast_node_t *assign = ast_new(AST_ASSIGN, "=");
        if (!assign) return NULL;
        assign->left = left;
        assign->right = value;
        return assign;
    }

    return left;
}

static const char *ast_kind_name(ast_kind_t kind) {
    switch (kind) {
        case AST_NUMBER: return "Number";
        case AST_STRING: return "String";
        case AST_IDENTIFIER: return "Identifier";
        case AST_UNARY: return "Unary";
        case AST_BINARY: return "Binary";
        case AST_CALL: return "Call";
        case AST_LIST: return "List";
        case AST_DICT: return "Dict";
        case AST_INDEX: return "Index";
        case AST_MEMBER: return "Member";
        case AST_ASSIGN: return "Assign";
        default: return "Unknown";
    }
}

static void ast_dump_node(const ast_node_t *node, int depth) {
    if (!node) return;
    for (int i = 0; i < depth; ++i) printf("  ");

    printf("%s", ast_kind_name(node->kind));
    if (node->text[0]) printf(": %s", node->text);
    printf("\n");

    if (node->kind == AST_CALL && node->callee) {
        ast_dump_node(node->callee, depth + 1);
        for (size_t i = 0; i < node->arg_count; ++i)
            ast_dump_node(node->args[i], depth + 1);
        return;
    }

    if (node->kind == AST_DICT) {
        for (size_t i = 0; i < node->arg_count; ++i)
            ast_dump_node(node->args[i], depth + 1);
        return;
    }

    if (node->kind == AST_LIST) {
        for (size_t i = 0; i < node->arg_count; ++i)
            ast_dump_node(node->args[i], depth + 1);
        return;
    }

    if (node->left) ast_dump_node(node->left, depth + 1);
    if (node->right) ast_dump_node(node->right, depth + 1);
}

static int ast_dump_source(const char *source) {
    ast_parser_t parser;
    memset(&parser, 0, sizeof(parser));
    lexer_init(&parser.lexer, source);
    ast_node_count = 0;
    ast_next(&parser);

    while (!parser.error && parser.current.type != TOK_EOF) {
        if (parser.current.type == TOK_NEWLINE ||
            parser.current.type == TOK_INDENT ||
            parser.current.type == TOK_DEDENT) {
            ast_next(&parser);
            continue;
        }

        ast_node_t *node = ast_parse_statement(&parser);
        if (!node || parser.error) break;
        ast_dump_node(node, 0);

        if (parser.current.type == TOK_NEWLINE) ast_next(&parser);
        else if (parser.current.type != TOK_EOF) parser.error = 1;
    }

    if (parser.error) {
        fprintf(stderr, "pythont: AST parse error near %zu:%zu ('%s')\n",
                parser.current.line, parser.current.column, parser.current.text);
        return 1;
    }
    return 0;
}

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
        size_t name_len = strlen(name);
        if (name_len >= sizeof(symbols[symbol_count].name))
            name_len = sizeof(symbols[symbol_count].name) - 1;
        memcpy(symbols[symbol_count].name, name, name_len);
        symbols[symbol_count].name[name_len] = '\0';
        symbols[symbol_count].type = type;
        symbols[symbol_count].is_global = !inside_function;
        if (class_type) {
            size_t class_len = strlen(class_type);
            if (class_len >= sizeof(symbols[symbol_count].class_type))
                class_len = sizeof(symbols[symbol_count].class_type) - 1;
            memcpy(symbols[symbol_count].class_type, class_type, class_len);
            symbols[symbol_count].class_type[class_len] = '\0';
        }
        else symbols[symbol_count].class_type[0] = '\0';
        symbol_count++;
    } else if (sym) {
        sym->type = type;
        if (class_type) {
            size_t class_len = strlen(class_type);
            if (class_len >= sizeof(sym->class_type))
                class_len = sizeof(sym->class_type) - 1;
            memcpy(sym->class_type, class_type, class_len);
            sym->class_type[class_len] = '\0';
        }
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
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n < 0 || (size_t)n >= sizeof(buf)) {
        fprintf(stderr, "pythont: generated class code is too large\n");
        return;
    }

    size_t l = (size_t)n;
    if (class_pos + l >= sizeof(class_struct_buffer) - 1) {
        fprintf(stderr, "pythont: class code buffer exhausted\n");
        return;
    }
    memcpy(class_struct_buffer + class_pos, buf, l + 1);
    class_pos += l;
}

static int format_checked(char *out, size_t cap, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(out, cap, fmt, args);
    va_end(args);

    if (n < 0 || (size_t)n >= cap) {
        fprintf(stderr, "pythont: generated C code is too large\n");
        return 0;
    }
    return 1;
}

static void emit(const char *fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n < 0 || (size_t)n >= sizeof(buf)) {
        fprintf(stderr, "pythont: generated code fragment is too large\n");
        return;
    }

    size_t l = (size_t)n;
    if (inside_function) {
        if (func_pos + l >= (MAX_CODE_SZ / 2) - 1) {
            fprintf(stderr, "pythont: function code buffer exhausted\n");
            return;
        }
        memcpy(func_buffer + func_pos, buf, l + 1);
        func_pos += l;
    } else {
        if (main_pos + l >= (MAX_CODE_SZ / 2) - 1) {
            fprintf(stderr, "pythont: main code buffer exhausted\n");
            return;
        }
        memcpy(main_buffer + main_pos, buf, l + 1);
        main_pos += l;
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

static int run_process(const char *program, char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(program, argv);
        _exit(127);
    }
    int status;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return status;
}

static int append_format(char *out, size_t *pos, size_t cap, const char *fmt, ...) {
    if (*pos >= cap) {
        fprintf(stderr, "pythont: output buffer exhausted\n");
        return 0;
    }

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(out + *pos, cap - *pos, fmt, args);
    va_end(args);

    if (n < 0 || (size_t)n >= cap - *pos) {
        fprintf(stderr, "pythont: output buffer exhausted\n");
        return 0;
    }

    *pos += (size_t)n;
    return 1;
}

static int append_fragment(char *out, size_t *pos, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (*pos > cap - 1 || n >= cap - *pos) {
        fprintf(stderr, "pythont: expression buffer exhausted\n");
        return 0;
    }
    memcpy(out + *pos, src, n);
    *pos += n;
    out[*pos] = '\0';
    return 1;
}

static void replace_operators(char *expr) {
    char tmp[4096] = "";
    size_t t = 0;
    size_t len = strlen(expr);
    int in_string = 0;
    char quote = '\0';

    for (size_t i = 0; i < len; i++) {
        if ((expr[i] == '"' || expr[i] == '\'') &&
            (i == 0 || expr[i - 1] != '\\')) {
            char one[2] = { expr[i], '\0' };
            if (!append_fragment(tmp, &t, sizeof(tmp), one)) return;
            if (!in_string) {
                in_string = 1;
                quote = expr[i];
            } else if (quote == expr[i]) {
                in_string = 0;
                quote = '\0';
            }
            continue;
        }

        if (in_string) {
            char one[2] = { expr[i], '\0' };
            if (!append_fragment(tmp, &t, sizeof(tmp), one)) return;
            continue;
        }


        if (strncmp(expr + i, " and ", 5) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), " && ")) return;
            i += 4;
        } else if (strncmp(expr + i, " or ", 4) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), " || ")) return;
            i += 3;
        } else if (strncmp(expr + i, "not ", 4) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "!")) return;
            i += 3;
        } else if (strncmp(expr + i, "True", 4) == 0 && !isalnum((unsigned char)expr[i+4]) && expr[i+4] != '_') {
            if (!append_fragment(tmp, &t, sizeof(tmp), "1")) return;
            i += 3;
        } else if (strncmp(expr + i, "False", 5) == 0 && !isalnum((unsigned char)expr[i+5]) && expr[i+5] != '_') {
            if (!append_fragment(tmp, &t, sizeof(tmp), "0")) return;
            i += 4;
        } else if (strncmp(expr + i, "None", 4) == 0 && !isalnum((unsigned char)expr[i+4])) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "NULL")) return;
            i += 3;
        } else if (strncmp(expr + i, "min(", 4) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "py_min(")) return;
            i += 3;
        } else if (strncmp(expr + i, "max(", 4) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "py_max(")) return;
            i += 3;
        } else if (strncmp(expr + i, "abs(", 4) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "py_abs(")) return;
            i += 3;
        } else if (strncmp(expr + i, "int(", 4) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "py_int(")) return;
            i += 3;
        } else if (strncmp(expr + i, "str(", 4) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "py_str(")) return;
            i += 3;
        } else if (strncmp(expr + i, "float(", 6) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "py_float(")) return;
            i += 5;
        } else if (strncmp(expr + i, "round(", 6) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "py_round(")) return;
            i += 5;
        } else if (strncmp(expr + i, "bin(", 4) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "py_bin(")) return;
            i += 3;
        } else if (strncmp(expr + i, "hex(", 4) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "py_hex(")) return;
            i += 3;
        } else if (strncmp(expr + i, "oct(", 4) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "py_oct(")) return;
            i += 3;
        } else if (strncmp(expr + i, "chr(", 4) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "py_chr(")) return;
            i += 3;
        } else if (strncmp(expr + i, "ord(", 4) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "py_ord(")) return;
            i += 3;
        } else if (strncmp(expr + i, "input(", 6) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "py_input(")) return;
            i += 5;
        } else if (strncmp(expr + i, "sum(", 4) == 0) {
            char target[64] = "";
            size_t k = i + 4, p = 0;
            while (expr[k] && expr[k] != ')' && p < sizeof(target) - 1) target[p++] = expr[k++];
            target[p] = '\0';
            if (expr[k] == ')') {
                char sum_call[256];
                snprintf(sum_call, sizeof(sum_call), "py_sum(%s, len_%s)", target, target);
                if (!append_fragment(tmp, &t, sizeof(tmp), sum_call)) return;
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
                    if (!append_fragment(tmp, &t, sizeof(tmp), len_call)) return;
                } else if (s && s->type == VAR_DICT) {
                    char len_call[128];
                    snprintf(len_call, sizeof(len_call), "(int64_t)%s.count", target);
                    if (!append_fragment(tmp, &t, sizeof(tmp), len_call)) return;
                } else {
                    char len_call[128];
                    snprintf(len_call, sizeof(len_call), "(int64_t)strlen(%s)", target);
                    if (!append_fragment(tmp, &t, sizeof(tmp), len_call)) return;
                }
                i = k;
            }
        } else if (strncmp(expr + i, "math.sqrt(", 10) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "sqrt(")) return;
            i += 9;
        } else if (strncmp(expr + i, "math.sin(", 9) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "sin(")) return;
            i += 8;
        } else if (strncmp(expr + i, "math.cos(", 9) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "cos(")) return;
            i += 8;
        } else if (strncmp(expr + i, "math.floor(", 11) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "floor(")) return;
            i += 10;
        } else if (strncmp(expr + i, "math.ceil(", 10) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "ceil(")) return;
            i += 9;
        } else if (strncmp(expr + i, "math.pow(", 9) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "pow(")) return;
            i += 8;
        } else if (strncmp(expr + i, "math.pi", 7) == 0 && !isalnum((unsigned char)expr[i+7]) && expr[i+7] != '_') {
            if (!append_fragment(tmp, &t, sizeof(tmp), "3.14159265358979323846")) return;
            i += 6;
        } else if (strncmp(expr + i, "math.e", 6) == 0 && !isalnum((unsigned char)expr[i+6]) && expr[i+6] != '_') {
            if (!append_fragment(tmp, &t, sizeof(tmp), "2.71828182845904523536")) return;
            i += 5;
        } else if (strncmp(expr + i, "//", 2) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "/")) return;
            i += 1;
        } else if (strncmp(expr + i, "self.", 5) == 0) {
            if (!append_fragment(tmp, &t, sizeof(tmp), "self->")) return;
            i += 4;
        } else {
            if (t + 1 >= sizeof(tmp)) {
                fprintf(stderr, "pythont: expression buffer exhausted\n");
                return;
            }
            tmp[t++] = expr[i];
            tmp[t] = '\0';
        }
    }
    snprintf(expr, 4096, "%s", tmp);
}

static int append_expr_text(char *out, size_t *pos, size_t cap, const char *text) {
    size_t n = strlen(text);
    if (*pos + n >= cap) {
        fprintf(stderr, "pythont: generated expression is too large\n");
        return 0;
    }
    memcpy(out + *pos, text, n);
    *pos += n;
    out[*pos] = '\0';
    return 1;
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
                if (!append_expr_text(out, &o, sizeof(out), call)) return;
                continue;
            }
            if (expr[i] == '.' && (strncmp(expr + i, ".lower()", 8) == 0)) {
                i += 8;
                char call[256]; snprintf(call, sizeof(call), "py_str_lower(%s)", ident);
                if (!append_expr_text(out, &o, sizeof(out), call)) return;
                continue;
            }
            if (expr[i] == '.' && (strncmp(expr + i, ".strip()", 8) == 0)) {
                i += 8;
                char call[256]; snprintf(call, sizeof(call), "py_str_strip(%s)", ident);
                if (!append_expr_text(out, &o, sizeof(out), call)) return;
                continue;
            }
            if (expr[i] == '.' && (strncmp(expr + i, ".title()", 8) == 0)) {
                i += 8;
                char call[256]; snprintf(call, sizeof(call), "py_str_title(%s)", ident);
                if (!append_expr_text(out, &o, sizeof(out), call)) return;
                continue;
            }
            if (expr[i] == '.' && (strncmp(expr + i, ".capitalize()", 13) == 0)) {
                i += 13;
                char call[256]; snprintf(call, sizeof(call), "py_str_capitalize(%s)", ident);
                if (!append_expr_text(out, &o, sizeof(out), call)) return;
                continue;
            }
            if (expr[i] == '.' && (strncmp(expr + i, ".startswith(", 12) == 0)) {
                i += 12;
                char p_arg[128] = ""; size_t p_l = 0;
                while (i < len && expr[i] != ')' && p_l < sizeof(p_arg) - 1) p_arg[p_l++] = expr[i++];
                p_arg[p_l] = '\0';
                if (expr[i] == ')') i++;
                normalize_quotes_in_str(p_arg);
                char call[512];
                snprintf(call, sizeof(call), "py_str_startswith(%s, %s)", ident, p_arg);
                if (!append_expr_text(out, &o, sizeof(out), call)) return;
                continue;
            }
            if (expr[i] == '.' && (strncmp(expr + i, ".endswith(", 10) == 0)) {
                i += 10;
                char p_arg[128] = ""; size_t p_l = 0;
                while (i < len && expr[i] != ')' && p_l < sizeof(p_arg) - 1) p_arg[p_l++] = expr[i++];
                p_arg[p_l] = '\0';
                if (expr[i] == ')') i++;
                normalize_quotes_in_str(p_arg);
                char call[512];
                snprintf(call, sizeof(call), "py_str_endswith(%s, %s)", ident, p_arg);
                if (!append_expr_text(out, &o, sizeof(out), call)) return;
                continue;
            }
            if (expr[i] == '.' && (strncmp(expr + i, ".count(", 7) == 0)) {
                i += 7;
                char p_arg[128] = ""; size_t p_l = 0;
                while (i < len && expr[i] != ')' && p_l < sizeof(p_arg) - 1) p_arg[p_l++] = expr[i++];
                p_arg[p_l] = '\0';
                if (expr[i] == ')') i++;
                normalize_quotes_in_str(p_arg);
                char call[512];
                if (sym && sym->type == VAR_LIST) {
                    snprintf(call, sizeof(call), "py_list_count(%s, len_%s, %s)", ident, ident, p_arg);
                } else {
                    snprintf(call, sizeof(call), "py_str_count(%s, %s)", ident, p_arg);
                }
                if (!append_expr_text(out, &o, sizeof(out), call)) return;
                continue;
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
                if (!append_expr_text(out, &o, sizeof(out), call)) return;
                continue;
            }

            // Métodos de Dict
            if (sym && sym->type == VAR_DICT && expr[i] == '.') {
                if (strncmp(expr + i, ".keys()", 7) == 0) {
                    i += 7;
                    char call[256]; snprintf(call, sizeof(call), "py_dict_keys(&%s)", ident);
                    if (!append_expr_text(out, &o, sizeof(out), call)) return;
                continue;
                }
                if (strncmp(expr + i, ".values()", 9) == 0) {
                    i += 9;
                    char call[256]; snprintf(call, sizeof(call), "py_dict_values(&%s)", ident);
                    if (!append_expr_text(out, &o, sizeof(out), call)) return;
                continue;
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
                    if (!append_expr_text(out, &o, sizeof(out), call)) return;
                continue;
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
                    if (!append_expr_text(out, &o, sizeof(out), m_call)) return;
                    i--; continue;
                } else {
                    char f_access[128];
                    snprintf(f_access, sizeof(f_access), "%s.%s", ident, method_name);
                    if (!append_expr_text(out, &o, sizeof(out), f_access)) return;
                    i--; continue;
                }
            }

            if (sym && sym->type == VAR_FILE && strncmp(expr + i, ".read()", 7) == 0) {
                i += 7;
                char call[256]; snprintf(call, sizeof(call), "py_file_read(&%s)", ident);
                if (!append_expr_text(out, &o, sizeof(out), call)) return;
                continue;
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
                if (!append_expr_text(out, &o, sizeof(out), dict_call)) return;
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
                        if (*slice_content) snprintf(s_start, sizeof(s_start), "%s", slice_content);
                        if (* (colon1 + 1)) snprintf(s_end, sizeof(s_end), "%s", colon1 + 1);
                        if (* (colon2 + 1)) snprintf(s_step, sizeof(s_step), "%s", colon2 + 1);
                    } else if (colon1) {
                        *colon1 = '\0';
                                            }
                    char slice_call[512];
                    snprintf(slice_call, sizeof(slice_call), "py_str_slice(%s, %s, %s, %s)", ident, s_start, s_end, s_step);
                    if (!append_expr_text(out, &o, sizeof(out), slice_call)) return;
                continue;
                }
            }

            if (o + id_len >= sizeof(out)) {
                fprintf(stderr, "pythont: generated expression is too large\n");
                return;
            }
            memcpy(out + o, ident, id_len);
            o += id_len;
            i--;
        } else {
            if (o + 1 >= sizeof(out)) {
            fprintf(stderr, "pythont: generated expression is too large\n");
            return;
        }
        out[o++] = expr[i];
        }
    }
    out[o] = '\0';
    strcpy(expr, out);
}

static void transpile_fstring(const char *fstr, char *out_fmt, char *out_args) {
    out_fmt[0] = '\0'; out_args[0] = '\0';
    size_t fmt_pos = 0, args_pos = 0;
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
            if (*p != '}') {
                fprintf(stderr, "pythont: f-string expression is too large or unterminated\\n");
                return;
            }
            p++;

            char clean_expr[512];
            snprintf(clean_expr, sizeof(clean_expr), "%s", raw_expr);
            normalize_quotes_in_str(clean_expr);
            replace_operators(clean_expr);
            transform_advanced_expressions(clean_expr);

            char *trim_e = clean_expr;
            while (*trim_e == ' ') trim_e++;

            symbol_t *sym = find_symbol(trim_e);

            if (sym && sym->type == VAR_LIST) {
                if (!append_fragment(fmt_buf, &fmt_pos, sizeof(fmt_buf), "%s")) return;
                if (arg_cnt > 0 && !append_fragment(args_buf, &args_pos, sizeof(args_buf), ", ")) return;
                char list_call[1100];
                int n = snprintf(list_call, sizeof(list_call), "py_list_repr(%s, len_%s)", trim_e, trim_e);
                if (n < 0 || (size_t)n >= sizeof(list_call) ||
                    !append_fragment(args_buf, &args_pos, sizeof(args_buf), list_call)) return;
                arg_cnt++;
                continue;
            }

            int is_str = is_string_expression(trim_e);
            int is_flt = is_float_expression(trim_e);

            if (is_str) {
                if (!append_fragment(fmt_buf, &fmt_pos, sizeof(fmt_buf), "%s")) return;
            } else if (is_flt) {
                if (!append_fragment(fmt_buf, &fmt_pos, sizeof(fmt_buf), "%f")) return;
            } else {
                if (!append_fragment(fmt_buf, &fmt_pos, sizeof(fmt_buf), "%lld")) return;
            }

            if (arg_cnt > 0 && !append_fragment(args_buf, &args_pos, sizeof(args_buf), ", ")) return;

            if (is_str) {
                if (!append_fragment(args_buf, &args_pos, sizeof(args_buf), trim_e)) return;
            } else if (is_flt) {
                char cast_arg[544];
                int n = snprintf(cast_arg, sizeof(cast_arg), "(double)(%s)", trim_e);
                if (n < 0 || (size_t)n >= sizeof(cast_arg) ||
                    !append_fragment(args_buf, &args_pos, sizeof(args_buf), cast_arg)) return;
            } else {
                char cast_arg[544];
                int n = snprintf(cast_arg, sizeof(cast_arg), "(long long)(%s)", trim_e);
                if (n < 0 || (size_t)n >= sizeof(cast_arg) ||
                    !append_fragment(args_buf, &args_pos, sizeof(args_buf), cast_arg)) return;
            }
            arg_cnt++;
        } else {
            if (*p == '%') {
                if (!append_fragment(fmt_buf, &fmt_pos, sizeof(fmt_buf), "%%")) return;
            } else {
                char ch[2] = { *p, '\0' };
                if (!append_fragment(fmt_buf, &fmt_pos, sizeof(fmt_buf), ch)) return;
            }
            p++;
        }
    }

    snprintf(out_fmt, 1024, "%s", fmt_buf);
    snprintf(out_args, 2048, "%s", args_buf);
}

static void transpile_print(const char *args_str) {
    char fmt_str[1024] = "";
    char val_list[4096] = "";
    size_t fmt_pos = 0, val_pos = 0;
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
        if (token_len >= sizeof(token)) {
            fprintf(stderr, "pythont: print argument is too large\\n");
            return;
        }
        memcpy(token, token_start, token_len);
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
            if (!first && !append_fragment(fmt_str, &fmt_pos, sizeof(fmt_str), " ")) return;
            if (!append_fragment(fmt_str, &fmt_pos, sizeof(fmt_str), f_fmt)) return;
            if (strlen(f_args) > 0) {
                if (!first && strlen(val_list) > 0 &&
                    !append_fragment(val_list, &val_pos, sizeof(val_list), ", ")) return;
                if (!append_fragment(val_list, &val_pos, sizeof(val_list), f_args)) return;
            }
            first = 0;
            continue;
        }

        if (!first && !append_fragment(fmt_str, &fmt_pos, sizeof(fmt_str), " ")) return;

        normalize_quotes_in_str(t);
        replace_operators(t);
        transform_advanced_expressions(t);

        symbol_t *sym = find_symbol(t);

        if (sym && sym->type == VAR_LIST) {
            if (!append_fragment(fmt_str, &fmt_pos, sizeof(fmt_str), "%s")) return;
            if (!first && strlen(val_list) > 0 &&
                !append_fragment(val_list, &val_pos, sizeof(val_list), ", ")) return;
            char list_str_call[2100];
            int n = snprintf(list_str_call, sizeof(list_str_call), "py_list_repr(%s, len_%s)", t, t);
            if (n < 0 || (size_t)n >= sizeof(list_str_call) ||
                !append_fragment(val_list, &val_pos, sizeof(val_list), list_str_call)) return;
            first = 0;
            continue;
        }

        int is_str = is_string_expression(t);
        int is_flt = is_float_expression(t);

        if (is_str) {
            if (!append_fragment(fmt_str, &fmt_pos, sizeof(fmt_str), "%s")) return;
        } else if (is_flt) {
            if (!append_fragment(fmt_str, &fmt_pos, sizeof(fmt_str), "%f")) return;
        } else {
            if (!append_fragment(fmt_str, &fmt_pos, sizeof(fmt_str), "%lld")) return;
        }

        if (!first && strlen(val_list) > 0 &&
            !append_fragment(val_list, &val_pos, sizeof(val_list), ", ")) return;

        if (is_str) {
            if (!append_fragment(val_list, &val_pos, sizeof(val_list), t)) return;
        } else if (is_flt) {
            char cast_val[1050];
            int n = snprintf(cast_val, sizeof(cast_val), "(double)(%s)", t);
            if (n < 0 || (size_t)n >= sizeof(cast_val) ||
                !append_fragment(val_list, &val_pos, sizeof(val_list), cast_val)) return;
        } else {
            char cast_val[1050];
            int n = snprintf(cast_val, sizeof(cast_val), "(long long)(%s)", t);
            if (n < 0 || (size_t)n >= sizeof(cast_val) ||
                !append_fragment(val_list, &val_pos, sizeof(val_list), cast_val)) return;
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
        memcpy(b_var, block_var[block_top - 1], 63);
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
            memcpy(block_var[block_top], pending_var, 63);
            block_var[block_top][63] = '\0';
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
        memcpy(pending_var, var_name, 63);
        pending_var[63] = '\0';
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

        memcpy(active_class, cname, 63);
        active_class[63] = '\0';
        if (class_count < MAX_CLASSES) {
            memcpy(classes[class_count].class_name, cname, 63);
            classes[class_count].class_name[63] = '\0';
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
                size_t fn_pos = strlen(fn_decl);
                if (!fst && !append_fragment(fn_decl, &fn_pos, sizeof(fn_decl), ", ")) return;
                if (!append_fragment(fn_decl, &fn_pos, sizeof(fn_decl), "int64_t ")) return;
                if (!append_fragment(fn_decl, &fn_pos, sizeof(fn_decl), p_tok)) return;
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
                while (*v1 == ' ')
                    v1++;
                while (*v2 == ' ')
                    v2++;
                while (*e1 == ' ')
                    e1++;
                while (*e2 == ' ')
                    e2++;
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
            int in_init_block = block_top > 0 && block_type[block_top - 1] == BLOCK_INIT;
            if (inside_function && strlen(active_class) > 0 && in_init_block)
                emit("    self.%s = %s;\n", field, vexpr_start);
            else if (inside_function && strlen(active_class) > 0)
                emit("    self->%s = %s;\n", field, vexpr_start);
            else
                emit("    self->%s = %s;\n", field, vexpr_start);
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
            size_t inner_len = elen - 2;
            if (inner_len >= sizeof(inner_dict)) {
                fprintf(stderr, "pythont: dictionary literal is too large\n");
                return;
            }
            memcpy(inner_dict, vexpr_start + 1, inner_len);
            inner_dict[inner_len] = '\0';

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
                size_t items_len = elen - 2;
                if (items_len >= sizeof(items_only)) {
                    fprintf(stderr, "pythont: list literal is too large\n");
                    return;
                }
                memcpy(items_only, vexpr_start + 1, items_len);
                items_only[items_len] = '\0';
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

        pid_t pid = fork();
        if (pid < 0) {
            perror("pythont: fork");
            continue;
        }
        if (pid == 0) {
            execl("./pythont", "./pythont", "-e", line_buf, (char *)NULL);
            perror("pythont: execl");
            _exit(127);
        }
        int status;
        if (waitpid(pid, &status, 0) < 0) {
            perror("pythont: waitpid");
            continue;
        }
        if (WIFSIGNALED(status))
            fprintf(stderr, "pythont: REPL child terminated by signal %d\n", WTERMSIG(status));
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
    int dump_tokens = 0;
    int dump_ast = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) inline_code = argv[++i];
        else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--emit-c") == 0) emit_c_only = 1;
        else if (strcmp(argv[i], "--tokens") == 0) dump_tokens = 1;
        else if (strcmp(argv[i], "--ast") == 0) dump_ast = 1;
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out_bin = argv[++i];
        else if (!py_file && !inline_code) py_file = argv[i];
    }

    if (dump_ast) {
        const char *source = inline_code;
        char *source_copy = NULL;

        if (!source && py_file) {
            FILE *fp = fopen(py_file, "rb");
            if (!fp) {
                fprintf(stderr, "pythont: erro ao abrir '%s': %s\\n",
                        py_file, strerror(errno));
                utilipc_close();
                return 1;
            }
            if (fseek(fp, 0, SEEK_END) != 0) {
                fclose(fp);
                utilipc_close();
                return 1;
            }
            long size = ftell(fp);
            if (size < 0 || (size_t)size >= MAX_CODE_SZ) {
                fclose(fp);
                fprintf(stderr, "pythont: arquivo grande demais para AST\\n");
                utilipc_close();
                return 1;
            }
            rewind(fp);
            source_copy = malloc((size_t)size + 1);
            if (!source_copy) {
                fclose(fp);
                utilipc_close();
                return 1;
            }
            size_t read_size = fread(source_copy, 1, (size_t)size, fp);
            fclose(fp);
            source_copy[read_size] = '\\0';
            source = source_copy;
        }

        int ast_result = ast_dump_source(source ? source : "");
        free(source_copy);
        utilipc_close();
        return ast_result;
    }

    if (dump_tokens) {
        const char *source = inline_code;

        if (!source && py_file) {
            FILE *fp = fopen(py_file, "rb");
            if (!fp) {
                fprintf(stderr, "pythont: erro ao abrir '%s': %s\\n",
                        py_file, strerror(errno));
                utilipc_close();
                return 1;
            }

            if (fseek(fp, 0, SEEK_END) != 0) {
                fclose(fp);
                utilipc_close();
                return 1;
            }

            long size = ftell(fp);
            if (size < 0 || (size_t)size >= MAX_CODE_SZ) {
                fclose(fp);
                fprintf(stderr, "pythont: arquivo grande demais para tokenizacao\\n");
                utilipc_close();
                return 1;
            }

            rewind(fp);
            char *source_copy = malloc((size_t)size + 1);
            if (!source_copy) {
                fclose(fp);
                fprintf(stderr, "pythont: memoria insuficiente para tokenizacao\\n");
                utilipc_close();
                return 1;
            }

            size_t read_size = fread(source_copy, 1, (size_t)size, fp);
            fclose(fp);
            source_copy[read_size] = '\0';
            lexer_dump(source_copy);
            free(source_copy);
        } else {
            lexer_dump(source ? source : "");
        }

        utilipc_close();
        return 0;
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
            size_t needed = strlen(accum) + (accum[0] != '\0' ? 1 : 0) + tl + 1;
            if (needed > sizeof(accum)) {
                fprintf(stderr, "pythont: logical Python line exceeds 4095 characters\n");
                fclose(fp);
                utilipc_close();
                return 1;
            }
            size_t accum_pos = strlen(accum);
            if (accum[0] != '\0' && !append_fragment(accum, &accum_pos, sizeof(accum), " ")) {
                fclose(fp);
                utilipc_close();
                return 1;
            }
            if (!append_fragment(accum, &accum_pos, sizeof(accum), trimmed)) {
                fclose(fp);
                utilipc_close();
                return 1;
            }

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
    size_t var_decl_pos = 0;
    for (int i = 0; i < symbol_count; i++) {
        if (!symbols[i].is_global) continue;

        int ok = 1;
        if (symbols[i].type == VAR_INT)
            ok = append_format(var_decl_buf, &var_decl_pos, sizeof(var_decl_buf),
                               "    int64_t %s = 0;\n", symbols[i].name);
        else if (symbols[i].type == VAR_FLOAT)
            ok = append_format(var_decl_buf, &var_decl_pos, sizeof(var_decl_buf),
                               "    double %s = 0.0;\n", symbols[i].name);
        else if (symbols[i].type == VAR_STR)
            ok = append_format(var_decl_buf, &var_decl_pos, sizeof(var_decl_buf),
                               "    const char *%s = \"\";\n", symbols[i].name);
        else if (symbols[i].type == VAR_LIST)
            ok = append_format(var_decl_buf, &var_decl_pos, sizeof(var_decl_buf),
                               "    int64_t %s[%d] = {0};\n    int64_t len_%s = 0;\n",
                               symbols[i].name, MAX_LIST_SZ, symbols[i].name);
        else if (symbols[i].type == VAR_DICT)
            ok = append_format(var_decl_buf, &var_decl_pos, sizeof(var_decl_buf),
                               "    py_dict_t %s; py_dict_init(&%s);\n",
                               symbols[i].name, symbols[i].name);
        else if (symbols[i].type == VAR_FILE)
            ok = append_format(var_decl_buf, &var_decl_pos, sizeof(var_decl_buf),
                               "    py_file_t %s = {0};\n", symbols[i].name);
        else if (symbols[i].type == VAR_OBJ)
            ok = append_format(var_decl_buf, &var_decl_pos, sizeof(var_decl_buf),
                               "    %s %s = {0};\n", symbols[i].class_type, symbols[i].name);

        if (!ok) {
            utilipc_close();
            return 1;
        }
    }

    char final_c_code[MAX_CODE_SZ];
    if (!format_checked(final_c_code, sizeof(final_c_code),
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
        "__attribute__((unused)) static inline int64_t py_int_str(const char *s) { return (int64_t)strtoll(s, NULL, 10); }\n"
        "__attribute__((unused)) static inline int64_t py_int_num(double d) { return (int64_t)d; }\n"
        "#define py_int(x) _Generic((x), char*: py_int_str, const char*: py_int_str, default: py_int_num)(x)\n"
        "__attribute__((unused)) static inline double py_float_str(const char *s) { return strtod(s, NULL); }\n"
        "__attribute__((unused)) static inline double py_float_num(double d) { return d; }\n"
        "#define py_float(x) _Generic((x), char*: py_float_str, const char*: py_float_str, default: py_float_num)(x)\n"
        "__attribute__((unused)) static inline int64_t py_round(double d) { return (int64_t)round(d); }\n"
        "__attribute__((unused)) static inline const char *py_str_i64(int64_t val) {\n"
        "    static char s_buf[64];\n"
        "    snprintf(s_buf, sizeof(s_buf), \"%%lld\", (long long)val);\n"
        "    return s_buf;\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_str_double(double val) {\n"
        "    static char s_buf[64];\n"
        "    snprintf(s_buf, sizeof(s_buf), \"%%g\", val);\n"
        "    return s_buf;\n"
        "}\n"
        "__attribute__((unused)) static inline const char *py_str_text(const char *val) { return val ? val : \"None\"; }\n"
        "#define py_str(x) _Generic((x), char*: py_str_text, const char*: py_str_text, float: py_str_double, double: py_str_double, default: py_str_i64)(x)\n"
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
        "        size_t list_len = strlen(list_buf), elem_len = strlen(elem);\n"
        "        if (list_len + elem_len + 1 >= sizeof(list_buf)) return \"[list too large]\";\n"
        "        memcpy(list_buf + list_len, elem, elem_len + 1);\n"
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