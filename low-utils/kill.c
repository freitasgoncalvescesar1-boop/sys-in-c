#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET "\033[0m"
#define COLOR_OK    "\033[1;32m"
#define COLOR_ERR   "\033[1;31m"
#define COLOR_SIG   "\033[1;35m"
#define COLOR_PID   "\033[1;33m"

typedef struct {
    const char *name;
    int signum;
    const char *desc;
} SigMap;

static const SigMap sig_table[] = {
    {"HUP",   SIGHUP,   "Hangup / Reiniciar configuracoes"},
    {"INT",   SIGINT,   "Interrupt (Ctrl+C)"},
    {"QUIT",  SIGQUIT,  "Quit (Ctrl+\\)"},
    {"KILL",  SIGKILL,  "Forcar Parada Imediata (Incondicional)"},
    {"TERM",  SIGTERM,  "Finalizacao Graciosa (Padrao)"},
    {"STOP",  SIGSTOP,  "Pausar/Congelar Processo"},
    {"CONT",  SIGCONT,  "Despausar/Continuar Processo"},
    {"USR1",  SIGUSR1,  "Sinal Definido pelo Usuario 1"},
    {"USR2",  SIGUSR2,  "Sinal Definido pelo Usuario 2"},
    {"SEGV",  SIGSEGV,  "Segmentacao de Memoria Invalida"},
    {"ALRM",  SIGALRM,  "Temporizador de Alarme"},
    {NULL, 0, NULL}
};

static void print_help(void) {
    low_print_banner("kill");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./kill [SIGNAL] <PID...>\n");
    printf("  ./kill -n <PROCESS_NAME> [SIGNAL]\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-s <SIG>, -<SIG>%s     Specify signal by name or number (e.g. -9, -KILL, -STOP)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-n, --name <NAME>%s    Pkill mode: signal all processes matching NAME\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-l, --list%s           List all available POSIX signals\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s           Display this formatted help guide and exit\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --version%s        Display version and repository information\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./kill 1234%s                    (Envia SIGTERM / Finalizacao padrao)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./kill -9 1234%s                 (Envia SIGKILL / Forca parada)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./kill -STOP 1234%s              (Congela o processo)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./kill -CONT 1234%s              (Descongela o processo)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./kill -n dummy_target -9%s      (Finaliza todos os processos pelo nome)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

static void list_signals(void) {
    printf("\n%s  %-4s %-8s %s%s\n", LOW_COLOR_LABEL, "NUM", "SINAL", "DESCRICAO", COLOR_RESET);
    printf("  --------------------------------------------------------\n");
    for (int i = 0; sig_table[i].name != NULL; i++) {
        printf("  %-4d %sSIG%-6s%s %s\n",
               sig_table[i].signum, COLOR_SIG, sig_table[i].name, COLOR_RESET, sig_table[i].desc);
    }
    printf("\n");
}

static int parse_signal(const char *str) {
    if (str[0] == '-') str++;
    if (strncasecmp(str, "sig", 3) == 0) str += 3;

    if (isdigit(str[0])) {
        return atoi(str);
    }
    for (int i = 0; sig_table[i].name != NULL; i++) {
        if (strcasecmp(str, sig_table[i].name) == 0) {
            return sig_table[i].signum;
        }
    }
    return -1;
}

static const char *get_sig_name(int signum) {
    for (int i = 0; sig_table[i].name != NULL; i++) {
        if (sig_table[i].signum == signum) return sig_table[i].name;
    }
    return "UNKNOWN";
}

static int kill_by_name(const char *name, int signum) {
    DIR *dir = opendir("/proc");
    if (!dir) return -1;

    struct dirent *de;
    int found = 0;

    while ((de = readdir(dir)) != NULL) {
        if (!isdigit(de->d_name[0])) continue;
        pid_t pid = atoi(de->d_name);
        if (pid == getpid()) continue;

        char path[128], comm[128] = "";
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        FILE *fp = fopen(path, "r");
        if (fp) {
            if (fscanf(fp, "%*d (%127[^)])", comm) == 1) {
                if (strcasecmp(comm, name) == 0) {
                    if (kill(pid, signum) == 0) {
                        printf("  %s[OK]%s Sinal %sSIG%s (%d)%s enviado para PID %s%d%s (%s)\n",
                               COLOR_OK, COLOR_RESET, COLOR_SIG, get_sig_name(signum), signum, COLOR_RESET,
                               COLOR_PID, pid, COLOR_RESET, comm);
                        found++;
                    } else {
                        fprintf(stderr, "  %s[ERRO]%s PID %d: %s\n", COLOR_ERR, COLOR_RESET, pid, strerror(errno));
                    }
                }
            }
            fclose(fp);
        }
    }
    closedir(dir);
    if (!found) printf("  Nenhum processo encontrado com nome '%s'\n", name);
    return found;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 1;
    }

    int signum = SIGTERM;
    const char *target_name = NULL;
    pid_t pids[256];
    int pid_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            print_help();
            return 0;
        }

        if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            list_signals();
            return 0;
        }

        if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--name") == 0) {
            if (i + 1 < argc) target_name = argv[++i];
        } else if (argv[i][0] == '-' && isdigit(argv[i][1])) {
            signum = atoi(argv[i] + 1);
        } else if (argv[i][0] == '-' && isalpha(argv[i][1])) {
            int s = parse_signal(argv[i]);
            if (s > 0) signum = s;
        } else if (isdigit(argv[i][0])) {
            if (pid_count < 256) pids[pid_count++] = atoi(argv[i]);
        }
    }

    if (target_name) {
        kill_by_name(target_name, signum);
        return 0;
    }

    if (pid_count == 0) {
        print_help();
        return 1;
    }

    int has_err = 0;
    for (int i = 0; i < pid_count; i++) {
        if (kill(pids[i], signum) == 0) {
            printf("  %s[OK]%s Sinal %sSIG%s (%d)%s enviado para PID %s%d%s\n",
                   COLOR_OK, COLOR_RESET, COLOR_SIG, get_sig_name(signum), signum, COLOR_RESET,
                   COLOR_PID, pids[i], COLOR_RESET);
        } else {
            fprintf(stderr, "  %s[ERRO]%s PID %d: %s\n", COLOR_ERR, COLOR_RESET, pids[i], strerror(errno));
            has_err = 1;
        }
    }

    return has_err ? 1 : 0;
}
