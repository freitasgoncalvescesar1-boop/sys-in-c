#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <termios.h>
#include "low.h"

#define MAX_LINE_LEN 2048
#define MAX_ARGS     128
#define MAX_PIPES    16
#define MAX_HISTORY  200
#define MAX_ALIASES  32

#define COLOR_RESET   "\033[0m"
#define COLOR_PROMPT  "\033[1;35m"
#define COLOR_USER    "\033[1;32m"
#define COLOR_HOST    "\033[1;36m"
#define COLOR_PATH    "\033[1;34m"
#define COLOR_ERR_RET "\033[1;31m"
#define COLOR_TAG     "\033[1;33m"
#define COLOR_VAL     "\033[1;32m"

typedef struct {
    char name[32];
    char replacement[256];
    int active;
} lsh_alias_t;

static int last_exit_status = 0;
static char old_pwd[512] = "";
static char history[MAX_HISTORY][MAX_LINE_LEN];
static int history_count = 0;
static lsh_alias_t alias_table[MAX_ALIASES];

static struct termios orig_termios;
static int raw_mode_active = 0;

static const char *builtins_list[] = {
    "cd", "pwd", "export", "unset", "alias", "unalias", "source", ".",
    "read", "echo", "history", "which", "type", "clear", "cls", "time",
    "exec", "help", "exit", "quit", NULL
};

static double get_time_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static const char *get_history_file(void) {
    static char path[512];
    const char *home = getenv("HOME");
    if (!home || strlen(home) == 0) home = ".";
    snprintf(path, sizeof(path), "%s/.lsh_history", home);
    return path;
}

static void load_history_from_file(void) {
    FILE *fp = fopen(get_history_file(), "r");
    if (!fp) return;
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp) && history_count < MAX_HISTORY) {
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\r' || line[l - 1] == '\n')) line[--l] = '\0';
        if (l > 0) {
            strncpy(history[history_count++], line, MAX_LINE_LEN - 1);
        }
    }
    fclose(fp);
}

static void append_history_to_file(const char *cmd) {
    if (!cmd || strlen(cmd) == 0) return;
    FILE *fp = fopen(get_history_file(), "a");
    if (fp) {
        fprintf(fp, "%s\n", cmd);
        fclose(fp);
    }
}

static void add_history(const char *cmd) {
    if (!cmd || strlen(cmd) == 0) return;
    if (history_count > 0 && strcmp(history[history_count - 1], cmd) == 0) return;

    if (history_count < MAX_HISTORY) {
        strncpy(history[history_count++], cmd, MAX_LINE_LEN - 1);
    } else {
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            strcpy(history[i], history[i + 1]);
        }
        strncpy(history[MAX_HISTORY - 1], cmd, MAX_LINE_LEN - 1);
    }
    append_history_to_file(cmd);
}

static void disable_raw_mode(void) {
    if (raw_mode_active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        raw_mode_active = 0;
    }
}

static void enable_raw_mode(void) {
    if (!raw_mode_active && isatty(STDIN_FILENO)) {
        tcgetattr(STDIN_FILENO, &orig_termios);
        struct termios raw = orig_termios;
        raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
        raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
        raw.c_cflag |= (CS8);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        raw_mode_active = 1;
    }
}

static void init_default_env(void) {
    uid_t uid = geteuid();
    struct passwd *pw = getpwuid(uid);
    if (pw && pw->pw_name) {
        setenv("USER", pw->pw_name, 0);
        setenv("LOGNAME", pw->pw_name, 0);
    }

    if (!getenv("TMPDIR")) {
        if (uid != 0 && access("/data/data/com.termux/files/usr/tmp", W_OK) == 0) {
            setenv("TMPDIR", "/data/data/com.termux/files/usr/tmp", 1);
        } else if (access("/tmp", W_OK) == 0) {
            setenv("TMPDIR", "/tmp", 1);
        } else {
            setenv("TMPDIR", "/data/data/com.termux/files/usr/tmp", 1);
        }
    }

    if (!getenv("HOME")) {
        if (pw && pw->pw_dir && strlen(pw->pw_dir) > 0) {
            setenv("HOME", pw->pw_dir, 1);
        } else if (uid == 0) {
            setenv("HOME", "/root", 1);
        } else if (access("/data/data/com.termux/files/home", F_OK) == 0) {
            setenv("HOME", "/data/data/com.termux/files/home", 1);
        } else {
            setenv("HOME", "/tmp", 1);
        }
    }

    const char *cur_path = getenv("PATH");
    char new_path[4096];
    if (!cur_path || strlen(cur_path) == 0) {
        if (access("/data/data/com.termux/files/usr/bin", F_OK) == 0) {
            snprintf(new_path, sizeof(new_path), ".:/data/data/com.termux/files/usr/bin:/bin:/usr/bin:/usr/local/bin");
        } else {
            snprintf(new_path, sizeof(new_path), ".:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin");
        }
        setenv("PATH", new_path, 1);
    } else if (strncmp(cur_path, ".:", 2) != 0 && strstr(cur_path, ":.:") == NULL) {
        snprintf(new_path, sizeof(new_path), ".:%s", cur_path);
        setenv("PATH", new_path, 1);
    }

    const char *shlvl = getenv("SHLVL");
    int lvl = shlvl ? atoi(shlvl) + 1 : 1;
    char lvl_str[16];
    snprintf(lvl_str, sizeof(lvl_str), "%d", lvl);
    setenv("SHLVL", lvl_str, 1);

    char self_path[512];
    ssize_t len = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (len > 0) {
        self_path[len] = '\0';
        setenv("SHELL", self_path, 1);
    }
}

static void print_help(void) {
    low_print_banner("lsh");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./lsh                      (Sessao de Shell Interativa com TAB Autocomplete)\n");
    printf("  ./lsh -c \"<COMANDO>\"       (Executa comando em lote e sai)\n\n");
    printf("%sCOMANDOS EMBUTIDOS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %scd [DIR|-]%s               Navega entre pastas (~, -, caminhos relativos)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %spwd%s                      Exibe diretorio atual\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %sexport VAR=VAL%s           Define variaveis de ambiente\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %sunset VAR%s                Remove variaveis de ambiente\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %salias [NAME=CMD]%s         Cria ou lista apelidos de comandos\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %sunalias NAME%s             Remove um alias\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %ssource <ARQ> / . <ARQ>%s   Executa script no ambiente do shell\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %sread [-p prompt] VAR%s     Le entrada para uma variavel\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %secho [-n|-e] [TEXTO]%s     Exibe texto com suporte a escapes ANSI\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %shistory%s                  Historico de comandos salvos em ~/.lsh_history\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %stime <CMD>%s               Mede tempo de execucao com precisao\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %swhich <CMD>%s              Localiza executaveis no $PATH\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %sclear, cls%s               Limpa tela do terminal\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %sexit, quit%s               Encerra o lsh\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
}

static void sigint_handler(int sig) {
    (void)sig;
    printf("\n");
    fflush(stdout);
}

static void format_prompt(char *out_prompt, size_t max_len) {
    uid_t uid = geteuid();
    struct passwd *pw = getpwuid(uid);
    const char *user = pw ? pw->pw_name : "user";

    char cwd[512];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, ".");

    const char *home = getenv("HOME");
    char display_cwd[512];
    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        snprintf(display_cwd, sizeof(display_cwd), "~%s", cwd + strlen(home));
    } else {
        strncpy(display_cwd, cwd, sizeof(display_cwd) - 1);
    }

    if (last_exit_status != 0) {
        snprintf(out_prompt, max_len, "%s[%d]%s %s%s%s%s@sys-in-c%s:%s%s%s%s%c%s ",
                 COLOR_ERR_RET, last_exit_status, COLOR_RESET,
                 COLOR_USER, user, COLOR_RESET,
                 COLOR_HOST, COLOR_RESET,
                 COLOR_PATH, display_cwd, COLOR_RESET,
                 COLOR_PROMPT, (uid == 0) ? '#' : '$', COLOR_RESET);
    } else {
        snprintf(out_prompt, max_len, "%s%s%s%s@sys-in-c%s:%s%s%s%s%c%s ",
                 COLOR_USER, user, COLOR_RESET,
                 COLOR_HOST, COLOR_RESET,
                 COLOR_PATH, display_cwd, COLOR_RESET,
                 COLOR_PROMPT, (uid == 0) ? '#' : '$', COLOR_RESET);
    }
}

// =========================================================================
// MOTOR DE AUTOCOMPLETAR COM TAB (Comandos $PATH + Pastas/Arquivos)
// =========================================================================
static void perform_tab_completion(char *buf, size_t *len, size_t *pos, size_t max_len) {
    size_t cur = *pos;
    if (cur == 0 && *len == 0) return;

    size_t token_start = cur;
    while (token_start > 0 && buf[token_start - 1] != ' ' && buf[token_start - 1] != '\t' && buf[token_start - 1] != '|' && buf[token_start - 1] != '&' && buf[token_start - 1] != ';') {
        token_start--;
    }

    char prefix[256] = "";
    size_t plen = cur - token_start;
    if (plen >= sizeof(prefix)) plen = sizeof(prefix) - 1;
    strncpy(prefix, buf + token_start, plen);
    prefix[plen] = '\0';

    int is_command = 1;
    for (size_t i = 0; i < token_start; i++) {
        if (buf[i] != ' ' && buf[i] != '\t') {
            is_command = 0;
            break;
        }
    }

    char matches[64][256];
    int match_count = 0;

    // 1. Completa Comandos Embutidos e $PATH
    if (is_command && !strchr(prefix, '/')) {
        for (int b = 0; builtins_list[b] != NULL && match_count < 64; b++) {
            if (strncmp(builtins_list[b], prefix, plen) == 0) {
                snprintf(matches[match_count++], 255, "%s", builtins_list[b]);
            }
        }

        const char *penv = getenv("PATH");
        if (penv) {
            char pcopy[2048];
            strncpy(pcopy, penv, sizeof(pcopy) - 1);
            char *dir_token = strtok(pcopy, ":");
            while (dir_token && match_count < 64) {
                DIR *d = opendir(dir_token);
                if (d) {
                    struct dirent *de;
                    while ((de = readdir(d)) != NULL && match_count < 64) {
                        if (de->d_name[0] == '.') continue;
                        if (strncmp(de->d_name, prefix, plen) == 0) {
                            int exists = 0;
                            for (int k = 0; k < match_count; k++) {
                                if (strcmp(matches[k], de->d_name) == 0) { exists = 1; break; }
                            }
                            if (!exists) {
                                snprintf(matches[match_count++], 255, "%s", de->d_name);
                            }
                        }
                    }
                    closedir(d);
                }
                dir_token = strtok(NULL, ":");
            }
        }
    }

    // 2. Completa Arquivos e Pastas Locais
    if (match_count == 0) {
        char dir_path[256] = ".";
        char file_prefix[128] = "";

        const char *last_slash = strrchr(prefix, '/');
        if (last_slash) {
            size_t dlen = last_slash - prefix;
            if (dlen == 0) strcpy(dir_path, "/");
            else {
                strncpy(dir_path, prefix, dlen);
                dir_path[dlen] = '\0';
            }
            strncpy(file_prefix, last_slash + 1, sizeof(file_prefix) - 1);
        } else {
            strncpy(file_prefix, prefix, sizeof(file_prefix) - 1);
        }

        DIR *d = opendir(dir_path);
        if (d) {
            struct dirent *de;
            size_t fplen = strlen(file_prefix);
            while ((de = readdir(d)) != NULL && match_count < 64) {
                if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
                if (strncmp(de->d_name, file_prefix, fplen) == 0) {
                    char full_check[512];
                    snprintf(full_check, sizeof(full_check), "%s/%s", dir_path, de->d_name);
                    struct stat st;
                    int is_dir = (stat(full_check, &st) == 0 && S_ISDIR(st.st_mode));

                    if (last_slash) {
                        snprintf(matches[match_count++], 255, "%s%s%s%s",
                                 dir_path, (strcmp(dir_path, "/") == 0) ? "" : "/", de->d_name, is_dir ? "/" : "");
                    } else {
                        snprintf(matches[match_count++], 255, "%s%s", de->d_name, is_dir ? "/" : "");
                    }
                }
            }
            closedir(d);
        }
    }

    // Aplica o autocomplete
    if (match_count == 1) {
        size_t m_len = strlen(matches[0]);
        char suffix[256];
        snprintf(suffix, sizeof(suffix), "%s", matches[0] + plen);
        size_t s_len = strlen(suffix);

        if (*len + s_len + 1 < max_len) {
            memmove(buf + cur + s_len, buf + cur, *len - cur + 1);
            memcpy(buf + cur, suffix, s_len);
            *len += s_len;
            *pos += s_len;

            if (matches[0][m_len - 1] != '/') {
                memmove(buf + *pos + 1, buf + *pos, *len - *pos + 1);
                buf[*pos] = ' ';
                (*len)++;
                (*pos)++;
            }
        }
    } else if (match_count > 1) {
        // Exibe opções encontradas
        printf("\n");
        for (int i = 0; i < match_count; i++) {
            printf("  %s%s%s  ", COLOR_VAL, matches[i], COLOR_RESET);
            if ((i + 1) % 4 == 0) printf("\n");
        }
        printf("\n");
        char prompt_buf[512];
        format_prompt(prompt_buf, sizeof(prompt_buf));
        printf("%s%s", prompt_buf, buf);
        fflush(stdout);
    }
}

// =========================================================================
// LEITOR INTERATIVO DE LINHA (Setas, Histórico, Cursor, TAB, Ctrl+L)
// =========================================================================
static int lsh_readline(const char *prompt, char *out_buf, size_t max_len) {
    if (!isatty(STDIN_FILENO)) {
        printf("%s", prompt);
        fflush(stdout);
        if (!fgets(out_buf, max_len, stdin)) return 0;
        size_t l = strlen(out_buf);
        while (l > 0 && (out_buf[l-1] == '\r' || out_buf[l-1] == '\n')) out_buf[--l] = '\0';
        return 1;
    }

    enable_raw_mode();
    printf("%s", prompt);
    fflush(stdout);

    size_t len = 0;
    size_t pos = 0;
    out_buf[0] = '\0';

    int hist_idx = history_count;
    char temp_backup[MAX_LINE_LEN] = "";

    while (1) {
        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) {
            disable_raw_mode();
            return 0;
        }

        // Enter
        if (c == '\r' || c == '\n') {
            printf("\n");
            break;
        }

        // Ctrl + D (Sair se linha vazia)
        if (c == 4) {
            if (len == 0) {
                disable_raw_mode();
                printf("exit\n");
                exit(0);
            }
            continue;
        }

        // Ctrl + C (Cancela linha)
        if (c == 3) {
            printf("^C\n");
            out_buf[0] = '\0';
            len = 0;
            pos = 0;
            char prompt_buf[512];
            format_prompt(prompt_buf, sizeof(prompt_buf));
            printf("%s", prompt_buf);
            fflush(stdout);
            continue;
        }

        // Ctrl + L (Limpar tela)
        if (c == 12) {
            printf("\033[H\033[J%s%s", prompt, out_buf);
            if (pos < len) printf("\033[%zuD", len - pos);
            fflush(stdout);
            continue;
        }

        // TAB Autocomplete
        if (c == '\t') {
            perform_tab_completion(out_buf, &len, &pos, max_len);
            printf("\r\033[K%s%s", prompt, out_buf);
            if (pos < len) printf("\033[%zuD", len - pos);
            fflush(stdout);
            continue;
        }

        // Backspace
        if (c == 127 || c == '\b') {
            if (pos > 0) {
                memmove(out_buf + pos - 1, out_buf + pos, len - pos + 1);
                pos--;
                len--;
                printf("\r\033[K%s%s", prompt, out_buf);
                if (pos < len) printf("\033[%zuD", len - pos);
                fflush(stdout);
            }
            continue;
        }

        // Sequência de Escape (Setas / Home / End)
        if (c == 27) {
            char seq[4];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;

            if (seq[0] == '[') {
                if (seq[1] == 'A') { // Seta CIMA (Histórico Anterior)
                    if (history_count > 0 && hist_idx > 0) {
                        if (hist_idx == history_count) {
                            strncpy(temp_backup, out_buf, sizeof(temp_backup) - 1);
                        }
                        hist_idx--;
                        strncpy(out_buf, history[hist_idx], max_len - 1);
                        len = strlen(out_buf);
                        pos = len;
                        printf("\r\033[K%s%s", prompt, out_buf);
                        fflush(stdout);
                    }
                } else if (seq[1] == 'B') { // Seta BAIXO (Histórico Posterior)
                    if (hist_idx < history_count) {
                        hist_idx++;
                        if (hist_idx == history_count) {
                            strncpy(out_buf, temp_backup, max_len - 1);
                        } else {
                            strncpy(out_buf, history[hist_idx], max_len - 1);
                        }
                        len = strlen(out_buf);
                        pos = len;
                        printf("\r\033[K%s%s", prompt, out_buf);
                        fflush(stdout);
                    }
                } else if (seq[1] == 'C') { // Seta DIREITA
                    if (pos < len) {
                        pos++;
                        printf("\033[1C");
                        fflush(stdout);
                    }
                } else if (seq[1] == 'D') { // Seta ESQUERDA
                    if (pos > 0) {
                        pos--;
                        printf("\033[1D");
                        fflush(stdout);
                    }
                } else if (seq[1] == 'H' || seq[1] == '1') { // Home
                    if (pos > 0) {
                        printf("\033[%zuD", pos);
                        pos = 0;
                        fflush(stdout);
                    }
                } else if (seq[1] == 'F' || seq[1] == '4') { // End
                    if (pos < len) {
                        printf("\033[%zuC", len - pos);
                        pos = len;
                        fflush(stdout);
                    }
                }
            }
            continue;
        }

        // Caracteres comuns
        if (isprint((unsigned char)c) || (unsigned char)c >= 128) {
            if (len < max_len - 1) {
                memmove(out_buf + pos + 1, out_buf + pos, len - pos + 1);
                out_buf[pos] = c;
                pos++;
                len++;
                out_buf[len] = '\0';
                printf("\r\033[K%s%s", prompt, out_buf);
                if (pos < len) printf("\033[%zuD", len - pos);
                fflush(stdout);
            }
        }
    }

    disable_raw_mode();
    return 1;
}

static void expand_variables(const char *in, char *out, size_t out_len) {
    size_t o = 0;
    size_t in_len = strlen(in);

    for (size_t i = 0; i < in_len && o < out_len - 1; i++) {
        if (in[i] == '$' && i + 1 < in_len) {
            i++;
            if (in[i] == '?') {
                char stat_str[16];
                snprintf(stat_str, sizeof(stat_str), "%d", last_exit_status);
                for (size_t k = 0; stat_str[k] && o < out_len - 1; k++) out[o++] = stat_str[k];
                continue;
            }

            char var_name[64];
            size_t v = 0;
            while (i < in_len && (isalnum((unsigned char)in[i]) || in[i] == '_') && v < sizeof(var_name) - 1) {
                var_name[v++] = in[i++];
            }
            var_name[v] = '\0';
            i--;

            const char *val = getenv(var_name);
            if (val) {
                for (size_t k = 0; val[k] && o < out_len - 1; k++) out[o++] = val[k];
            }
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = '\0';
}

static void expand_tilde(const char *in, char *out, size_t out_len) {
    if (in[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) home = ".";
        snprintf(out, out_len, "%s%s", home, in + 1);
    } else {
        strncpy(out, in, out_len - 1);
        out[out_len - 1] = '\0';
    }
}

static void apply_alias(char *cmd_out, const char *cmd_in, size_t out_sz) {
    char first_word[64] = "";
    size_t i = 0;
    while (cmd_in[i] && !isspace((unsigned char)cmd_in[i]) && i < sizeof(first_word) - 1) {
        first_word[i] = cmd_in[i];
        i++;
    }
    first_word[i] = '\0';

    for (int a = 0; a < MAX_ALIASES; a++) {
        if (alias_table[a].active && strcmp(alias_table[a].name, first_word) == 0) {
            snprintf(cmd_out, out_sz, "%s%s", alias_table[a].replacement, cmd_in + i);
            return;
        }
    }
    strncpy(cmd_out, cmd_in, out_sz - 1);
    cmd_out[out_sz - 1] = '\0';
}

static int find_in_path(const char *cmd, char *out_path, size_t max_len) {
    if (strchr(cmd, '/')) {
        if (access(cmd, X_OK) == 0) {
            strncpy(out_path, cmd, max_len - 1);
            out_path[max_len - 1] = '\0';
            return 1;
        }
        return 0;
    }

    const char *path_env = getenv("PATH");
    if (!path_env) path_env = ".:/bin:/usr/bin:/usr/local/bin";

    char path_copy[4096];
    strncpy(path_copy, path_env, sizeof(path_copy) - 1);

    char *dir = strtok(path_copy, ":");
    while (dir) {
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir, cmd);
        if (access(full, X_OK) == 0) {
            strncpy(out_path, full, max_len - 1);
            out_path[max_len - 1] = '\0';
            return 1;
        }
        dir = strtok(NULL, ":");
    }
    return 0;
}

static void process_line(char *line);

static int handle_builtin(char **args) {
    if (!args[0]) return 0;

    if (strcmp(args[0], "exit") == 0 || strcmp(args[0], "quit") == 0) {
        int code = args[1] ? atoi(args[1]) : last_exit_status;
        exit(code);
    }

    if (strcmp(args[0], "clear") == 0 || strcmp(args[0], "cls") == 0) {
        printf("\033[H\033[J");
        fflush(stdout);
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(args[0], "cd") == 0) {
        char target_dir[512];
        char current_cwd[512];
        if (!getcwd(current_cwd, sizeof(current_cwd))) strcpy(current_cwd, ".");

        if (!args[1] || strcmp(args[1], "~") == 0) {
            const char *home = getenv("HOME");
            if (!home) home = ".";
            strncpy(target_dir, home, sizeof(target_dir) - 1);
        } else if (strcmp(args[1], "-") == 0) {
            if (strlen(old_pwd) == 0) {
                fprintf(stderr, "lsh: cd: OLDPWD nao definido\n");
                last_exit_status = 1;
                return 1;
            }
            strncpy(target_dir, old_pwd, sizeof(target_dir) - 1);
            printf("%s\n", target_dir);
        } else {
            expand_tilde(args[1], target_dir, sizeof(target_dir));
        }

        if (chdir(target_dir) == 0) {
            strncpy(old_pwd, current_cwd, sizeof(old_pwd) - 1);
            last_exit_status = 0;
        } else {
            fprintf(stderr, "lsh: cd: %s: %s\n", target_dir, strerror(errno));
            last_exit_status = 1;
        }
        return 1;
    }

    if (strcmp(args[0], "pwd") == 0) {
        char cwd[512];
        if (getcwd(cwd, sizeof(cwd))) printf("%s\n", cwd);
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(args[0], "echo") == 0) {
        int opt_newline = 1;
        int opt_escape = 0;
        int start_idx = 1;

        while (args[start_idx] && args[start_idx][0] == '-') {
            if (strcmp(args[start_idx], "-n") == 0) opt_newline = 0;
            else if (strcmp(args[start_idx], "-e") == 0) opt_escape = 1;
            else break;
            start_idx++;
        }

        for (int i = start_idx; args[i] != NULL; i++) {
            const char *str = args[i];
            if (opt_escape) {
                for (size_t s = 0; str[s]; s++) {
                    if (str[s] == '\\' && str[s+1]) {
                        s++;
                        if (str[s] == 'n') putchar('\n');
                        else if (str[s] == 't') putchar('\t');
                        else if (str[s] == 'e') printf("\033");
                        else if (str[s] == '\\') putchar('\\');
                        else { putchar('\\'); putchar(str[s]); }
                    } else putchar(str[s]);
                }
            } else {
                printf("%s", str);
            }
            if (args[i + 1]) printf(" ");
        }
        if (opt_newline) printf("\n");
        fflush(stdout);
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(args[0], "read") == 0) {
        char prompt_txt[128] = "";
        char *var_name = NULL;

        for (int i = 1; args[i]; i++) {
            if (strcmp(args[i], "-p") == 0 && args[i+1]) {
                strncpy(prompt_txt, args[++i], sizeof(prompt_txt) - 1);
            } else {
                var_name = args[i];
            }
        }

        if (strlen(prompt_txt) > 0) {
            printf("%s", prompt_txt);
            fflush(stdout);
        }

        char input_val[512] = "";
        if (fgets(input_val, sizeof(input_val), stdin)) {
            size_t vl = strlen(input_val);
            while (vl > 0 && (input_val[vl-1] == '\r' || input_val[vl-1] == '\n')) input_val[--vl] = '\0';
            if (var_name) setenv(var_name, input_val, 1);
        }
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(args[0], "source") == 0 || strcmp(args[0], ".") == 0) {
        if (!args[1]) {
            fprintf(stderr, "lsh: informe o arquivo para executar\n");
            last_exit_status = 1;
            return 1;
        }
        FILE *sfp = fopen(args[1], "r");
        if (!sfp) {
            fprintf(stderr, "lsh: source: %s: %s\n", args[1], strerror(errno));
            last_exit_status = 1;
            return 1;
        }
        char sline[MAX_LINE_LEN];
        while (fgets(sline, sizeof(sline), sfp)) {
            size_t sl = strlen(sline);
            while (sl > 0 && (sline[sl-1] == '\r' || sline[sl-1] == '\n')) sline[--sl] = '\0';
            if (sl == 0 || sline[0] == '#') continue;
            process_line(sline);
        }
        fclose(sfp);
        return 1;
    }

    if (strcmp(args[0], "alias") == 0) {
        if (!args[1]) {
            for (int a = 0; a < MAX_ALIASES; a++) {
                if (alias_table[a].active) {
                    printf("alias %s='%s'\n", alias_table[a].name, alias_table[a].replacement);
                }
            }
            last_exit_status = 0;
            return 1;
        }

        char *eq = strchr(args[1], '=');
        if (eq) {
            *eq = '\0';
            const char *val = eq + 1;
            if (*val == '\'' || *val == '"') val++;
            char clean_val[256];
            strncpy(clean_val, val, sizeof(clean_val) - 1);
            size_t cl = strlen(clean_val);
            if (cl > 0 && (clean_val[cl-1] == '\'' || clean_val[cl-1] == '"')) clean_val[--cl] = '\0';

            int slot = -1;
            for (int a = 0; a < MAX_ALIASES; a++) {
                if (alias_table[a].active && strcmp(alias_table[a].name, args[1]) == 0) {
                    slot = a; break;
                }
                if (!alias_table[a].active && slot == -1) slot = a;
            }
            if (slot != -1) {
                strncpy(alias_table[slot].name, args[1], 31);
                strncpy(alias_table[slot].replacement, clean_val, 255);
                alias_table[slot].active = 1;
            }
        }
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(args[0], "unalias") == 0) {
        if (args[1]) {
            for (int a = 0; a < MAX_ALIASES; a++) {
                if (alias_table[a].active && strcmp(alias_table[a].name, args[1]) == 0) {
                    alias_table[a].active = 0;
                }
            }
        }
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(args[0], "export") == 0) {
        if (!args[1]) {
            extern char **environ;
            for (char **env = environ; *env; env++) printf("export %s\n", *env);
            last_exit_status = 0;
            return 1;
        }
        char *eq = strchr(args[1], '=');
        if (eq) {
            *eq = '\0';
            setenv(args[1], eq + 1, 1);
        } else {
            setenv(args[1], "", 1);
        }
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(args[0], "unset") == 0) {
        if (args[1]) unsetenv(args[1]);
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(args[0], "history") == 0) {
        for (int i = 0; i < history_count; i++) printf("  %3d  %s\n", i + 1, history[i]);
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(args[0], "which") == 0 || strcmp(args[0], "type") == 0) {
        if (!args[1]) {
            fprintf(stderr, "which: informe o comando\n");
            last_exit_status = 1;
            return 1;
        }
        char bin_path[1024];
        if (find_in_path(args[1], bin_path, sizeof(bin_path))) {
            printf("%s\n", bin_path);
            last_exit_status = 0;
        } else {
            fprintf(stderr, "lsh: %s nao encontrado em $PATH\n", args[1]);
            last_exit_status = 1;
        }
        return 1;
    }

    if (strcmp(args[0], "exec") == 0 && args[1]) {
        execvp(args[1], args + 1);
        perror("lsh: exec");
        exit(1);
    }

    if (strcmp(args[0], "help") == 0) {
        print_help();
        last_exit_status = 0;
        return 1;
    }

    return 0;
}

static void execute_command_stage(char *cmd_str) {
    char expanded_cmd[MAX_LINE_LEN];
    expand_variables(cmd_str, expanded_cmd, sizeof(expanded_cmd));

    char aliased_cmd[MAX_LINE_LEN];
    apply_alias(aliased_cmd, expanded_cmd, sizeof(aliased_cmd));

    char *args[MAX_ARGS];
    int arg_cnt = 0;
    char *infile = NULL, *outfile = NULL;
    int append_out = 0;

    char *p = aliased_cmd;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (*p == '>') {
            if (*(p + 1) == '>') { append_out = 1; p += 2; }
            else { append_out = 0; p += 1; }
            while (*p == ' ' || *p == '\t') p++;
            outfile = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '<') p++;
            if (*p) *p++ = '\0';
            continue;
        }

        if (*p == '<') {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            infile = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '>') p++;
            if (*p) *p++ = '\0';
            continue;
        }

        if (arg_cnt < MAX_ARGS - 1) {
            args[arg_cnt++] = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '>' && *p != '<') p++;
            if (*p && (*p == ' ' || *p == '\t')) *p++ = '\0';
        } else p++;
    }
    args[arg_cnt] = NULL;

    if (arg_cnt == 0) exit(0);

    if (handle_builtin(args)) exit(last_exit_status);

    if (infile) {
        int fd_in = open(infile, O_RDONLY);
        if (fd_in < 0) {
            fprintf(stderr, "lsh: %s: %s\n", infile, strerror(errno));
            exit(1);
        }
        dup2(fd_in, STDIN_FILENO);
        close(fd_in);
    }

    if (outfile) {
        int flags = O_WRONLY | O_CREAT | (append_out ? O_APPEND : O_TRUNC);
        int fd_out = open(outfile, flags, 0644);
        if (fd_out < 0) {
            fprintf(stderr, "lsh: %s: %s\n", outfile, strerror(errno));
            exit(1);
        }
        dup2(fd_out, STDOUT_FILENO);
        close(fd_out);
    }

    execvp(args[0], args);
    fprintf(stderr, "lsh: comando nao encontrado: '%s'\n", args[0]);
    exit(127);
}

static int execute_pipeline(char *line) {
    char *stages[MAX_PIPES];
    int stage_cnt = 0;

    char *token = strtok(line, "|");
    while (token && stage_cnt < MAX_PIPES) {
        stages[stage_cnt++] = token;
        token = strtok(NULL, "|");
    }

    if (stage_cnt == 0) return 0;

    if (stage_cnt == 1) {
        char expanded[MAX_LINE_LEN];
        expand_variables(stages[0], expanded, sizeof(expanded));

        char aliased[MAX_LINE_LEN];
        apply_alias(aliased, expanded, sizeof(aliased));

        char line_copy[MAX_LINE_LEN];
        strncpy(line_copy, aliased, sizeof(line_copy) - 1);
        char *first_word = strtok(line_copy, " \t");

        int is_time_cmd = 0;
        if (first_word && strcmp(first_word, "time") == 0) {
            is_time_cmd = 1;
            char *next_part = strstr(stages[0], "time");
            if (next_part) stages[0] = next_part + 4;
        }

        if (!is_time_cmd && first_word && (strcmp(first_word, "cd") == 0 || strcmp(first_word, "exit") == 0 ||
                           strcmp(first_word, "quit") == 0 || strcmp(first_word, "help") == 0 ||
                           strcmp(first_word, "export") == 0 || strcmp(first_word, "unset") == 0 ||
                           strcmp(first_word, "history") == 0 || strcmp(first_word, "which") == 0 ||
                           strcmp(first_word, "alias") == 0 || strcmp(first_word, "unalias") == 0 ||
                           strcmp(first_word, "read") == 0 || strcmp(first_word, "source") == 0 ||
                           strcmp(first_word, ".") == 0 || strcmp(first_word, "clear") == 0 ||
                           strcmp(first_word, "cls") == 0 || strcmp(first_word, "echo") == 0)) {
            char *args[MAX_ARGS];
            int ac = 0;
            char *w = strtok(aliased, " \t");
            while (w && ac < MAX_ARGS - 1) {
                args[ac++] = w;
                w = strtok(NULL, " \t");
            }
            args[ac] = NULL;
            handle_builtin(args);
            return last_exit_status;
        }

        double t0 = is_time_cmd ? get_time_sec() : 0;
        pid_t pid = fork();
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);
            execute_command_stage(stages[0]);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            if (is_time_cmd) {
                double elapsed = get_time_sec() - t0;
                printf("\n\033[1;33m[time: %.3f segundos]\033[0m\n", elapsed);
            }
            if (WIFEXITED(status)) last_exit_status = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) last_exit_status = 128 + WTERMSIG(status);
            return last_exit_status;
        }
        return 1;
    }

    int pipefds[2 * (MAX_PIPES - 1)];
    for (int i = 0; i < stage_cnt - 1; i++) {
        if (pipe(pipefds + i * 2) < 0) return 1;
    }

    for (int i = 0; i < stage_cnt; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);

            if (i != 0) dup2(pipefds[(i - 1) * 2], STDIN_FILENO);
            if (i != stage_cnt - 1) dup2(pipefds[i * 2 + 1], STDOUT_FILENO);

            for (int j = 0; j < 2 * (stage_cnt - 1); j++) close(pipefds[j]);

            execute_command_stage(stages[i]);
        }
    }

    for (int j = 0; j < 2 * (stage_cnt - 1); j++) close(pipefds[j]);

    int final_status = 0;
    for (int i = 0; i < stage_cnt; i++) {
        int status;
        wait(&status);
        if (i == stage_cnt - 1 && WIFEXITED(status)) final_status = WEXITSTATUS(status);
    }
    last_exit_status = final_status;
    return last_exit_status;
}

static void process_line(char *line) {
    char *p = line;
    char current_cmd[MAX_LINE_LEN] = "";
    size_t c_idx = 0;
    int next_op = 0;
    int should_run = 1;

    while (*p) {
        if (*p == ';' || (*p == '&' && *(p+1) == '&') || (*p == '|' && *(p+1) == '|')) {
            current_cmd[c_idx] = '\0';

            if (should_run && c_idx > 0) {
                int res = execute_pipeline(current_cmd);
                if (next_op == 1 && res != 0) should_run = 0;
                else if (next_op == 2 && res == 0) should_run = 0;
            }

            c_idx = 0;
            if (*p == ';') {
                next_op = 0;
                should_run = 1;
                p++;
            } else if (*p == '&') {
                next_op = 1;
                should_run = (last_exit_status == 0);
                p += 2;
            } else if (*p == '|') {
                next_op = 2;
                should_run = (last_exit_status != 0);
                p += 2;
            }
            continue;
        }

        current_cmd[c_idx++] = *p++;
    }

    if (c_idx > 0 && should_run) {
        current_cmd[c_idx] = '\0';
        execute_pipeline(current_cmd);
    }
}

int main(int argc, char *argv[]) {
    init_default_env();
    load_history_from_file();

    alias_table[0] = (lsh_alias_t){"ll", "ls -la", 1};
    alias_table[1] = (lsh_alias_t){"cls", "clear", 1};
    alias_table[2] = (lsh_alias_t){"..", "cd ..", 1};

    if (argc >= 2) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(argv[1], "-c") == 0 && argc >= 3) {
            process_line(argv[2]);
            return last_exit_status;
        }
    }

    signal(SIGINT, sigint_handler);

    printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", LOW_COLOR_BORDER, LOW_COLOR_RESET);
    printf("%s│%s  %s[ lsh 3.0 - Intelligent Environment Shell (TAB Autocomplete & History) ]%s %s│%s\n",
           LOW_COLOR_BORDER, LOW_COLOR_RESET, LOW_COLOR_LABEL, LOW_COLOR_RESET, LOW_COLOR_BORDER, LOW_COLOR_RESET);
    printf("%s│%s  • $TMPDIR   : %-58.58s %s│%s\n", LOW_COLOR_BORDER, LOW_COLOR_RESET, getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", LOW_COLOR_BORDER, LOW_COLOR_RESET);
    printf("%s│%s  • $PATH     : Auto-injetado '.' (ferramentas rodam sem './')                 %s│%s\n", LOW_COLOR_BORDER, LOW_COLOR_RESET, LOW_COLOR_BORDER, LOW_COLOR_RESET);
    printf("%s│%s  • Recursos  : TAB (Completar) | ↑/↓ (Histórico) | Ctrl+L (Limpar) | Aliases  %s│%s\n", LOW_COLOR_BORDER, LOW_COLOR_RESET, LOW_COLOR_BORDER, LOW_COLOR_RESET);
    printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n", LOW_COLOR_BORDER, LOW_COLOR_RESET);

    char raw_line[MAX_LINE_LEN];
    char prompt_buf[512];

    while (1) {
        format_prompt(prompt_buf, sizeof(prompt_buf));

        if (!lsh_readline(prompt_buf, raw_line, sizeof(raw_line))) {
            printf("\n");
            break;
        }

        size_t len = strlen(raw_line);
        while (len > 0 && (raw_line[len - 1] == '\n' || raw_line[len - 1] == '\r')) {
            raw_line[--len] = '\0';
        }

        if (len == 0) continue;

        if (strcmp(raw_line, "!!") == 0) {
            if (history_count == 0) {
                fprintf(stderr, "lsh: nenhum comando no historico\n");
                continue;
            }
            strncpy(raw_line, history[history_count - 1], sizeof(raw_line) - 1);
            printf("%s\n", raw_line);
        }

        add_history(raw_line);
        process_line(raw_line);
    }

    return last_exit_status;
}
