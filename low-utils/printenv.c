#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET     "\033[0m"
#define COLOR_BORDER    "\033[1;34m" // Azul escuro moderno
#define COLOR_HDR_BG    "\033[1;37;44m"
#define COLOR_KEY       "\033[1;36m" // Ciano negrito
#define COLOR_VAL_STR   "\033[0;37m" // Branco suave legível
#define COLOR_VAL_NUM   "\033[1;33m" // Amarelo
#define COLOR_VAL_PATH  "\033[38;2;166;227;161m" // Verde Menta suave
#define COLOR_SEC       "\033[1;31m" // Vermelho para segredos
#define COLOR_MUTED     "\033[0;90m" // Cinza
#define COLOR_OK        "\033[1;32m" // Verde sucesso
#define COLOR_WARN      "\033[1;33m" // Amarelo alerta
#define COLOR_ERR       "\033[1;31m" // coisa ai de erro
// Badges de Categoria
#define BADGE_PATH  "\033[1;33m[PATH]\033[0m"
#define BADGE_USER  "\033[1;32m[USER]\033[0m"
#define BADGE_SHLL  "\033[1;36m[SHLL]\033[0m"
#define BADGE_DEV   "\033[1;35m[DEV ]\033[0m"
#define BADGE_SEC   "\033[1;31m[SEC ]\033[0m"
#define BADGE_SYS   "\033[1;34m[SYS ]\033[0m"

extern char **environ;

typedef struct {
    char *key;
    char *val;
    const char *badge;
    int is_secret;
    int is_path;
} EnvEntry;

static void get_term_size(int *cols, int *rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    } else {
        *cols = 80;
        *rows = 24;
    }
}

static void print_help(void) {
    low_print_banner("printenv");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./printenv [OPTIONS] [VARIABLE_NAME]\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Professional semantic environment variables viewer with PATH auditor & secret shielding.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-P, --path [VAR]%s         Inspect & audit PATH components (tests directories for X_OK permissions)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-p, --pid <PID>%s          Inspect environment variables from another active process\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-s, --search <QUERY>%s     Search for QUERY in both variable names and values\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s--unmask%s                 Reveal sensitive passwords/tokens (masked by default)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-r, --raw%s                Print standard unformatted KEY=VALUE (script-friendly)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s               Display this formatted help guide and exit\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./printenv%s                     (Tabela profissional colorida e categorizada)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./printenv -P%s                  (Auditoria do $PATH: checa quais pastas existem e funcionam)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./printenv -P LD_LIBRARY_PATH%s  (Auditoria de bibliotecas)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./printenv -s android%s          (Busca variaveis contendo 'android')\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./printenv -p 1234%s             (Examina variaveis de outro processo)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

// Analisa e categoriza a variável para aplicar cor e badge
static void categorize_variable(const char *key, const char **badge, int *is_secret, int *is_path) {
    *is_secret = 0;
    *is_path = 0;

    // 1. Variáveis Sensíveis / Credenciais
    if (strcasestr(key, "SECRET") || strcasestr(key, "PASSWORD") || strcasestr(key, "PASSWD") ||
        strcasestr(key, "TOKEN") || strcasestr(key, "AUTH") || strcasestr(key, "API_KEY") ||
        strcasestr(key, "PRIVATE") || strcasestr(key, "CREDENTIAL")) {
        *badge = BADGE_SEC;
        *is_secret = 1;
        return;
    }

    // 2. Variáveis de Caminho (Paths)
    if (strcmp(key, "PATH") == 0 || strcasestr(key, "_PATH") || strcasestr(key, "_DIR") ||
        strcmp(key, "HOME") == 0 || strcmp(key, "PWD") == 0 || strcmp(key, "OLDPWD") == 0 ||
        strcmp(key, "TMPDIR") == 0 || strcmp(key, "PREFIX") == 0) {
        *badge = BADGE_PATH;
        *is_path = 1;
        return;
    }

    // 3. Usuário e Identidade
    if (strcmp(key, "USER") == 0 || strcmp(key, "LOGNAME") == 0 || strcmp(key, "UID") == 0 ||
        strcmp(key, "EUID") == 0 || strcmp(key, "GID") == 0) {
        *badge = BADGE_USER;
        return;
    }

    // 4. Shell e Terminal
    if (strcmp(key, "SHELL") == 0 || strcmp(key, "TERM") == 0 || strcmp(key, "COLORTERM") == 0 ||
        strcmp(key, "SHLVL") == 0 || strcmp(key, "PS1") == 0 || strncmp(key, "LC_", 3) == 0 ||
        strcmp(key, "LANG") == 0 || strcmp(key, "EDITOR") == 0 || strcmp(key, "VISUAL") == 0) {
        *badge = BADGE_SHLL;
        return;
    }

    // 5. Runtimes de Desenvolvimento (Python, Java, Node, GCC)
    if (strcasestr(key, "PYTHON") || strcasestr(key, "JAVA") || strcasestr(key, "NODE") ||
        strcasestr(key, "RUST") || strcasestr(key, "CARGO") || strcasestr(key, "GO") ||
        strcmp(key, "CC") == 0 || strcmp(key, "CXX") == 0 || strstr(key, "FLAGS")) {
        *badge = BADGE_DEV;
        return;
    }

    *badge = BADGE_SYS;
}

// Auditoria profunda de caminhos (PATH / LD_LIBRARY_PATH)
static void audit_path_variable(const char *var_name) {
    const char *val = getenv(var_name);
    if (!val || strlen(val) == 0) {
        printf("\n  %s[AVISO]%s Variavel '%s' nao definida ou vazia no ambiente.\n\n", COLOR_WARN, COLOR_RESET, var_name);
        return;
    }

    int cols, rows;
    get_term_size(&cols, &rows);

    printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", COLOR_BORDER, COLOR_RESET);
    printf("%s│%s  %s[ 🔎 AUDITORIA DE CAMINHOS: $%s ]%s%-*s%s│%s\n",
           COLOR_BORDER, COLOR_RESET, COLOR_KEY, var_name, COLOR_RESET,
           (int)(47 - strlen(var_name)), "", COLOR_BORDER, COLOR_RESET);
    printf("%s├────────────────────────────────────────────────────────────────────────────┤%s\n", COLOR_BORDER, COLOR_RESET);

    char *copy = strdup(val);
    if (!copy) return;

    char *saveptr = NULL;
    char *dir = strtok_r(copy, ":", &saveptr);
    int idx = 1;
    int valid_count = 0, missing_count = 0;

    while (dir) {
        const char *display_dir = (*dir == '\0') ? "." : dir;

        struct stat st;
        int exists = (stat(display_dir, &st) == 0);
        int is_dir = exists && S_ISDIR(st.st_mode);
        int can_exec = exists && (access(display_dir, X_OK) == 0);

        printf("  %s%2d.%s 48.48s ", COLOR_MUTED, idx++, display_dir);

        if (!exists) {
            printf("%s[✖ Inexistente]%s\n", COLOR_ERR, COLOR_RESET);
            missing_count++;
        } else if (!is_dir) {
            printf("%s[✖ Nao e Diretorio]%s\n", COLOR_WARN, COLOR_RESET);
            missing_count++;
        } else if (!can_exec) {
            printf("%s[🔒 Sem Permissao X]%s\n", COLOR_WARN, COLOR_RESET);
            valid_count++;
        } else {
            printf("%s[✔ OK / Ativo]%s\n", COLOR_OK, COLOR_RESET);
            valid_count++;
        }

        dir = strtok_r(NULL, ":", &saveptr);
    }
    free(copy);

    printf("%s├────────────────────────────────────────────────────────────────────────────┤%s\n", COLOR_BORDER, COLOR_RESET);
    printf("  %s• Resumo:%s %s%d pastas validas%s | %s%d inexistentes ou invalidas%s\n",
           COLOR_KEY, COLOR_RESET, COLOR_OK, valid_count, COLOR_RESET,
           (missing_count > 0) ? COLOR_ERR : COLOR_MUTED, missing_count, COLOR_RESET);
    printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n", COLOR_BORDER, COLOR_RESET);
}

// Leitura do ambiente de outro processo via /proc/<PID>/environ
static char **load_proc_environ(pid_t pid, int *out_count) {
    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/environ", pid);

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "printenv: erro ao ler ambiente do PID %d: %s\n", pid, strerror(errno));
        return NULL;
    }

    size_t cap = 64;
    int count = 0;
    char **env_list = malloc(cap * sizeof(char *));

    char buf[4096];
    size_t pos = 0;
    int c;

    while ((c = fgetc(fp)) != EOF) {
        if (c == '\0') {
            if (pos > 0) {
                buf[pos] = '\0';
                if (count >= (int)cap) {
                    cap *= 2;
                    env_list = realloc(env_list, cap * sizeof(char *));
                }
                env_list[count++] = strdup(buf);
                pos = 0;
            }
        } else if (pos < sizeof(buf) - 1) {
            buf[pos++] = (char)c;
        }
    }
    fclose(fp);

    if (pos > 0) {
        buf[pos] = '\0';
        env_list[count++] = strdup(buf);
    }

    *out_count = count;
    return env_list;
}

static int compare_entries(const void *a, const void *b) {
    const EnvEntry *ea = (const EnvEntry *)a;
    const EnvEntry *eb = (const EnvEntry *)b;
    return strcasecmp(ea->key, eb->key);
}

int main(int argc, char *argv[]) {
    const char *query = NULL;
    const char *search_filter = NULL;
    pid_t target_pid = 0;
    int opt_unmask = 0;
    int opt_raw = 0;
    int opt_path_audit = 0;
    const char *path_var_name = "PATH";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--raw") == 0) {
            opt_raw = 1;
            continue;
        }
        if (strcmp(argv[i], "--unmask") == 0) {
            opt_unmask = 1;
            continue;
        }
        if (strcmp(argv[i], "-P") == 0 || strcmp(argv[i], "--path") == 0) {
            opt_path_audit = 1;
            if (i + 1 < argc && argv[i+1][0] != '-') {
                path_var_name = argv[++i];
            }
            continue;
        }
        if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--pid") == 0) && i + 1 < argc) {
            target_pid = (pid_t)atoi(argv[++i]);
            continue;
        }
        if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--search") == 0) && i + 1 < argc) {
            search_filter = argv[++i];
            continue;
        }
        if (!query && argv[i][0] != '-') {
            query = argv[i];
        }
    }

    // Se solicitou auditoria de caminho (-P)
    if (opt_path_audit) {
        audit_path_variable(path_var_name);
        return 0;
    }

    char **source_environ = environ;
    int env_count = 0;
    char **proc_env = NULL;

    if (target_pid > 0) {
        proc_env = load_proc_environ(target_pid, &env_count);
        if (!proc_env) return 1;
        source_environ = proc_env;
    } else {
        while (source_environ[env_count]) env_count++;
    }

    EnvEntry *entries = malloc(env_count * sizeof(EnvEntry));
    int valid_entries = 0;

    for (int i = 0; i < env_count; i++) {
        char *eq = strchr(source_environ[i], '=');
        if (!eq) continue;

        size_t klen = eq - source_environ[i];
        char *k = malloc(klen + 1);
        strncpy(k, source_environ[i], klen);
        k[klen] = '\0';
        char *v = eq + 1;

        if (query && strcasecmp(k, query) != 0) {
            free(k);
            continue;
        }

        if (search_filter && strcasestr(k, search_filter) == NULL && strcasestr(v, search_filter) == NULL) {
            free(k);
            continue;
        }

        entries[valid_entries].key = k;
        entries[valid_entries].val = v;
        categorize_variable(k, &entries[valid_entries].badge,
                            &entries[valid_entries].is_secret,
                            &entries[valid_entries].is_path);
        valid_entries++;
    }

    // Modo Raw (estilo GNU printenv para scripts)
    if (opt_raw) {
        for (int i = 0; i < valid_entries; i++) {
            printf("%s=%s\n", entries[i].key, entries[i].val);
            free(entries[i].key);
        }
        free(entries);
        if (proc_env) {
            for (int i = 0; i < env_count; i++) free(proc_env[i]);
            free(proc_env);
        }
        return 0;
    }

    // Ordena alfabeticamente para leitura profissional
    qsort(entries, valid_entries, sizeof(EnvEntry), compare_entries);

    int cols, rows;
    get_term_size(&cols, &rows);

    int key_col_w = 26;
    int val_col_w = cols - key_col_w - 18;
    if (val_col_w < 20) val_col_w = 20;

    printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", COLOR_BORDER, COLOR_RESET);
    if (target_pid > 0) {
        printf("%s│%s  %s[ 🌐 AMBIENTE DO PROCESSO PID: %-5d ]%s                                   %s│%s\n",
               COLOR_BORDER, COLOR_RESET, COLOR_KEY, target_pid, COLOR_RESET, COLOR_BORDER, COLOR_RESET);
    } else {
        printf("%s│%s  %s[ 🌐 TABELA PROFISSIONAL DE VARIÁVEIS DE AMBIENTE ]%s                       %s│%s\n",
               COLOR_BORDER, COLOR_RESET, COLOR_KEY, COLOR_RESET, COLOR_BORDER, COLOR_RESET);
    }
    printf("%s├────────┬───────────────────────────┬────────────────────────────────────────┤%s\n", COLOR_BORDER, COLOR_RESET);
    printf("  %-6s  %s%-25.25s%s  %sVALOR / CONFIGURAÇÃO%s\n",
           "TIPO", COLOR_KEY, "VARIÁVEL", COLOR_RESET, COLOR_MUTED, COLOR_RESET);
    printf("%s├────────┼───────────────────────────┼────────────────────────────────────────┤%s\n", COLOR_BORDER, COLOR_RESET);

    for (int i = 0; i < valid_entries; i++) {
        char display_val[1024];
        if (entries[i].is_secret && !opt_unmask) {
            snprintf(display_val, sizeof(display_val), "%s•••••••••••••••• (protegido)%s", COLOR_SEC, COLOR_RESET);
        } else {
            const char *col = entries[i].is_path ? COLOR_VAL_PATH :
                              (isdigit((unsigned char)entries[i].val[0])) ? COLOR_VAL_NUM : COLOR_VAL_STR;
            snprintf(display_val, sizeof(display_val), "%s%-*.*s%s",
                     col, val_col_w, val_col_w, entries[i].val, COLOR_RESET);
        }

        printf("  %s  %s%-25.25s%s  %s\n",
               entries[i].badge,
               COLOR_KEY, entries[i].key, COLOR_RESET,
               display_val);

        free(entries[i].key);
    }

    printf("%s╰────────┴───────────────────────────┴────────────────────────────────────────╯%s\n", COLOR_BORDER, COLOR_RESET);
    printf("  %s• Total de Variáveis:%s %s%d listadas%s | %sUse -P para auditar caminhos do $PATH%s\n\n",
           COLOR_KEY, COLOR_RESET, COLOR_OK, valid_entries, COLOR_RESET, COLOR_MUTED, COLOR_RESET);

    free(entries);
    if (proc_env) {
        for (int i = 0; i < env_count; i++) free(proc_env[i]);
        free(proc_env);
    }

    return 0;
}
