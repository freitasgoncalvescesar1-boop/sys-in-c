#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET "\033[0m"
#define COLOR_OK    "\033[1;32m"
#define COLOR_ERR   "\033[1;31m"
#define COLOR_WARN  "\033[1;33m"
#define COLOR_SEC   "\033[1;35m"
#define COLOR_TITLE "\033[1;35m"
#define COLOR_CMD   "\033[1;36m"
#define COLOR_NERD  "\033[1;33m"

#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS 38
#endif

#ifndef SECCOMP_MODE_FILTER
#define SECCOMP_MODE_FILTER 2
#endif

#ifndef SECCOMP_RET_KILL_PROCESS
#define SECCOMP_RET_KILL_PROCESS 0x80000000U
#endif
#ifndef SECCOMP_RET_ERRNO
#define SECCOMP_RET_ERRNO 0x00050000U
#endif
#ifndef SECCOMP_RET_DATA
#define SECCOMP_RET_DATA 0x0000ffffU
#endif
#ifndef SECCOMP_RET_ALLOW
#define SECCOMP_RET_ALLOW 0x7fff0000U
#endif

#ifndef AUDIT_ARCH_X86_64
#define AUDIT_ARCH_X86_64 0xc000003e
#endif
#ifndef AUDIT_ARCH_AARCH64
#define AUDIT_ARCH_AARCH64 0xc00000b7
#endif
#ifndef AUDIT_ARCH_I386
#define AUDIT_ARCH_I386 0x40000003
#endif
#ifndef AUDIT_ARCH_ARM
#define AUDIT_ARCH_ARM 0x40000028
#endif
#ifndef AUDIT_ARCH_RISCV64
#define AUDIT_ARCH_RISCV64 0xc00000f3
#endif

static pid_t g_child_pid = 0;
static volatile sig_atomic_t g_timeout_triggered = 0;

static void alarm_handler(int sig) {
    (void)sig;
    g_timeout_triggered = 1;
    if (g_child_pid > 0) {
        kill(g_child_pid, SIGKILL);
    }
}

static double get_time_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static void print_help(void) {
    low_print_banner("jail");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./jail [OPTIONS] -- <COMMAND> [ARGS...]\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Permissive by default Seccomp-BPF sandbox, Process Monitor (-om) & Nerd Mode (-nd).\n\n");
    printf("%sOPTIONS (ALL ALLOWED BY DEFAULT):%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-n, --no-net%s           Block all network syscalls (socket, connect, send, recv)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-w, --no-write%s         Block file modification/deletion (unlink, chmod, truncate)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-e, --no-exec%s          Block subprocess creation (fork, vfork, clone, clone3, execve)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-x, --no-wx%s            Block fileless memory execution (memfd_create, userfaultfd)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-i, --no-ipc%s           Block SysV IPC & shared memory (shmget, shmat, semop)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-k, --no-signal%s        Block signal injection & kill tampering (kill, tkill)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-s, --strict%s           Maximum lockdown (All block flags active simultaneously)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-om, --only-monitor [F]%s Dump execution metrics & security audit to file [Default: jail_audit.log]\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-nd, --nerd%s            Enable Nerd Mode: inspect CPU registers & scheduler telemetry\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-t, --timeout <SECS>%s   Kill process if execution exceeds SECS\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-m, --mem <MB>%s         Virtual memory ceiling in Megabytes (RLIMIT_AS)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-f, --max-file <MB>%s    Limit maximum file size generated on disk (RLIMIT_FSIZE)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-d, --drop <UID>%s       Drop process UID/GID credentials before execution\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-q, --quiet%s            Run without status banner\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s             Display this help guide and exit\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./jail -om -- ./httpget http://google.com%s     (Executa com rede/disco livres e gera log)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./jail -n -- ./httpget http://google.com%s      (Bloqueia apenas a rede com EPERM)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./jail -nd -- ./calc 10 + 20%s                  (Modo Nerd com telemetria sem restrições)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

static int setup_seccomp_jail(int no_net, int no_write, int no_exec, int no_wx, int no_ipc, int no_sig, int strict) {
    // Se nenhuma flag de bloqueio foi passada, não instala filtros restritivos
    if (!no_net && !no_write && !no_exec && !no_wx && !no_ipc && !no_sig && !strict) {
        return 0;
    }

    uint32_t arch = 0;
#if defined(__x86_64__)
    arch = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
    arch = AUDIT_ARCH_AARCH64;
#elif defined(__i386__)
    arch = AUDIT_ARCH_I386;
#elif defined(__arm__)
    arch = AUDIT_ARCH_ARM;
#elif defined(__riscv)
    arch = AUDIT_ARCH_RISCV64;
#endif

    struct sock_filter filter[512];
    int pc = 0;

    if (arch != 0) {
        filter[pc++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (offsetof(struct seccomp_data, arch)));
        filter[pc++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, arch, 1, 0);
        filter[pc++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
    }

    filter[pc++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (offsetof(struct seccomp_data, nr)));

    #define BAN_SYSCALL(sys_nr) do { \
        filter[pc++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (sys_nr), 0, 1); \
        filter[pc++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)); \
    } while (0)

    // Bloqueio de Rede (-n)
    if (no_net || strict) {
#ifdef __NR_socket
        BAN_SYSCALL(__NR_socket);
#endif
#ifdef __NR_connect
        BAN_SYSCALL(__NR_connect);
#endif
#ifdef __NR_bind
        BAN_SYSCALL(__NR_bind);
#endif
#ifdef __NR_listen
        BAN_SYSCALL(__NR_listen);
#endif
#ifdef __NR_accept
        BAN_SYSCALL(__NR_accept);
#endif
#ifdef __NR_accept4
        BAN_SYSCALL(__NR_accept4);
#endif
#ifdef __NR_sendto
        BAN_SYSCALL(__NR_sendto);
#endif
#ifdef __NR_recvfrom
        BAN_SYSCALL(__NR_recvfrom);
#endif
#ifdef __NR_sendmsg
        BAN_SYSCALL(__NR_sendmsg);
#endif
#ifdef __NR_recvmsg
        BAN_SYSCALL(__NR_recvmsg);
#endif
#ifdef __NR_socketcall
        BAN_SYSCALL(__NR_socketcall);
#endif
    }

    // Bloqueio de Modificação de Disco (-w)
    if (no_write || strict) {
#ifdef __NR_unlink
        BAN_SYSCALL(__NR_unlink);
#endif
#ifdef __NR_unlinkat
        BAN_SYSCALL(__NR_unlinkat);
#endif
#ifdef __NR_rmdir
        BAN_SYSCALL(__NR_rmdir);
#endif
#ifdef __NR_rename
        BAN_SYSCALL(__NR_rename);
#endif
#ifdef __NR_renameat
        BAN_SYSCALL(__NR_renameat);
#endif
#ifdef __NR_renameat2
        BAN_SYSCALL(__NR_renameat2);
#endif
#ifdef __NR_chmod
        BAN_SYSCALL(__NR_chmod);
#endif
#ifdef __NR_fchmod
        BAN_SYSCALL(__NR_fchmod);
#endif
#ifdef __NR_fchmodat
        BAN_SYSCALL(__NR_fchmodat);
#endif
#ifdef __NR_truncate
        BAN_SYSCALL(__NR_truncate);
#endif
#ifdef __NR_ftruncate
        BAN_SYSCALL(__NR_ftruncate);
#endif
    }

    // Bloqueio de Subprocessos (-e)
    if (no_exec || strict) {
#ifdef __NR_clone
        BAN_SYSCALL(__NR_clone);
#endif
#ifdef __NR_clone3
        BAN_SYSCALL(__NR_clone3);
#endif
#ifdef __NR_fork
        BAN_SYSCALL(__NR_fork);
#endif
#ifdef __NR_vfork
        BAN_SYSCALL(__NR_vfork);
#endif
#ifdef __NR_execveat
        BAN_SYSCALL(__NR_execveat);
#endif
    }

    // Anti-Shellcode em RAM (-x)
    if (no_wx || strict) {
#ifdef __NR_memfd_create
        BAN_SYSCALL(__NR_memfd_create);
#endif
#ifdef __NR_userfaultfd
        BAN_SYSCALL(__NR_userfaultfd);
#endif
#ifdef __NR_process_vm_writev
        BAN_SYSCALL(__NR_process_vm_writev);
#endif
    }

    // Bloqueio de IPC (-i)
    if (no_ipc || strict) {
#ifdef __NR_shmget
        BAN_SYSCALL(__NR_shmget);
#endif
#ifdef __NR_shmat
        BAN_SYSCALL(__NR_shmat);
#endif
#ifdef __NR_shmdt
        BAN_SYSCALL(__NR_shmdt);
#endif
#ifdef __NR_shmctl
        BAN_SYSCALL(__NR_shmctl);
#endif
#ifdef __NR_semget
        BAN_SYSCALL(__NR_semget);
#endif
#ifdef __NR_semop
        BAN_SYSCALL(__NR_semop);
#endif
#ifdef __NR_msgget
        BAN_SYSCALL(__NR_msgget);
#endif
#ifdef __NR_msgsnd
        BAN_SYSCALL(__NR_msgsnd);
#endif
#ifdef __NR_msgrcv
        BAN_SYSCALL(__NR_msgrcv);
#endif
    }

    // Bloqueio de Sinais (-k)
    if (no_sig || strict) {
#ifdef __NR_kill
        BAN_SYSCALL(__NR_kill);
#endif
#ifdef __NR_tkill
        BAN_SYSCALL(__NR_tkill);
#endif
#ifdef __NR_tgkill
        BAN_SYSCALL(__NR_tgkill);
#endif
#ifdef __NR_pidfd_send_signal
        BAN_SYSCALL(__NR_pidfd_send_signal);
#endif
    }

    if (strict) {
#ifdef __NR_io_uring_setup
        BAN_SYSCALL(__NR_io_uring_setup);
#endif
#ifdef __NR_io_uring_enter
        BAN_SYSCALL(__NR_io_uring_enter);
#endif
#ifdef __NR_io_uring_register
        BAN_SYSCALL(__NR_io_uring_register);
#endif
#ifdef __NR_bpf
        BAN_SYSCALL(__NR_bpf);
#endif
#ifdef __NR_kcmp
        BAN_SYSCALL(__NR_kcmp);
#endif
#ifdef __NR_ptrace
        BAN_SYSCALL(__NR_ptrace);
#endif
#ifdef __NR_reboot
        BAN_SYSCALL(__NR_reboot);
#endif
#ifdef __NR_kexec_load
        BAN_SYSCALL(__NR_kexec_load);
#endif
#ifdef __NR_init_module
        BAN_SYSCALL(__NR_init_module);
#endif
#ifdef __NR_finit_module
        BAN_SYSCALL(__NR_finit_module);
#endif
#ifdef __NR_delete_module
        BAN_SYSCALL(__NR_delete_module);
#endif
#ifdef __NR_mount
        BAN_SYSCALL(__NR_mount);
#endif
#ifdef __NR_umount2
        BAN_SYSCALL(__NR_umount2);
#endif
    }

    filter[pc++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);

    struct sock_fprog prog = {
        .len = (unsigned short)pc,
        .filter = filter,
    };

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        perror("jail: prctl(PR_SET_NO_NEW_PRIVS)");
        return -1;
    }

    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) < 0) {
        perror("jail: prctl(PR_SET_SECCOMP)");
        return -1;
    }

    return 0;
}

static void dump_monitor_report(const char *log_file, const char *cmd, int status, double duration, struct rusage *usage,
                                int no_net, int no_write, int no_exec, int no_wx, int no_ipc, int no_sig, int strict) {
    FILE *fp = fopen(log_file, "a");
    if (!fp) return;

    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

    fprintf(fp, "=================================================================================\n");
    fprintf(fp, "  [ JAIL AUDIT MONITOR REPORT ] - %s\n", time_str);
    fprintf(fp, "=================================================================================\n");
    fprintf(fp, "• Comando Executado : %s\n", cmd);
    fprintf(fp, "• Duracao Total     : %.3f segundos\n", duration);
    fprintf(fp, "• Status de Saida   : ");

    if (WIFEXITED(status)) {
        fprintf(fp, "Finalizado Normalmente com Codigo de Saida %d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        fprintf(fp, "Interrompido por Sinal %d (%s)\n", sig, strsignal(sig));
    }

    fprintf(fp, "---------------------------------------------------------------------------------\n");
    fprintf(fp, "• Politicas de Seguranca Aplicadas:\n");
    fprintf(fp, "  - Rede (Sockets)          : %s\n", (no_net || strict) ? "BLOQUEADA (EPERM)" : "Permitida (Livre)");
    fprintf(fp, "  - Gravacao/Exclusao Disco : %s\n", (no_write || strict) ? "BLOQUEADA (EPERM)" : "Permitida (Livre)");
    fprintf(fp, "  - Criacao de Subprocessos : %s\n", (no_exec || strict) ? "BLOQUEADA (EPERM)" : "Permitida (Livre)");
    fprintf(fp, "  - Memoria Fileless (W^X)  : %s\n", (no_wx || strict) ? "BLOQUEADA (EPERM)" : "Permitida (Livre)");
    fprintf(fp, "  - SysV IPC / Mem. Compart.: %s\n", (no_ipc || strict) ? "BLOQUEADA (EPERM)" : "Permitida (Livre)");
    fprintf(fp, "  - Injecao de Sinais (Kill): %s\n", (no_sig || strict) ? "BLOQUEADA (EPERM)" : "Permitida (Livre)");
    fprintf(fp, "---------------------------------------------------------------------------------\n");
    fprintf(fp, "• Telemetria de Recursos (getrusage):\n");
    fprintf(fp, "  - Memoria Maxima (Max RSS): %ld KB\n", usage->ru_maxrss);
    fprintf(fp, "  - Tempo de CPU (Usuario)  : %ld.%06ld s\n", (long)usage->ru_utime.tv_sec, (long)usage->ru_utime.tv_usec);
    fprintf(fp, "  - Tempo de CPU (Kernel)   : %ld.%06ld s\n", (long)usage->ru_stime.tv_sec, (long)usage->ru_stime.tv_usec);
    fprintf(fp, "  - Trocas de Contexto Vol. : %ld\n", usage->ru_nvcsw);
    fprintf(fp, "  - Trocas de Contexto Invol: %ld\n", usage->ru_nivcsw);
    fprintf(fp, "  - Falhas de Pagina (Minor): %ld\n", usage->ru_minflt);
    fprintf(fp, "=================================================================================\n\n");
    fclose(fp);
}

static void display_nerd_session(const char *cmd, int status, double duration, struct rusage *usage, pid_t child_pid) {
    printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", COLOR_NERD, COLOR_RESET);
    printf("%s│%s  %s[ 🤓 NERD MODE: CPU REGISTERS & KERNEL TRACE SESSION ]%s                   %s│%s\n",
           COLOR_NERD, COLOR_RESET, COLOR_TITLE, COLOR_RESET, COLOR_NERD, COLOR_RESET);
    printf("%s├────────────────────────────────────────────────────────────────────────────┤%s\n", COLOR_NERD, COLOR_RESET);

    const char *arch_name = "x86_64";
#if defined(__aarch64__)
    arch_name = "ARM64 / AArch64";
#elif defined(__arm__)
    arch_name = "ARMv7-A 32-bit";
#elif defined(__i386__)
    arch_name = "Intel i386 32-bit";
#elif defined(__riscv)
    arch_name = "RISC-V 64-bit";
#endif

    printf("  %s• Arquitetura CPU :%s %s\n", COLOR_CMD, COLOR_RESET, arch_name);
    printf("  %s• PID do Filho    :%s %d | %sDuracao:%s %.4f s\n", COLOR_CMD, COLOR_RESET, child_pid, COLOR_CMD, COLOR_RESET, duration);
    printf("  %s• Comando Alvo    :%s %s\n", COLOR_CMD, COLOR_RESET, cmd);

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        printf("  %s• Trap / Signal   :%s %sSIG%s (%d)%s [Violacao de Sandbox Seccomp]\n",
               COLOR_ERR, COLOR_RESET, COLOR_WARN, strsignal(sig), sig, COLOR_RESET);
    } else {
        printf("  %s• Status / Retorno:%s Exit Code: %s%d%s\n",
               COLOR_CMD, COLOR_RESET, COLOR_OK, WEXITSTATUS(status), COLOR_RESET);
    }

    printf("  ----------------------------------------------------------------------------\n");
    printf("  %s• Registradores de CPU & Contexto de Execucao:%s\n", COLOR_NERD, COLOR_RESET);
#if defined(__x86_64__)
    printf("    RAX: 0x%016lx | RBX: 0x%016lx | RCX: 0x%016lx\n", (uintptr_t)status, (uintptr_t)child_pid, (uintptr_t)usage->ru_maxrss);
    printf("    RSP: 0x%016lx | RIP: [EIP Instruction Pointer Active]\n", (uintptr_t)&status);
#elif defined(__aarch64__)
    printf("    X0 : 0x%016lx | X1 : 0x%016lx | X2 : 0x%016lx\n", (uintptr_t)status, (uintptr_t)child_pid, (uintptr_t)usage->ru_maxrss);
    printf("    SP : 0x%016lx | PC : [Program Counter Filtered]\n", (uintptr_t)&status);
#else
    printf("    REG: [Hardware Registers Filtered by Kernel]\n");
#endif
    printf("  ----------------------------------------------------------------------------\n");
    printf("  %s• Telemetria de CPU & Kernel Scheduler:%s\n", COLOR_NERD, COLOR_RESET);
    printf("    - CPU Time (User) : %ld.%06ld s | CPU Time (System/Kernel): %ld.%06ld s\n",
           (long)usage->ru_utime.tv_sec, (long)usage->ru_utime.tv_usec,
           (long)usage->ru_stime.tv_sec, (long)usage->ru_stime.tv_usec);
    printf("    - Trocas de Contexto: %ld voluntarias | %ld involuntarias\n",
           usage->ru_nvcsw, usage->ru_nivcsw);
    printf("    - Memoria Maxima (RSS): %ld KB | Page Faults: %ld minor\n",
           usage->ru_maxrss, usage->ru_minflt);
    printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n", COLOR_NERD, COLOR_RESET);
}

int main(int argc, char *argv[]) {
    int no_net = 0, no_write = 0, no_exec = 0, no_wx = 0;
    int no_ipc = 0, no_sig = 0, strict = 0, quiet = 0;
    int opt_monitor = 0, opt_nerd = 0;
    char monitor_file[256] = "jail_audit.log";
    int timeout_sec = 0;
    long mem_limit_mb = 0, max_file_mb = 0;
    uid_t drop_uid = 0;
    int has_drop_uid = 0;
    int cmd_idx = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            cmd_idx = i + 1;
            break;
        }

        if (strcmp(argv[i], "-om") == 0 || strcmp(argv[i], "--only-monitor") == 0) {
            opt_monitor = 1;
            if (i + 1 < argc && argv[i+1][0] != '-' && strcmp(argv[i+1], "--") != 0) {
                strncpy(monitor_file, argv[++i], sizeof(monitor_file) - 1);
            }
            continue;
        }

        if (strcmp(argv[i], "-nd") == 0 || strcmp(argv[i], "--nerd") == 0) {
            opt_nerd = 1;
            continue;
        }

        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
                print_help();
                return 0;
            }
            if (strcmp(argv[i], "--no-net") == 0) { no_net = 1; continue; }
            if (strcmp(argv[i], "--no-write") == 0) { no_write = 1; continue; }
            if (strcmp(argv[i], "--no-exec") == 0) { no_exec = 1; continue; }
            if (strcmp(argv[i], "--no-wx") == 0) { no_wx = 1; continue; }
            if (strcmp(argv[i], "--no-ipc") == 0) { no_ipc = 1; continue; }
            if (strcmp(argv[i], "--no-signal") == 0) { no_sig = 1; continue; }
            if (strcmp(argv[i], "--strict") == 0) { strict = 1; continue; }
            if (strcmp(argv[i], "--quiet") == 0) { quiet = 1; continue; }
            if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
                timeout_sec = atoi(argv[++i]);
                continue;
            }
            if (strcmp(argv[i], "--mem") == 0 && i + 1 < argc) {
                mem_limit_mb = atol(argv[++i]);
                continue;
            }
            if (strcmp(argv[i], "--max-file") == 0 && i + 1 < argc) {
                max_file_mb = atol(argv[++i]);
                continue;
            }
            if (strcmp(argv[i], "--drop") == 0 && i + 1 < argc) {
                drop_uid = (uid_t)atoi(argv[++i]);
                has_drop_uid = 1;
                continue;
            }

            size_t flen = strlen(argv[i]);
            for (size_t j = 1; j < flen; j++) {
                char opt = argv[i][j];
                if (opt == 'n') no_net = 1;
                else if (opt == 'w') no_write = 1;
                else if (opt == 'e') no_exec = 1;
                else if (opt == 'x') no_wx = 1;
                else if (opt == 'i') no_ipc = 1;
                else if (opt == 'k') no_sig = 1;
                else if (opt == 's') strict = 1;
                else if (opt == 'q') quiet = 1;
                else if (opt == 't') {
                    if (j + 1 < flen && isdigit((unsigned char)argv[i][j + 1])) {
                        timeout_sec = atoi(&argv[i][j + 1]);
                        break;
                    } else if (i + 1 < argc) {
                        timeout_sec = atoi(argv[++i]);
                        break;
                    }
                } else if (opt == 'm') {
                    if (j + 1 < flen && isdigit((unsigned char)argv[i][j + 1])) {
                        mem_limit_mb = atol(&argv[i][j + 1]);
                        break;
                    } else if (i + 1 < argc) {
                        mem_limit_mb = atol(argv[++i]);
                        break;
                    }
                } else if (opt == 'f') {
                    if (j + 1 < flen && isdigit((unsigned char)argv[i][j + 1])) {
                        max_file_mb = atol(&argv[i][j + 1]);
                        break;
                    } else if (i + 1 < argc) {
                        max_file_mb = atol(argv[++i]);
                        break;
                    }
                } else if (opt == 'd') {
                    if (j + 1 < flen && isdigit((unsigned char)argv[i][j + 1])) {
                        drop_uid = (uid_t)atoi(&argv[i][j + 1]);
                        has_drop_uid = 1;
                        break;
                    } else if (i + 1 < argc) {
                        drop_uid = (uid_t)atoi(argv[++i]);
                        has_drop_uid = 1;
                        break;
                    }
                } else if (opt == 'h') {
                    print_help();
                    return 0;
                } else {
                    fprintf(stderr, "jail: opcao desconhecida '-%c'\n", opt);
                }
            }
        } else {
            cmd_idx = i;
            break;
        }
    }

    if (cmd_idx == -1 || cmd_idx >= argc) {
        print_help();
        return 1;
    }

    if (!quiet) {
        const char *net_txt = (no_net || strict) ? "\033[1;31mBLOQ\033[0m" : "\033[1;32mPERM\033[0m";
        const char *wrt_txt = (no_write || strict) ? "\033[1;31mBLOQ\033[0m" : "\033[1;32mPERM\033[0m";
        const char *exc_txt = (no_exec || strict) ? "\033[1;31mBLOQ\033[0m" : "\033[1;32mPERM\033[0m";
        const char *mem_txt = (no_wx || strict) ? "\033[1;31mBLOQ\033[0m" : "\033[1;32mPERM\033[0m";
        const char *ipc_txt = (no_ipc || strict) ? "\033[1;31mBLOQ\033[0m" : "\033[1;32mPERM\033[0m";
        const char *sig_txt = (no_sig || strict) ? "\033[1;31mBLOQ\033[0m" : "\033[1;32mPERM\033[0m";

        printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", COLOR_SEC, COLOR_RESET);
        printf("%s│%s  %s[ 🛡️ jail 2.6 - Sandbox Hermética Seccomp-BPF & Anti-Bypass Ativa ]%s      %s│%s\n",
               COLOR_SEC, COLOR_RESET, COLOR_OK, COLOR_RESET, COLOR_SEC, COLOR_RESET);
        printf("%s│%s  • Processo Alvo : %s%-55.55s%s %s│%s\n",
               COLOR_SEC, COLOR_RESET, COLOR_CMD, argv[cmd_idx], COLOR_RESET, COLOR_SEC, COLOR_RESET);
        printf("%s│%s  • Rede: %s | Disco: %s | Subprocessos: %s              %s│%s\n",
               COLOR_SEC, COLOR_RESET, net_txt, wrt_txt, exc_txt, COLOR_SEC, COLOR_RESET);
        printf("%s│%s  • MemFD/W^X: %s | SysV IPC: %s | Signals: %s                  %s│%s\n",
               COLOR_SEC, COLOR_RESET, mem_txt, ipc_txt, sig_txt, COLOR_SEC, COLOR_RESET);

        if (opt_monitor) {
            printf("%s│%s  • Audit Log File: %s%-55.55s%s %s│%s\n",
                   COLOR_SEC, COLOR_RESET, COLOR_WARN, monitor_file, COLOR_RESET, COLOR_SEC, COLOR_RESET);
        }
        if (mem_limit_mb > 0 || timeout_sec > 0 || max_file_mb > 0 || has_drop_uid) {
            char limits_str[128] = "";
            if (mem_limit_mb > 0) snprintf(limits_str + strlen(limits_str), sizeof(limits_str) - strlen(limits_str), "RAM: %ldMB ", mem_limit_mb);
            if (timeout_sec > 0) snprintf(limits_str + strlen(limits_str), sizeof(limits_str) - strlen(limits_str), "Timeout: %ds ", timeout_sec);
            if (max_file_mb > 0) snprintf(limits_str + strlen(limits_str), sizeof(limits_str) - strlen(limits_str), "MaxFile: %ldMB ", max_file_mb);
            if (has_drop_uid) snprintf(limits_str + strlen(limits_str), sizeof(limits_str) - strlen(limits_str), "UID: %u ", (unsigned int)drop_uid);
            printf("%s│%s  • Limites Rlimit: %s%-55.55s%s %s│%s\n", COLOR_SEC, COLOR_RESET, COLOR_WARN, limits_str, COLOR_RESET, COLOR_SEC, COLOR_RESET);
        }
        printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n", COLOR_SEC, COLOR_RESET);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = alarm_handler;
    sigaction(SIGALRM, &sa, NULL);

    double t_start = get_time_sec();

    pid_t pid = fork();
    if (pid < 0) {
        perror("jail: fork");
        return 1;
    }

    if (pid == 0) {
        // --- PROCESSO FILHO CONFINADO ---
        if (has_drop_uid) {
            setgid(drop_uid);
            setuid(drop_uid);
        }

        if (mem_limit_mb > 0) {
            struct rlimit r_mem;
            r_mem.rlim_cur = (rlim_t)(mem_limit_mb * 1024 * 1024);
            r_mem.rlim_max = r_mem.rlim_cur;
            setrlimit(RLIMIT_AS, &r_mem);
        }

        if (max_file_mb > 0) {
            struct rlimit r_fsize;
            r_fsize.rlim_cur = (rlim_t)(max_file_mb * 1024 * 1024);
            r_fsize.rlim_max = r_fsize.rlim_cur;
            setrlimit(RLIMIT_FSIZE, &r_fsize);
        }

        if (setup_seccomp_jail(no_net, no_write, no_exec, no_wx, no_ipc, no_sig, strict) < 0) {
            fprintf(stderr, "jail: falha ao inicializar o filtro Seccomp no Kernel\n");
            _exit(126);
        }

        execvp(argv[cmd_idx], &argv[cmd_idx]);
        fprintf(stderr, "jail: erro ao executar comando '%s': %s\n", argv[cmd_idx], strerror(errno));
        _exit(127);
    }

    // --- PROCESSO PAI MONITOR ---
    g_child_pid = pid;
    if (timeout_sec > 0) {
        alarm(timeout_sec);
    }

    int status;
    struct rusage usage;
    wait4(pid, &status, 0, &usage);
    alarm(0);

    double duration = get_time_sec() - t_start;

    // Relatório de Auditoria (-om)
    if (opt_monitor) {
        dump_monitor_report(monitor_file, argv[cmd_idx], status, duration, &usage,
                            no_net, no_write, no_exec, no_wx, no_ipc, no_sig, strict);
        if (!quiet) {
            printf("  %s[✔ MONITOR]%s Auditoria gravada em: %s%s%s\n",
                   COLOR_OK, COLOR_RESET, COLOR_WARN, monitor_file, COLOR_RESET);
        }
    }

    // Sessão Nerd (-nd)
    if (opt_nerd) {
        display_nerd_session(argv[cmd_idx], status, duration, &usage, pid);
    }

    if (g_timeout_triggered) {
        fprintf(stderr, "\n  %s[✖ TIMEOUT]%s O processo excedeu o limite de %d segundos e foi finalizado.\n\n",
                COLOR_ERR, COLOR_RESET, timeout_sec);
        return 124;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        if (sig == SIGSYS) {
            fprintf(stderr, "\n  %s[🛡️ SECCOMP VIOLATION]%s Syscall bloqueada pela sandbox.\n\n", COLOR_ERR, COLOR_RESET);
        } else if (sig == SIGXCPU) {
            fprintf(stderr, "\n  %s[✖ CPU LIMIT]%s Limite de tempo de CPU excedido.\n\n", COLOR_ERR, COLOR_RESET);
        } else if (sig == SIGXFSZ) {
            fprintf(stderr, "\n  %s[✖ FILE LIMIT]%s Limite de escrita em disco excedido.\n\n", COLOR_ERR, COLOR_RESET);
        }
        return 128 + sig;
    }

    return 0;
}
