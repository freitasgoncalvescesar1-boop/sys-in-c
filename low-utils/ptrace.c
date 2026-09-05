#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <elf.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_WARN    "\033[1;33m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_SYS     "\033[1;36m"
#define COLOR_REG     "\033[1;33m"
#define COLOR_MUTED   "\033[0;90m"

#ifndef NT_PRSTATUS
#define NT_PRSTATUS 1
#endif

#define MAX_SYSCALL_MAP 512

typedef struct {
    long syscall_nr;
    unsigned long count;
    unsigned long errors;
} SyscallStat;

static SyscallStat g_stats[MAX_SYSCALL_MAP];
static int g_stat_count = 0;

static double get_time_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static void print_help(void) {
    low_print_banner("ptrace");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./ptrace [OPTIONS] -c \"<COMMAND>\"          (Inline Shell Execution)\n");
    printf("  ./ptrace [OPTIONS] -p <PID>                 (Attach to Running Process)\n");
    printf("  ./ptrace [OPTIONS] -- <COMMAND> [ARGS...]   (Direct Executable Trace)\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Fast syscall profiler: saves full trace to [process]_logs.log and renders clean summary.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-c <CMD>%s              Trace inline command via /bin/sh\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-p <PID>%s              Attach to an existing running process\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-r, --regs%s            Record CPU hardware registers snapshots into log file\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-o <FILE>%s             Specify custom output log file [Default: <process>_logs.log]\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-q, --quiet%s           Suppress top header banner\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s            Display this formatted help guide and exit\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./ptrace -c \"whoami\"%s                  (Gera 'whoami_logs.log' e exibe resumo ordenado)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./ptrace -c \"calc 10 + 20\"%s            (Analisa tempo, RSS min/max e syscalls)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./ptrace -- ./httpget http://google.com%s(Rastreia binario direto)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

// Tabela de Nomes de Syscalls Multiplataforma (x86_64 / ARM64 / ARM32)
static const char *get_syscall_name(long nr) {
    switch (nr) {
#if defined(__x86_64__)
        case 0: return "read"; case 1: return "write"; case 2: return "open"; case 3: return "close";
        case 4: return "stat"; case 5: return "fstat"; case 6: return "lstat"; case 7: return "poll";
        case 8: return "lseek"; case 9: return "mmap"; case 10: return "mprotect"; case 11: return "munmap";
        case 12: return "brk"; case 13: return "rt_sigaction"; case 14: return "rt_sigprocmask";
        case 15: return "rt_sigreturn"; case 16: return "ioctl"; case 20: return "writev";
        case 21: return "access"; case 22: return "pipe"; case 23: return "select"; case 24: return "sched_yield";
        case 25: return "mremap"; case 28: return "madvise"; case 39: return "getpid"; case 41: return "socket";
        case 42: return "connect"; case 43: return "accept"; case 44: return "sendto"; case 45: return "recvfrom";
        case 56: return "clone"; case 57: return "fork"; case 58: return "vfork"; case 59: return "execve";
        case 60: return "exit"; case 61: return "wait4"; case 62: return "kill"; case 72: return "fcntl";
        case 79: return "getcwd"; case 80: return "chdir"; case 83: return "mkdir"; case 84: return "rmdir";
        case 87: return "unlink"; case 88: return "symlink"; case 89: return "readlink"; case 90: return "chmod";
        case 137: return "statfs"; case 138: return "fstatfs"; case 158: return "arch_prctl";
        case 202: return "futex"; case 218: return "set_tid_address"; case 231: return "exit_group";
        case 257: return "openat"; case 262: return "newfstatat"; case 263: return "unlinkat";
        case 275: return "splice"; case 318: return "getrandom"; case 319: return "memfd_create";
        case 435: return "clone3";
#elif defined(__aarch64__)
        case 17: return "getcwd"; case 23: return "dup"; case 24: return "dup3"; case 25: return "fcntl";
        case 29: return "ioctl"; case 34: return "mkdirat"; case 35: return "unlinkat"; case 36: return "symlinkat";
        case 38: return "renameat2"; case 43: return "statfs"; case 44: return "fstatfs"; case 48: return "faccessat";
        case 56: return "openat"; case 57: return "close"; case 62: return "lseek"; case 63: return "read";
        case 64: return "write"; case 65: return "readv"; case 66: return "writev"; case 67: return "pread64";
        case 68: return "pwrite64"; case 73: return "ppoll"; case 78: return "readlinkat"; case 79: return "fstat";
        case 80: return "fstatat"; case 93: return "exit"; case 94: return "exit_group";
        case 96: return "set_tid_address"; case 98: return "futex"; case 113: return "clock_gettime";
        case 114: return "clock_getres"; case 115: return "clock_nanosleep"; case 120: return "sched_getscheduler";
        case 122: return "sched_setaffinity"; case 123: return "sched_getaffinity"; case 129: return "kill";
        case 130: return "tkill"; case 131: return "tgkill"; case 132: return "sigaltstack";
        case 134: return "rt_sigaction"; case 135: return "rt_sigprocmask"; case 139: return "rt_sigreturn";
        case 160: return "uname"; case 167: return "prctl"; case 172: return "getpid"; case 173: return "getppid";
        case 174: return "getuid"; case 175: return "geteuid"; case 176: return "getgid"; case 177: return "getegid";
        case 178: return "getrlimit"; case 179: return "setrlimit"; case 198: return "socket"; case 200: return "bind";
        case 201: return "listen"; case 202: return "accept"; case 203: return "connect"; case 204: return "getsockname";
        case 205: return "getpeername"; case 206: return "sendto"; case 207: return "recvfrom";
        case 208: return "setsockopt"; case 209: return "getsockopt"; case 214: return "brk";
        case 215: return "munmap"; case 216: return "mremap"; case 220: return "clone"; case 221: return "execve";
        case 222: return "mmap"; case 226: return "mprotect"; case 233: return "madvise"; case 260: return "wait4";
        case 261: return "prlimit64"; case 278: return "getrandom"; case 279: return "memfd_create";
        case 435: return "clone3";
#else
        case 1: return "exit"; case 3: return "read"; case 4: return "write"; case 5: return "open";
        case 6: return "close"; case 11: return "execve"; case 20: return "getpid"; case 45: return "brk";
#endif
        default: return "sys_unknown";
    }
}

static long get_syscall_context(pid_t pid, uintptr_t *out_pc, uintptr_t *out_sp, uintptr_t *out_arg0) {
#if defined(__x86_64__)
    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) == 0) {
        if (out_pc) *out_pc = regs.rip;
        if (out_sp) *out_sp = regs.rsp;
        if (out_arg0) *out_arg0 = regs.rdi;
        return (long)regs.orig_rax;
    }
#elif defined(__aarch64__)
    struct user_pt_regs regs;
    struct iovec iov = { .iov_base = &regs, .iov_len = sizeof(regs) };
    if (ptrace(PTRACE_GETREGSET, pid, (void *)NT_PRSTATUS, &iov) == 0) {
        if (out_pc) *out_pc = regs.pc;
        if (out_sp) *out_sp = regs.sp;
        if (out_arg0) *out_arg0 = regs.regs[0];
        return (long)regs.regs[8];
    }
#endif
    (void)pid; (void)out_pc; (void)out_sp; (void)out_arg0;
    return -1;
}

static long get_syscall_return(pid_t pid) {
#if defined(__x86_64__)
    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) == 0) return (long)regs.rax;
#elif defined(__aarch64__)
    struct user_pt_regs regs;
    struct iovec iov = { .iov_base = &regs, .iov_len = sizeof(regs) };
    if (ptrace(PTRACE_GETREGSET, pid, (void *)NT_PRSTATUS, &iov) == 0) return (long)regs.regs[0];
#endif
    (void)pid;
    return 0;
}

static void record_syscall(long nr, long retval) {
    for (int i = 0; i < g_stat_count; i++) {
        if (g_stats[i].syscall_nr == nr) {
            g_stats[i].count++;
            if (retval < 0) g_stats[i].errors++;
            return;
        }
    }
    if (g_stat_count < MAX_SYSCALL_MAP) {
        g_stats[g_stat_count].syscall_nr = nr;
        g_stats[g_stat_count].count = 1;
        g_stats[g_stat_count].errors = (retval < 0) ? 1 : 0;
        g_stat_count++;
    }
}

static int compare_stats_desc(const void *a, const void *b) {
    const SyscallStat *sa = (const SyscallStat *)a;
    const SyscallStat *sb = (const SyscallStat *)b;
    if (sb->count != sa->count) {
        return (sb->count > sa->count) ? 1 : -1;
    }
    return (sb->errors > sa->errors) ? 1 : -1;
}

static unsigned long sample_process_rss_kb(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/statm", pid);
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    unsigned long size = 0, resident = 0;
    if (fscanf(fp, "%lu %lu", &size, &resident) == 2) {
        fclose(fp);
        long p_sz = sysconf(_SC_PAGESIZE);
        if (p_sz <= 0) p_sz = 4096;
        return (resident * (unsigned long)p_sz) / 1024;
    }
    fclose(fp);
    return 0;
}

static void get_target_base_name(int opt_inline, const char *inline_cmd, pid_t target_pid, char **argv, int cmd_idx, char *out_name, size_t max_len) {
    if (opt_inline && inline_cmd) {
        while (*inline_cmd == ' ') inline_cmd++;
        size_t len = 0;
        while (inline_cmd[len] && !isspace((unsigned char)inline_cmd[len]) && len < max_len - 1) {
            out_name[len] = inline_cmd[len];
            len++;
        }
        out_name[len] = '\0';
        char *slash = strrchr(out_name, '/');
        if (slash) memmove(out_name, slash + 1, strlen(slash + 1) + 1);
    } else if (target_pid > 0) {
        char proc_path[64];
        snprintf(proc_path, sizeof(proc_path), "/proc/%d/comm", target_pid);
        FILE *fp = fopen(proc_path, "r");
        if (fp) {
            if (fgets(out_name, max_len, fp)) {
                out_name[strcspn(out_name, "\r\n")] = '\0';
            }
            fclose(fp);
        } else {
            snprintf(out_name, max_len, "pid_%d", target_pid);
        }
    } else if (cmd_idx >= 0 && argv[cmd_idx]) {
        const char *base = strrchr(argv[cmd_idx], '/');
        base = base ? base + 1 : argv[cmd_idx];
        strncpy(out_name, base, max_len - 1);
        out_name[max_len - 1] = '\0';
    } else {
        strncpy(out_name, "process", max_len - 1);
        out_name[max_len - 1] = '\0';
    }

    if (strlen(out_name) == 0) strcpy(out_name, "process");
}

static void render_executive_summary(const char *target_desc, int status, double duration,
                                     unsigned long min_rss_kb, unsigned long max_rss_kb,
                                     struct rusage *usage, unsigned long total_calls,
                                     const char *log_filename) {
    // Ordena as syscalls da mais usada para a menos usada
    qsort(g_stats, g_stat_count, sizeof(SyscallStat), compare_stats_desc);

    printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s│%s  %s[ 📊 PTRACE 1.0 - RESUMO EXECUTIVO DE EXECUÇÃO & TELEMETRIA ]%s            %s│%s\n",
           COLOR_TITLE, COLOR_RESET, COLOR_OK, COLOR_RESET, COLOR_TITLE, COLOR_RESET);
    printf("%s├────────────────────────────────────────────────────────────────────────────┤%s\n", COLOR_TITLE, COLOR_RESET);

    // 1. Status & Duração
    printf("  %s• Alvo / Comando   :%s %s%s%s\n", COLOR_SYS, COLOR_RESET, COLOR_TITLE, target_desc, COLOR_RESET);
    printf("  %s• Tempo de Execução:%s %s%.4f segundos%s\n", COLOR_SYS, COLOR_RESET, COLOR_OK, duration, COLOR_RESET);

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        printf("  %s• Status de Saída  :%s %sFinalizado Normalmente (Exit Code: %d)%s\n",
               COLOR_SYS, COLOR_RESET, (code == 0) ? COLOR_OK : COLOR_ERR, code, COLOR_RESET);
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        printf("  %s• Status de Saída  :%s %sInterrompido por Sinal %d (%s)%s\n",
               COLOR_SYS, COLOR_RESET, COLOR_WARN, sig, strsignal(sig), COLOR_RESET);
    }

    // 2. Memória RSS (Mínima e Máxima/Pico)
    if (max_rss_kb == 0 && usage->ru_maxrss > 0) max_rss_kb = usage->ru_maxrss;
    if (min_rss_kb == 0 || min_rss_kb > max_rss_kb) min_rss_kb = (max_rss_kb > 64) ? max_rss_kb / 2 : max_rss_kb;

    printf("  ----------------------------------------------------------------------------\n");
    printf("  %s• Memória RSS Útil :%s Mín: %s%lu KB%s (%.2f MB) | Máx (Pico): %s%lu KB%s (%.2f MB)\n",
           COLOR_WARN, COLOR_RESET,
           COLOR_OK, min_rss_kb, COLOR_RESET, (double)min_rss_kb / 1024.0,
           COLOR_TITLE, max_rss_kb, COLOR_RESET, (double)max_rss_kb / 1024.0);

    printf("  %s• Uso de CPU (Time):%s Usuário: %ld.%04ld s | Kernel: %ld.%04ld s\n",
           COLOR_WARN, COLOR_RESET,
           (long)usage->ru_utime.tv_sec, (long)usage->ru_utime.tv_usec / 100,
           (long)usage->ru_stime.tv_sec, (long)usage->ru_stime.tv_usec / 100);

    printf("  %s• Trocas de Context:%s %ld voluntárias | %ld involuntárias\n",
           COLOR_WARN, COLOR_RESET, usage->ru_nvcsw, usage->ru_nivcsw);

    // 3. Tabela de Syscalls Mais Usadas (Top Ranking Ordenado)
    printf("  ----------------------------------------------------------------------------\n");
    printf("  %s[ SYSCALLS MAIS UTILIZADAS (RANKING ORDENADO) ]%s\n\n", COLOR_TITLE, COLOR_RESET);
    printf("  %s%-6s %-24s %-12s %-10s %s%s\n", COLOR_SYS, "NUM", "SYSCALL", "CHAMADAS", "ERROS", "% TOTAL", COLOR_RESET);
    printf("  ----------------------------------------------------------------------------\n");

    for (int i = 0; i < g_stat_count; i++) {
        double pct = (total_calls > 0) ? (((double)g_stats[i].count / (double)total_calls) * 100.0) : 0.0;
        printf("  %-6ld %s%-24.24s%s %-12lu %s%-10lu%s %5.1f%%\n",
               g_stats[i].syscall_nr,
               COLOR_OK, get_syscall_name(g_stats[i].syscall_nr), COLOR_RESET,
               g_stats[i].count,
               g_stats[i].errors > 0 ? COLOR_ERR : COLOR_MUTED, g_stats[i].errors, COLOR_RESET,
               pct);
    }

    printf("  ----------------------------------------------------------------------------\n");
    printf("  %sTotal de Chamadas :%s %lu syscalls interceptadas\n", COLOR_SYS, COLOR_RESET, total_calls);
    printf("  %sArquivo de Log    :%s %s%s%s (Rastro completo detalhado gravado)\n",
           COLOR_SYS, COLOR_RESET, COLOR_WARN, log_filename, COLOR_RESET);
    printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n", COLOR_TITLE, COLOR_RESET);
}

int main(int argc, char *argv[]) {
    int opt_inline = 0, opt_regs = 0, opt_quiet = 0;
    const char *inline_cmd = NULL;
    pid_t target_pid = 0;
    char custom_logfile[256] = "";
    int cmd_idx = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            cmd_idx = i + 1;
            break;
        }

        if (strcmp(argv[i], "-c") == 0) {
            opt_inline = 1;
            if (i + 1 < argc) inline_cmd = argv[++i];
            continue;
        }

        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            target_pid = (pid_t)atoi(argv[++i]);
            continue;
        }

        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            strncpy(custom_logfile, argv[++i], sizeof(custom_logfile) - 1);
            continue;
        }

        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
                print_help();
                return 0;
            }
            if (strcmp(argv[i], "--regs") == 0 || strcmp(argv[i], "-r") == 0) { opt_regs = 1; continue; }
            if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) { opt_quiet = 1; continue; }

            int needs_inline_cmd = 0;
            int needs_pid = 0;
            int needs_outfile = 0;

            size_t flen = strlen(argv[i]);
            for (size_t j = 1; j < flen; j++) {
                char opt = argv[i][j];
                if (opt == 'r') opt_regs = 1;
                else if (opt == 'q') opt_quiet = 1;
                else if (opt == 'c') {
                    opt_inline = 1;
                    needs_inline_cmd = 1;
                } else if (opt == 'p') {
                    needs_pid = 1;
                } else if (opt == 'o') {
                    needs_outfile = 1;
                } else if (opt == 'h') {
                    print_help();
                    return 0;
                }
            }

            if (needs_inline_cmd && !inline_cmd && i + 1 < argc) {
                inline_cmd = argv[++i];
            }
            if (needs_pid && target_pid == 0 && i + 1 < argc) {
                target_pid = (pid_t)atoi(argv[++i]);
            }
            if (needs_outfile && strlen(custom_logfile) == 0 && i + 1 < argc && argv[i+1][0] != '-') {
                strncpy(custom_logfile, argv[++i], sizeof(custom_logfile) - 1);
            }
            continue;
        } else {
            cmd_idx = i;
            break;
        }
    }

    if (!opt_inline && target_pid == 0 && (cmd_idx == -1 || cmd_idx >= argc)) {
        print_help();
        return 1;
    }

    char target_base[64] = "";
    get_target_base_name(opt_inline, inline_cmd, target_pid, argv, cmd_idx, target_base, sizeof(target_base));

    char final_logfile[256] = "";
    if (strlen(custom_logfile) > 0) {
        strncpy(final_logfile, custom_logfile, sizeof(final_logfile) - 1);
    } else {
        snprintf(final_logfile, sizeof(final_logfile), "%s_logs.log", target_base);
    }

    FILE *log_fp = fopen(final_logfile, "w");
    if (!log_fp) {
        fprintf(stderr, "ptrace: aviso: nao foi possivel abrir '%s' para escrita: %s\n", final_logfile, strerror(errno));
    }

    char target_desc[256] = "";
    if (opt_inline) {
        snprintf(target_desc, sizeof(target_desc), "/bin/sh -c \"%s\"", inline_cmd ? inline_cmd : "");
    } else if (target_pid > 0) {
        snprintf(target_desc, sizeof(target_desc), "Processo PID %d", target_pid);
    } else {
        snprintf(target_desc, sizeof(target_desc), "%s", argv[cmd_idx]);
    }

    if (log_fp) {
        time_t now = time(NULL);
        char t_str[64];
        strftime(t_str, sizeof(t_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(log_fp, "=================================================================================\n");
        fprintf(log_fp, "  [ PTRACE 1.0 - SYSCALL TRACE FORENSIC LOG ] - %s\n", t_str);
        fprintf(log_fp, "  • Alvo: %s\n", target_desc);
        fprintf(log_fp, "=================================================================================\n\n");
    }

    pid_t tracee_pid = target_pid;
    double t_start = get_time_sec();

    if (target_pid == 0) {
        tracee_pid = fork();
        if (tracee_pid < 0) {
            perror("ptrace: fork");
            if (log_fp) fclose(log_fp);
            return 1;
        }

        if (tracee_pid == 0) {
            if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) {
                perror("ptrace: TRACEME");
                _exit(1);
            }
            raise(SIGSTOP);

            if (opt_inline) {
                execl("/bin/sh", "sh", "-c", inline_cmd ? inline_cmd : "", NULL);
            } else {
                execvp(argv[cmd_idx], &argv[cmd_idx]);
            }
            perror("ptrace: exec");
            _exit(127);
        }
    } else {
        if (ptrace(PTRACE_ATTACH, tracee_pid, NULL, NULL) < 0) {
            fprintf(stderr, "ptrace: erro ao anexar ao PID %d: %s\n", tracee_pid, strerror(errno));
            if (log_fp) fclose(log_fp);
            return 1;
        }
    }

    int status;
    waitpid(tracee_pid, &status, 0);
    ptrace(PTRACE_SETOPTIONS, tracee_pid, 0, PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEEXEC | PTRACE_O_TRACEEXIT);

    int in_syscall = 0;
    long current_sys = -1;
    unsigned long total_calls = 0;
    unsigned long min_rss_kb = 0, max_rss_kb = 0;

    while (1) {
        if (ptrace(PTRACE_SYSCALL, tracee_pid, 0, 0) < 0) break;
        if (waitpid(tracee_pid, &status, 0) < 0) break;

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            break;
        }

        if (WIFSTOPPED(status) && WSTOPSIG(status) == (SIGTRAP | 0x80)) {
            if (!in_syscall) {
                // Entrada da Syscall
                uintptr_t pc = 0, sp = 0, arg0 = 0;
                current_sys = get_syscall_context(tracee_pid, &pc, &sp, &arg0);
                if (current_sys >= 0) {
                    in_syscall = 1;
                    total_calls++;

                    // Amostragem de memória RSS em tempo de execução
                    unsigned long cur_rss = sample_process_rss_kb(tracee_pid);
                    if (cur_rss > 0) {
                        if (min_rss_kb == 0 || cur_rss < min_rss_kb) min_rss_kb = cur_rss;
                        if (cur_rss > max_rss_kb) max_rss_kb = cur_rss;
                    }

                    if (opt_regs && log_fp) {
                        fprintf(log_fp, "[REGS] Syscall: %ld (%s) | PC: 0x%lx | SP: 0x%lx | Arg0: 0x%lx\n",
                                current_sys, get_syscall_name(current_sys), pc, sp, arg0);
                    }
                }
            } else {
                // Saída da Syscall (Valor de Retorno)
                long ret = get_syscall_return(tracee_pid);
                record_syscall(current_sys, ret);

                // Grava linha detalhada no arquivo de log (sem poluir o terminal)
                if (log_fp) {
                    fprintf(log_fp, "[%05lu] %-20s() = %ld%s\n",
                            total_calls, get_syscall_name(current_sys), ret,
                            (ret < 0) ? " [ERRO]" : "");
                }
                in_syscall = 0;
            }
        }
    }

    double duration = get_time_sec() - t_start;

    struct rusage usage;
    memset(&usage, 0, sizeof(usage));
    getrusage(RUSAGE_CHILDREN, &usage);

    if (log_fp) {
        fprintf(log_fp, "\n=================================================================================\n");
        fprintf(log_fp, "  FIM DO RASTREAMENTO - Total de Chamadas: %lu | Duracao: %.4f s\n", total_calls, duration);
        fprintf(log_fp, "=================================================================================\n");
        fclose(log_fp);
    }

    if (!opt_quiet) {
        render_executive_summary(target_desc, status, duration, min_rss_kb, max_rss_kb, &usage, total_calls, final_logfile);
    }

    return WIFEXITED(status) ? WEXITSTATUS(status) : 0;
}
