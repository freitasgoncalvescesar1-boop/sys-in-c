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
    printf("  Low-level Linux/POSIX process tracer, syscall decoder, and CPU register inspector.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-c <CMD>%s              Trace inline command via /bin/sh child process\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-p <PID>%s              Attach to an existing running process\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-s, --summary%s         Print summary table of syscall calls and counts\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --verbose%s         Print live trace stream of every syscall entry and exit\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-r, --regs%s            Dump CPU hardware registers context snapshots\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-o <FILE>%s             Dump trace log output to file\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-q, --quiet%s           Do not print startup banner\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s            Display this formatted help guide and exit\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sCOMBINED SHORT FLAGS EXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./ptrace -cs \"calc 10 + 20\"%s           (Inline + Tabela de Resumo)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./ptrace -cvr \"ls -la\"%s                 (Inline + Verbose + Registradores CPU)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./ptrace -s -- ./httpget http://google.com%s (Rastreia binario direto)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

// Tabela de Nomes de Syscalls Multiplataforma (x86_64 / ARM64 / ARM32)
static const char *get_syscall_name(long nr) {
    switch (nr) {
#if defined(__x86_64__)
        case 0: return "read"; case 1: return "write"; case 2: return "open"; case 3: return "close";
        case 4: return "stat"; case 5: return "fstat"; case 6: return "lstat"; case 7: return "poll";
        case 8: return "lseek"; case 9: return "mmap"; case 10: return "mprotect"; case 11: return "munmap";
        case 12: return "brk"; case 13: return "rt_sigaction"; case 14: return "rt_sigprocmask";
        case 16: return "ioctl"; case 20: return "writev"; case 21: return "access"; case 22: return "pipe";
        case 23: return "select"; case 39: return "getpid"; case 41: return "socket"; case 42: return "connect";
        case 43: return "accept"; case 44: return "sendto"; case 45: return "recvfrom"; case 56: return "clone";
        case 57: return "fork"; case 58: return "vfork"; case 59: return "execve"; case 60: return "exit";
        case 61: return "wait4"; case 62: return "kill"; case 72: return "fcntl"; case 79: return "getcwd";
        case 80: return "chdir"; case 83: return "mkdir"; case 84: return "rmdir"; case 87: return "unlink";
        case 88: return "symlink"; case 89: return "readlink"; case 90: return "chmod"; case 231: return "exit_group";
        case 257: return "openat"; case 262: return "newfstatat"; case 263: return "unlinkat"; case 435: return "clone3";
#elif defined(__aarch64__)
        case 17: return "getcwd"; case 23: return "dup"; case 24: return "dup3"; case 25: return "fcntl";
        case 29: return "ioctl"; case 34: return "mkdirat"; case 35: return "unlinkat"; case 36: return "symlinkat";
        case 38: return "renameat2"; case 48: return "faccessat"; case 56: return "openat"; case 57: return "close";
        case 62: return "lseek"; case 63: return "read"; case 64: return "write"; case 65: return "readv";
        case 66: return "writev"; case 73: return "ppoll"; case 79: return "fstat"; case 80: return "fstatat";
        case 93: return "exit"; case 94: return "exit_group"; case 96: return "set_tid_address"; case 98: return "futex";
        case 129: return "kill"; case 130: return "tkill"; case 131: return "tgkill"; case 134: return "rt_sigaction";
        case 135: return "rt_sigprocmask"; case 160: return "uname"; case 172: return "getpid"; case 173: return "getppid";
        case 174: return "getuid"; case 175: return "geteuid"; case 198: return "socket"; case 200: return "bind";
        case 201: return "listen"; case 202: return "accept"; case 203: return "connect"; case 206: return "sendto";
        case 207: return "recvfrom"; case 220: return "clone"; case 221: return "execve"; case 222: return "mmap";
        case 226: return "mprotect"; case 260: return "wait4"; case 261: return "prlimit64"; case 435: return "clone3";
#else
        case 1: return "exit"; case 3: return "read"; case 4: return "write"; case 5: return "open";
        case 6: return "close"; case 11: return "execve"; case 20: return "getpid"; case 45: return "brk";
#endif
        default: return "sys_unknown";
    }
}

// Extrai número da syscall e contexto de registradores
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
        return (long)regs.regs[8]; // Em ARM64 o número da Syscall fica no registrador X8
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

static void print_summary_table(double duration, unsigned long total_calls) {
    printf("\n%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ ptrace - Relatório Resumo de Syscalls (Histograma) ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n\n", COLOR_TITLE, COLOR_RESET);
    printf("  %s%-6s %-24s %-12s %-10s %s%s\n", COLOR_SYS, "NUM", "SYSCALL", "CHAMADAS", "ERROS", "% TOTAL", COLOR_RESET);
    printf("  ---------------------------------------------------------------------------------\n");

    for (int i = 0; i < g_stat_count; i++) {
        double pct = (total_calls > 0) ? (((double)g_stats[i].count / (double)total_calls) * 100.0) : 0.0;
        printf("  %-6ld %s%-24.24s%s %-12lu %s%-10lu%s %5.1f%%\n",
               g_stats[i].syscall_nr,
               COLOR_OK, get_syscall_name(g_stats[i].syscall_nr), COLOR_RESET,
               g_stats[i].count,
               g_stats[i].errors > 0 ? COLOR_ERR : COLOR_MUTED, g_stats[i].errors, COLOR_RESET,
               pct);
    }
    printf("  ---------------------------------------------------------------------------------\n");
    printf("  %sTotal de Chamadas:%s %lu | %sTempo de Rastreamento:%s %.4f segundos\n",
           COLOR_TITLE, COLOR_RESET, total_calls, COLOR_TITLE, COLOR_RESET, duration);
    printf("%s=================================================================================%s\n\n", COLOR_TITLE, COLOR_RESET);
}

static void dump_registers_snapshot(pid_t pid, long sys_nr) {
    uintptr_t pc = 0, sp = 0, arg0 = 0;
    get_syscall_context(pid, &pc, &sp, &arg0);

    printf("  %s[REGISTERS SNAPSHOT]%s Syscall: %s%ld (%s)%s | PC/RIP: %s0x%lx%s | SP: %s0x%lx%s | Arg0: 0x%lx\n",
           COLOR_REG, COLOR_RESET,
           COLOR_OK, sys_nr, get_syscall_name(sys_nr), COLOR_RESET,
           COLOR_SYS, pc, COLOR_RESET,
           COLOR_SYS, sp, COLOR_RESET,
           arg0);
}

int main(int argc, char *argv[]) {
    int opt_inline = 0, opt_summary = 0, opt_verbose = 0, opt_regs = 0, opt_quiet = 0;
    const char *inline_cmd = NULL;
    pid_t target_pid = 0;
    char *out_logfile = NULL;
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
            out_logfile = argv[++i];
            continue;
        }

        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
                print_help();
                return 0;
            }
            if (strcmp(argv[i], "--summary") == 0) { opt_summary = 1; continue; }
            if (strcmp(argv[i], "--verbose") == 0) { opt_verbose = 1; continue; }
            if (strcmp(argv[i], "--regs") == 0) { opt_regs = 1; continue; }
            if (strcmp(argv[i], "--quiet") == 0) { opt_quiet = 1; continue; }

            // Flags Curtas Combinadas (ex: -cs, -cv, -cvr, -sv, etc.)
            size_t flen = strlen(argv[i]);
            for (size_t j = 1; j < flen; j++) {
                char opt = argv[i][j];
                if (opt == 's') opt_summary = 1;
                else if (opt == 'v') opt_verbose = 1;
                else if (opt == 'r') opt_regs = 1;
                else if (opt == 'q') opt_quiet = 1;
                else if (opt == 'c') {
                    opt_inline = 1;
                    if (j + 1 == flen && i + 1 < argc) inline_cmd = argv[++i];
                    break;
                } else if (opt == 'p') {
                    if (i + 1 < argc) target_pid = (pid_t)atoi(argv[++i]);
                    break;
                } else if (opt == 'h') {
                    print_help();
                    return 0;
                }
            }
        } else {
            cmd_idx = i;
            break;
        }
    }

    if (!opt_inline && target_pid == 0 && (cmd_idx == -1 || cmd_idx >= argc)) {
        print_help();
        return 1;
    }

    if (!opt_verbose && !opt_summary && !opt_regs) {
        opt_summary = 1; // Modo padrão: Relatório Resumo
    }

    FILE *log_fp = out_logfile ? fopen(out_logfile, "w") : NULL;

    if (!opt_quiet) {
        printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", COLOR_TITLE, COLOR_RESET);
        printf("%s│%s  %s[ 🕵️ ptrace 1.0 - Monitor & Tracer de Syscalls e Registradores ]%s        %s│%s\n",
               COLOR_TITLE, COLOR_RESET, COLOR_OK, COLOR_RESET, COLOR_TITLE, COLOR_RESET);
        if (opt_inline) {
            printf("%s│%s  • Modo Inline   : /bin/sh -c \"%s%-43.43s%s\" %s│%s\n",
                   COLOR_TITLE, COLOR_RESET, COLOR_SYS, inline_cmd, COLOR_RESET, COLOR_TITLE, COLOR_RESET);
        } else if (target_pid > 0) {
            printf("%s│%s  • Anexando ao PID: %s%-55d%s %s│%s\n",
                   COLOR_TITLE, COLOR_RESET, COLOR_SYS, target_pid, COLOR_RESET, COLOR_TITLE, COLOR_RESET);
        } else {
            printf("%s│%s  • Executavel Alvo: %s%-55.55s%s %s│%s\n",
                   COLOR_TITLE, COLOR_RESET, COLOR_SYS, argv[cmd_idx], COLOR_RESET, COLOR_TITLE, COLOR_RESET);
        }
        printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n", COLOR_TITLE, COLOR_RESET);
    }

    pid_t tracee_pid = target_pid;
    double t_start = get_time_sec();

    if (target_pid == 0) {
        tracee_pid = fork();
        if (tracee_pid < 0) {
            perror("ptrace: fork");
            return 1;
        }

        if (tracee_pid == 0) {
            // Processo Filho: solicita ser rastreado
            if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) {
                perror("ptrace: TRACEME");
                _exit(1);
            }
            raise(SIGSTOP); // Pausa para o pai configurar as opções

            if (opt_inline) {
                execl("/bin/sh", "sh", "-c", inline_cmd, NULL);
            } else {
                execvp(argv[cmd_idx], &argv[cmd_idx]);
            }
            perror("ptrace: exec");
            _exit(127);
        }
    } else {
        if (ptrace(PTRACE_ATTACH, tracee_pid, NULL, NULL) < 0) {
            fprintf(stderr, "ptrace: erro ao anexar ao PID %d: %s\n", tracee_pid, strerror(errno));
            return 1;
        }
    }

    int status;
    waitpid(tracee_pid, &status, 0);

    // Configura opções do ptrace no pai
    ptrace(PTRACE_SETOPTIONS, tracee_pid, 0, PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEEXEC | PTRACE_O_TRACEEXIT);

    int in_syscall = 0;
    long current_sys = -1;
    unsigned long total_calls = 0;

    while (1) {
        if (ptrace(PTRACE_SYSCALL, tracee_pid, 0, 0) < 0) break;
        if (waitpid(tracee_pid, &status, 0) < 0) break;

        if (WIFEXITED(status)) {
            if (opt_verbose) printf("+++ Processo saiu com codigo %d +++\n", WEXITSTATUS(status));
            break;
        }
        if (WIFSIGNALED(status)) {
            if (opt_verbose) printf("+++ Processo finalizado pelo sinal %d (%s) +++\n", WTERMSIG(status), strsignal(WTERMSIG(status)));
            break;
        }

        // Interceptação de Chamada de Sistema
        if (WIFSTOPPED(status) && WSTOPSIG(status) == (SIGTRAP | 0x80)) {
            if (!in_syscall) {
                // Entrada da Syscall
                current_sys = get_syscall_context(tracee_pid, NULL, NULL, NULL);
                if (current_sys >= 0) {
                    in_syscall = 1;
                    total_calls++;
                    if (opt_regs) dump_registers_snapshot(tracee_pid, current_sys);
                }
            } else {
                // Saída da Syscall (Valor de Retorno)
                long ret = get_syscall_return(tracee_pid);
                record_syscall(current_sys, ret);

                if (opt_verbose) {
                    printf("  %s%-18s%s() = %s%ld%s\n",
                           COLOR_SYS, get_syscall_name(current_sys), COLOR_RESET,
                           (ret < 0) ? COLOR_ERR : COLOR_OK, ret, COLOR_RESET);
                    if (log_fp) fprintf(log_fp, "%s() = %ld\n", get_syscall_name(current_sys), ret);
                }
                in_syscall = 0;
            }
        }
    }

    double duration = get_time_sec() - t_start;

    if (opt_summary) {
        print_summary_table(duration, total_calls);
    }

    if (log_fp) fclose(log_fp);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 0;
}
