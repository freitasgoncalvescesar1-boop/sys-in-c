#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <termios.h>
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_WARN    "\033[1;33m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_USER    "\033[1;32m"
#define COLOR_GROUP   "\033[1;33m"
#define COLOR_PATH    "\033[1;36m"
#define COLOR_TAG     "\033[1;35m"
#define COLOR_VAL     "\033[1;37m"
#define COLOR_MUTED   "\033[0;90m"

static void print_help(void) {
    low_print_banner("whoami");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./whoami [OPTIONS] [PID]\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Process credentials, kernel isolation, memory footprint, TTY geometry, and resource limits.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-uo, --ultra%s          Display Ultra Output (complete system, memory, limits & TTY audit)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-s, --short, --raw%s   Print only effective username (classic GNU whoami output)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-u, --uid%s             Print only effective numeric UID\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-g, --groups%s          Print only supplementary group memberships\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s            Display this formatted help guide and exit\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --version%s         Display version and repository information\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./whoami%s                       (Visao formatada para humanos)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./whoami -uo%s                   (Modo Ultra Output de diagnostico profundo)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./whoami -uo 1234%s              (Ultra Output inspecionando outro PID)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./whoami -s%s                    (Saida curta para scripts Bash)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

static const char *get_user_classification(uid_t uid) {
    if (uid == 0) return "Superuser / Root (Acesso Total ao Kernel)";
    if (uid > 0 && uid < 1000) return "Conta de Servico / Daemon do Sistema";
    if (uid >= 10000 && uid <= 19999) return "Android App Sandbox (UID Isolado)";
    return "Usuario Padrao (Standard User)";
}

static int count_open_fds(pid_t pid) {
    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/fd", pid);
    DIR *d = opendir(path);
    if (!d) return -1;
    int count = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] != '.') count++;
    }
    closedir(d);
    return count;
}

static void get_proc_status_val(pid_t pid, const char *key, char *out, size_t out_sz) {
    strncpy(out, "N/A", out_sz - 1);
    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *fp = fopen(path, "r");
    if (!fp) return;

    char line[256];
    size_t klen = strlen(key);
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, key, klen) == 0 && line[klen] == ':') {
            char *v = line + klen + 1;
            while (*v == ' ' || *v == '\t') v++;
            size_t vlen = strlen(v);
            if (vlen > 0 && (v[vlen - 1] == '\n' || v[vlen - 1] == '\r')) v[--vlen] = '\0';
            snprintf(out, out_sz, "%s", v);
            break;
        }
    }
    fclose(fp);
}

static void print_human_card(void) {
    uid_t ruid = getuid(), euid = geteuid();
    gid_t egid = getegid();

    struct passwd *pw = getpwuid(euid);
    const char *username = pw ? pw->pw_name : getenv("USER");
    if (!username) username = "unknown";

    struct group *gr = getgrgid(egid);
    const char *groupname = gr ? gr->gr_name : "unknown";

    const char *home = pw ? pw->pw_dir : getenv("HOME");
    if (!home) home = "N/A";

    const char *shell = pw ? pw->pw_shell : getenv("SHELL");
    if (!shell) shell = "N/A";

    const char *tty = ttyname(STDIN_FILENO);
    if (!tty) tty = "Nao interativo / Pipe";

    pid_t pid = getpid(), ppid = getppid(), sid = getsid(0), pgid = getpgrp();

    printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", LOW_COLOR_BORDER, LOW_COLOR_RESET);
    printf("%s│%s  %sIDENTIDADE DO PROCESSO ATUAL%s                                            %s│%s\n",
           LOW_COLOR_BORDER, LOW_COLOR_RESET, LOW_COLOR_LABEL, LOW_COLOR_RESET, LOW_COLOR_BORDER, LOW_COLOR_RESET);
    printf("%s├────────────────────────────────────────────────────────────────────────────┤%s\n",
           LOW_COLOR_BORDER, LOW_COLOR_RESET);

    printf("  %s• Usuario Efetivo :%s %s%-20s%s (UID: %-5u)\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, COLOR_USER, username, COLOR_RESET, (unsigned int)euid);
    printf("  %s• Classificacao   :%s %s\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, get_user_classification(euid));

    if (ruid != euid) {
        printf("  %s⚠ PRIVILEGIOS ELEVADOS:%s RUID=%u != EUID=%u (SUID/Sudo Ativo!)\n", COLOR_ERR, COLOR_RESET, (unsigned int)ruid, (unsigned int)euid);
    }

    printf("  %s• Grupo Primario  :%s %s%-20s%s (GID: %-5u)\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, COLOR_GROUP, groupname, COLOR_RESET, (unsigned int)egid);

    int ngroups = getgroups(0, NULL);
    if (ngroups > 0) {
        gid_t *groups = malloc(ngroups * sizeof(gid_t));
        if (groups && getgroups(ngroups, groups) > 0) {
            printf("  %s• Membro dos Grupos:%s ", LOW_COLOR_PROJECT, LOW_COLOR_RESET);
            for (int i = 0; i < ngroups; i++) {
                struct group *g = getgrgid(groups[i]);
                if (g) printf("%s%s(%u)%s ", COLOR_GROUP, g->gr_name, (unsigned int)groups[i], COLOR_RESET);
                else printf("%s%u%s ", COLOR_GROUP, (unsigned int)groups[i], COLOR_RESET);
            }
            printf("\n");
            free(groups);
        }
    }

    printf("  %s• Home Directory :%s %s%s%s\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, COLOR_PATH, home, COLOR_RESET);
    printf("  %s• Shell Padrao   :%s %s\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, shell);
    printf("  %s• Terminal TTY   :%s %s\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, tty);
    printf("  %s• Processo / SID :%s PID: %d | PPID: %d | SID: %d | PGID: %d\n",
           LOW_COLOR_PROJECT, LOW_COLOR_RESET, pid, ppid, sid, pgid);

    printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n", LOW_COLOR_BORDER, LOW_COLOR_RESET);
}

static void print_ultra_output(pid_t target_pid) {
    uid_t ruid, euid, suid, fsuid;
    gid_t rgid, egid, sgid, fsgid;

    if (getresuid(&ruid, &euid, &suid) < 0) {
        ruid = getuid(); euid = geteuid(); suid = euid;
    }
    fsuid = euid;

    if (getresgid(&rgid, &egid, &sgid) < 0) {
        rgid = getgid(); egid = getegid(); sgid = egid;
    }
    fsgid = egid;

    struct passwd *pw = getpwuid(euid);
    const char *username = pw ? pw->pw_name : getenv("USER");
    if (!username) username = "unknown";

    struct group *gr = getgrgid(egid);
    const char *groupname = gr ? gr->gr_name : "unknown";

    const char *home = pw ? pw->pw_dir : getenv("HOME");
    const char *shell = pw ? pw->pw_shell : getenv("SHELL");
    const char *tty = ttyname(STDIN_FILENO);
    if (!tty) tty = "Not a TTY (Pipe / Daemon)";

    struct winsize ws;
    int tty_cols = 80, tty_rows = 24;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        tty_cols = ws.ws_col;
        tty_rows = ws.ws_row;
    }
    pid_t fg_pgid = tcgetpgrp(STDIN_FILENO);

    pid_t pid = target_pid ? target_pid : getpid();
    pid_t ppid = getppid();
    pid_t sid = getsid(pid);
    pid_t pgid = getpgid(pid);

    int open_fds = count_open_fds(pid);

    char vm_size[64], vm_rss[64], vm_peak[64], vm_data[64], vm_stk[64], vm_exe[64];
    char proc_state[64], proc_threads[64], seccomp[64];
    get_proc_status_val(pid, "VmSize", vm_size, sizeof(vm_size));
    get_proc_status_val(pid, "VmRSS", vm_rss, sizeof(vm_rss));
    get_proc_status_val(pid, "VmPeak", vm_peak, sizeof(vm_peak));
    get_proc_status_val(pid, "VmData", vm_data, sizeof(vm_data));
    get_proc_status_val(pid, "VmStk", vm_stk, sizeof(vm_stk));
    get_proc_status_val(pid, "VmExe", vm_exe, sizeof(vm_exe));
    get_proc_status_val(pid, "State", proc_state, sizeof(proc_state));
    get_proc_status_val(pid, "Threads", proc_threads, sizeof(proc_threads));
    get_proc_status_val(pid, "Seccomp", seccomp, sizeof(seccomp));

    struct rlimit r_nofile, r_stack;
    getrlimit(RLIMIT_NOFILE, &r_nofile);
    getrlimit(RLIMIT_STACK, &r_stack);

    printf("\n%s╔════════════════════════════════════════════════════════════════════════════╗%s\n", LOW_COLOR_BORDER, LOW_COLOR_RESET);
    printf("%s║%s  %s[ whoami - ULTRA OUTPUT DIAGNOSTIC & SECURITY AUDIT ]%s                  %s║%s\n",
           LOW_COLOR_BORDER, LOW_COLOR_RESET, LOW_COLOR_LABEL, LOW_COLOR_RESET, LOW_COLOR_BORDER, LOW_COLOR_RESET);
    printf("%s╚════════════════════════════════════════════════════════════════════════════╝%s\n\n", LOW_COLOR_BORDER, LOW_COLOR_RESET);

    printf("%s┌── [1] IDENTIDADE & CREDENCIAIS POSIX ──────────────────────────────────────┐%s\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  %s• Username       :%s %s%s%s (GECOS: %s)\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, COLOR_USER, username, COLOR_RESET, pw ? pw->pw_gecos : "N/A");
    printf("  %s• UIDs (R/E/S/FS):%s Real=%u | Effective=%u | Saved=%u | FS=%u\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, ruid, euid, suid, fsuid);
    printf("  %s• GIDs (R/E/S/FS):%s Real=%u | Effective=%u | Saved=%u | FS=%u\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, rgid, egid, sgid, fsgid);
    printf("  %s• Grupo Primario :%s %s%s%s (GID: %u)\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, COLOR_GROUP, groupname, COLOR_RESET, egid);
    printf("  %s• Classificacao  :%s %s\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, get_user_classification(euid));

    int ngroups = getgroups(0, NULL);
    if (ngroups > 0) {
        gid_t *groups = malloc(ngroups * sizeof(gid_t));
        if (groups && getgroups(ngroups, groups) > 0) {
            printf("  %s• Grupos Suplem. :%s ", LOW_COLOR_PROJECT, LOW_COLOR_RESET);
            for (int i = 0; i < ngroups; i++) {
                struct group *g = getgrgid(groups[i]);
                printf("%s%s(%u)%s ", COLOR_GROUP, g ? g->gr_name : "grp", (unsigned int)groups[i], COLOR_RESET);
            }
            printf("\n");
            free(groups);
        }
    }
    printf("%s└────────────────────────────────────────────────────────────────────────────┘%s\n\n", LOW_COLOR_MUTED, LOW_COLOR_RESET);

    printf("%s┌── [2] CONTEXTO DO PROCESSO (PID: %d) ──────────────────────────────────────┐%s\n", LOW_COLOR_TAG, pid, LOW_COLOR_RESET);
    printf("  %s• Hierarquia PIDs:%s PID: %s%d%s | Pai (PPID): %d | Session (SID): %d | PGID: %d\n",
           LOW_COLOR_PROJECT, LOW_COLOR_RESET, COLOR_VAL, pid, COLOR_RESET, ppid, sid, pgid);
    printf("  %s• Estado & Threads:%s Estado: %s%s%s | Threads Ativas: %s\n",
           LOW_COLOR_PROJECT, LOW_COLOR_RESET, COLOR_OK, proc_state, COLOR_RESET, proc_threads);
    printf("  %s• Prioridade/Nice:%s Prioridade Nice: %d\n",
           LOW_COLOR_PROJECT, LOW_COLOR_RESET, getpriority(PRIO_PROCESS, pid));
    printf("  %s• Arquivos Abertos:%s %s%d File Descriptors (FDs) ativos%s em /proc/%d/fd\n",
           LOW_COLOR_PROJECT, LOW_COLOR_RESET, COLOR_VAL, open_fds, COLOR_RESET, pid);
    printf("%s└────────────────────────────────────────────────────────────────────────────┘%s\n\n", LOW_COLOR_MUTED, LOW_COLOR_RESET);

    printf("%s┌── [3] CONSUMO DE MEMÓRIA VIRTUAL & RAM (VFS /proc) ────────────────────────┐%s\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  %s• RAM Real (RSS) :%s %s%s%s (Memoria fisica alocada)\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, COLOR_OK, vm_rss, COLOR_RESET);
    printf("  %s• Memoria Virtual:%s %s (Pico: %s)\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, vm_size, vm_peak);
    printf("  %s• Segmento Heap  :%s %s (VmData alocado dinamicamente)\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, vm_data);
    printf("  %s• Segmento Stack :%s %s (VmStk pilha de execucao)\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, vm_stk);
    printf("  %s• Codigo Execut. :%s %s (VmExe binario mapeado)\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, vm_exe);
    printf("%s└────────────────────────────────────────────────────────────────────────────┘%s\n\n", LOW_COLOR_MUTED, LOW_COLOR_RESET);

    printf("%s┌── [4] TERMINAL TTY & SUBSISTEMA DE CONTROLE ──────────────────────────────┐%s\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  %s• Dispositivo TTY:%s %s%s%s\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, COLOR_PATH, tty, COLOR_RESET);
    printf("  %s• Geometria Janela:%s %s%d Colunas x %d Linhas%s (%d x %d pixels)\n",
           LOW_COLOR_PROJECT, LOW_COLOR_RESET, COLOR_VAL, tty_cols, tty_rows, COLOR_RESET, ws.ws_xpixel, ws.ws_ypixel);
    printf("  %s• Process Group TTY:%s Foreground PGID: %d %s\n",
           LOW_COLOR_PROJECT, LOW_COLOR_RESET, fg_pgid, (fg_pgid == pgid) ? "[Foreground Ativo]" : "[Background]");
    printf("%s└────────────────────────────────────────────────────────────────────────────┘%s\n\n", LOW_COLOR_MUTED, LOW_COLOR_RESET);

    printf("%s┌── [5] LIMITES DO KERNEL (rlimit) & SEGURANÇA ─────────────────────────────┐%s\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  %s• Limite Max FDs :%s Soft: %lu | Hard: %lu (RLIMIT_NOFILE)\n",
           LOW_COLOR_PROJECT, LOW_COLOR_RESET, (unsigned long)r_nofile.rlim_cur, (unsigned long)r_nofile.rlim_max);
    printf("  %s• Limite Stack   :%s %lu KB (RLIMIT_STACK)\n",
           LOW_COLOR_PROJECT, LOW_COLOR_RESET, (unsigned long)(r_stack.rlim_cur / 1024));
    printf("  %s• Filtro Seccomp :%s Modo: %s (0=Desativado, 2=Filtro BPF Ativo)\n",
           LOW_COLOR_PROJECT, LOW_COLOR_RESET, seccomp);
    printf("  %s• Home Directory :%s %s%s%s\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, COLOR_PATH, home, COLOR_RESET);
    printf("  %s• Shell de Login :%s %s\n", LOW_COLOR_PROJECT, LOW_COLOR_RESET, shell);
    printf("%s└────────────────────────────────────────────────────────────────────────────┘%s\n\n", LOW_COLOR_MUTED, LOW_COLOR_RESET);
}

int main(int argc, char *argv[]) {
    int opt_short = 0, opt_uid = 0, opt_groups = 0, opt_ultra = 0;
    pid_t target_pid = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            print_help();
            return 0;
        }

        if (strcmp(argv[i], "-uo") == 0 || strcmp(argv[i], "--ultra") == 0 || strcmp(argv[i], "--ultra-output") == 0) {
            opt_ultra = 1;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
                target_pid = (pid_t)atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--short") == 0 || strcmp(argv[i], "--raw") == 0) {
            opt_short = 1;
        } else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--uid") == 0) {
            opt_uid = 1;
        } else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--groups") == 0) {
            opt_groups = 1;
        } else if (argv[i][0] >= '0' && argv[i][0] <= '9') {
            target_pid = (pid_t)atoi(argv[i]);
            opt_ultra = 1;
        } else {
            fprintf(stderr, "whoami: opcao desconhecida '%s'\n", argv[i]);
            return 1;
        }
    }

    uid_t euid = geteuid();
    struct passwd *pw = getpwuid(euid);
    const char *username = pw ? pw->pw_name : getenv("USER");
    if (!username) username = "unknown";

    if (opt_uid) {
        printf("%u\n", (unsigned int)euid);
        return 0;
    }

    if (opt_short) {
        printf("%s\n", username);
        return 0;
    }

    if (opt_groups) {
        int ngroups = getgroups(0, NULL);
        if (ngroups > 0) {
            gid_t *groups = malloc(ngroups * sizeof(gid_t));
            if (groups && getgroups(ngroups, groups) > 0) {
                for (int i = 0; i < ngroups; i++) {
                    struct group *g = getgrgid(groups[i]);
                    printf("%s%s", g ? g->gr_name : "unknown", (i < ngroups - 1) ? " " : "\n");
                }
                free(groups);
                return 0;
            }
        }
        printf("%u\n", (unsigned int)getegid());
        return 0;
    }

    if (opt_ultra) {
        print_ultra_output(target_pid);
        return 0;
    }

    print_human_card();
    return 0;
}
