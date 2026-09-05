#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
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
#include <fcntl.h>
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

// Fallbacks de definições para arquiteturas ARM64 / x86_64
#ifndef __NR_statfs
#if defined(__aarch64__)
#define __NR_statfs 43
#elif defined(__x86_64__)
#define __NR_statfs 137
#endif
#endif

#ifndef __NR_fstatfs
#if defined(__aarch64__)
#define __NR_fstatfs 44
#elif defined(__x86_64__)
#define __NR_fstatfs 138
#endif
#endif

#ifndef __NR_pread64
#if defined(__aarch64__)
#define __NR_pread64 67
#elif defined(__x86_64__)
#define __NR_pread64 17
#endif
#endif

#ifndef __NR_pwrite64
#if defined(__aarch64__)
#define __NR_pwrite64 68
#elif defined(__x86_64__)
#define __NR_pwrite64 18
#endif
#endif

#ifndef __NR_dup3
#if defined(__aarch64__)
#define __NR_dup3 24
#elif defined(__x86_64__)
#define __NR_dup3 292
#endif
#endif

#ifndef __NR_pipe2
#if defined(__aarch64__)
#define __NR_pipe2 59
#elif defined(__x86_64__)
#define __NR_pipe2 293
#endif
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
    printf("  ./jail [OPTIONS] -- <COMMAND> [ARGS...] (or jail [OPTIONS] -- ...)\n\n");
    printf("%sPHILOSOPHY (DEFAULT-DENY WHITELIST):%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  By default, the sandbox is in COMPLETE LOCKDOWN (0 permissions granted).\n");
    printf("  Use capability unlock flags (-N, -W, -E, -I, -K, -A) to explicitly grant access.\n\n");
    printf("%sCAPABILITY UNLOCK FLAGS (PERMISSIONS):%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-N, --net%s              Unlock network access (sockets, DNS, HTTP, connect, bind)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-W, --write%s            Unlock filesystem write & modifications (creat, unlink, chmod, mkdir)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-E, --exec%s             Unlock subprocess spawning (fork, vfork, clone, execve)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-I, --ipc%s              Unlock SysV IPC and shared memory\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-K, --signal%s           Unlock signal injection & kill tampering\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-A, --all%s              Unlock all capabilities (unrestricted mode)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s--kill%s                 Kill process immediately on unauthorized syscall (SIGSYS)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("\n%sAUDIT, TELEMETRY & LIMITS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-om, --only-monitor [F]%s Dump forensic execution report to file [Default: jail_audit.log]\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-nd, --nerd%s            Enable Nerd Mode: inspect CPU registers & scheduler telemetry\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-t, --timeout <SECS>%s   Execution watchdog timeout in seconds\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-m, --mem <MB>%s         Virtual memory ceiling in Megabytes (RLIMIT_AS)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-f, --max-file <MB>%s    Limit maximum disk file size (RLIMIT_FSIZE)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-d, --drop <UID>%s       Drop process UID/GID credentials before execution\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-q, --quiet%s            Suppress startup banner\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s             Display this formatted help guide and exit\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sCOMBINED SHORT FLAGS EXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./jail -- ./calc 10 + 20%s                  (Confinamento Total: 0 permissoes por padrao)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./jail -N -- ./httpget http://google.com%s  (Desbloqueia APENAS a rede)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./jail -E -- sh%s                           (Desbloqueia shell e subprocessos)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./jail -A -- sh%s                           (Desbloqueia todas as capacidades)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

// =========================================================================
// MOTOR SECCOMP-BPF (WHITELIST BASELINE + CAPABILITIES)
// =========================================================================
static int setup_seccomp_jail(int allow_net, int allow_write, int allow_exec,
                              int allow_ipc, int allow_sig, int kill_on_violation) {
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

    struct sock_filter filter[1024];
    int pc = 0;

    // 1. Verificação de Arquitetura da CPU
    if (arch != 0) {
        filter[pc++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (offsetof(struct seccomp_data, arch)));
        filter[pc++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, arch, 1, 0);
        filter[pc++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
    }

    // Carrega o número da chamada de sistema
    filter[pc++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (offsetof(struct seccomp_data, nr)));

    #define ALLOW_SYSCALL(sys_nr) do { \
        filter[pc++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (sys_nr), 0, 1); \
        filter[pc++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW); \
    } while (0)

    // =====================================================================
    // 1. SYSCALLS DE SOBREVIVÊNCIA DO RUNTIME (LIBC, BIONIC & LINKER)
    // =====================================================================
#ifdef __NR_exit
    ALLOW_SYSCALL(__NR_exit);
#endif
#ifdef __NR_exit_group
    ALLOW_SYSCALL(__NR_exit_group);
#endif
#ifdef __NR_rt_sigreturn
    ALLOW_SYSCALL(__NR_rt_sigreturn);
#endif
#ifdef __NR_read
    ALLOW_SYSCALL(__NR_read);
#endif
#ifdef __NR_write
    ALLOW_SYSCALL(__NR_write);
#endif
#ifdef __NR_pread64
    ALLOW_SYSCALL(__NR_pread64);
#endif
#ifdef __NR_pwrite64
    ALLOW_SYSCALL(__NR_pwrite64);
#endif
#ifdef __NR_readv
    ALLOW_SYSCALL(__NR_readv);
#endif
#ifdef __NR_writev
    ALLOW_SYSCALL(__NR_writev);
#endif
#ifdef __NR_close
    ALLOW_SYSCALL(__NR_close);
#endif
#ifdef __NR_lseek
    ALLOW_SYSCALL(__NR_lseek);
#endif
#ifdef __NR_brk
    ALLOW_SYSCALL(__NR_brk);
#endif
#ifdef __NR_mmap
    ALLOW_SYSCALL(__NR_mmap);
#endif
#ifdef __NR_munmap
    ALLOW_SYSCALL(__NR_munmap);
#endif
#ifdef __NR_mprotect
    ALLOW_SYSCALL(__NR_mprotect);
#endif
#ifdef __NR_madvise
    ALLOW_SYSCALL(__NR_madvise);
#endif
#ifdef __NR_mremap
    ALLOW_SYSCALL(__NR_mremap);
#endif
#ifdef __NR_futex
    ALLOW_SYSCALL(__NR_futex);
#endif
#ifdef __NR_set_tid_address
    ALLOW_SYSCALL(__NR_set_tid_address);
#endif
#ifdef __NR_getpid
    ALLOW_SYSCALL(__NR_getpid);
#endif
#ifdef __NR_getppid
    ALLOW_SYSCALL(__NR_getppid);
#endif
#ifdef __NR_getuid
    ALLOW_SYSCALL(__NR_getuid);
#endif
#ifdef __NR_geteuid
    ALLOW_SYSCALL(__NR_geteuid);
#endif
#ifdef __NR_getgid
    ALLOW_SYSCALL(__NR_getgid);
#endif
#ifdef __NR_getegid
    ALLOW_SYSCALL(__NR_getegid);
#endif
#ifdef __NR_gettid
    ALLOW_SYSCALL(__NR_gettid);
#endif
#ifdef __NR_getrandom
    ALLOW_SYSCALL(__NR_getrandom);
#endif
#ifdef __NR_clock_gettime
    ALLOW_SYSCALL(__NR_clock_gettime);
#endif
#ifdef __NR_clock_getres
    ALLOW_SYSCALL(__NR_clock_getres);
#endif
#ifdef __NR_nanosleep
    ALLOW_SYSCALL(__NR_nanosleep);
#endif
#ifdef __NR_clock_nanosleep
    ALLOW_SYSCALL(__NR_clock_nanosleep);
#endif
#ifdef __NR_rt_sigaction
    ALLOW_SYSCALL(__NR_rt_sigaction);
#endif
#ifdef __NR_rt_sigprocmask
    ALLOW_SYSCALL(__NR_rt_sigprocmask);
#endif
#ifdef __NR_sigaltstack
    ALLOW_SYSCALL(__NR_sigaltstack);
#endif
#ifdef __NR_ioctl
    ALLOW_SYSCALL(__NR_ioctl);
#endif
#ifdef __NR_fcntl
    ALLOW_SYSCALL(__NR_fcntl);
#endif
#ifdef __NR_poll
    ALLOW_SYSCALL(__NR_poll);
#endif
#ifdef __NR_ppoll
    ALLOW_SYSCALL(__NR_ppoll);
#endif
#ifdef __NR_select
    ALLOW_SYSCALL(__NR_select);
#endif
#ifdef __NR_pselect6
    ALLOW_SYSCALL(__NR_pselect6);
#endif
#ifdef __NR_fstat
    ALLOW_SYSCALL(__NR_fstat);
#endif
#ifdef __NR_fstatat
    ALLOW_SYSCALL(__NR_fstatat);
#endif
#ifdef __NR_newfstatat
    ALLOW_SYSCALL(__NR_newfstatat);
#endif
#ifdef __NR_stat
    ALLOW_SYSCALL(__NR_stat);
#endif
#ifdef __NR_lstat
    ALLOW_SYSCALL(__NR_lstat);
#endif
#ifdef __NR_statfs
    ALLOW_SYSCALL(__NR_statfs);
#endif
#ifdef __NR_fstatfs
    ALLOW_SYSCALL(__NR_fstatfs);
#endif
#ifdef __NR_statfs64
    ALLOW_SYSCALL(__NR_statfs64);
#endif
#ifdef __NR_fstatfs64
    ALLOW_SYSCALL(__NR_fstatfs64);
#endif
#ifdef __NR_prctl
    ALLOW_SYSCALL(__NR_prctl);
#endif
#ifdef __NR_sched_getaffinity
    ALLOW_SYSCALL(__NR_sched_getaffinity);
#endif
#ifdef __NR_sched_getscheduler
    ALLOW_SYSCALL(__NR_sched_getscheduler);
#endif
#ifdef __NR_uname
    ALLOW_SYSCALL(__NR_uname);
#endif
#ifdef __NR_getrlimit
    ALLOW_SYSCALL(__NR_getrlimit);
#endif
#ifdef __NR_prlimit64
    ALLOW_SYSCALL(__NR_prlimit64);
#endif
#ifdef __NR_getcwd
    ALLOW_SYSCALL(__NR_getcwd);
#endif
#ifdef __NR_access
    ALLOW_SYSCALL(__NR_access);
#endif
#ifdef __NR_faccessat
    ALLOW_SYSCALL(__NR_faccessat);
#endif
#ifdef __NR_faccessat2
    ALLOW_SYSCALL(__NR_faccessat2);
#endif
#ifdef __NR_readlink
    ALLOW_SYSCALL(__NR_readlink);
#endif
#ifdef __NR_readlinkat
    ALLOW_SYSCALL(__NR_readlinkat);
#endif
#ifdef __NR_open
    ALLOW_SYSCALL(__NR_open);
#endif
#ifdef __NR_openat
    ALLOW_SYSCALL(__NR_openat);
#endif
#ifdef __NR_dup
    ALLOW_SYSCALL(__NR_dup);
#endif
#ifdef __NR_dup2
    ALLOW_SYSCALL(__NR_dup2);
#endif
#ifdef __NR_dup3
    ALLOW_SYSCALL(__NR_dup3);
#endif
#ifdef __NR_pipe
    ALLOW_SYSCALL(__NR_pipe);
#endif
#ifdef __NR_pipe2
    ALLOW_SYSCALL(__NR_pipe2);
#endif
#ifdef __NR_capget
    ALLOW_SYSCALL(__NR_capget);
#endif

    // =====================================================================
    // 2. CAPACIDADE: ESCRITA E MODIFICAÇÃO DE DISCO (-W / --write)
    // =====================================================================
    if (allow_write) {
#ifdef __NR_creat
        ALLOW_SYSCALL(__NR_creat);
#endif
#ifdef __NR_unlink
        ALLOW_SYSCALL(__NR_unlink);
#endif
#ifdef __NR_unlinkat
        ALLOW_SYSCALL(__NR_unlinkat);
#endif
#ifdef __NR_rmdir
        ALLOW_SYSCALL(__NR_rmdir);
#endif
#ifdef __NR_mkdir
        ALLOW_SYSCALL(__NR_mkdir);
#endif
#ifdef __NR_mkdirat
        ALLOW_SYSCALL(__NR_mkdirat);
#endif
#ifdef __NR_rename
        ALLOW_SYSCALL(__NR_rename);
#endif
#ifdef __NR_renameat
        ALLOW_SYSCALL(__NR_renameat);
#endif
#ifdef __NR_renameat2
        ALLOW_SYSCALL(__NR_renameat2);
#endif
#ifdef __NR_chmod
        ALLOW_SYSCALL(__NR_chmod);
#endif
#ifdef __NR_fchmod
        ALLOW_SYSCALL(__NR_fchmod);
#endif
#ifdef __NR_fchmodat
        ALLOW_SYSCALL(__NR_fchmodat);
#endif
#ifdef __NR_truncate
        ALLOW_SYSCALL(__NR_truncate);
#endif
#ifdef __NR_ftruncate
        ALLOW_SYSCALL(__NR_ftruncate);
#endif
#ifdef __NR_utimensat
        ALLOW_SYSCALL(__NR_utimensat);
#endif
    }

    // =====================================================================
    // 3. CAPACIDADE: SUBPROCESSOS E EXECUÇÃO (-E / --exec)
    // =====================================================================
    if (allow_exec) {
#ifdef __NR_execve
        ALLOW_SYSCALL(__NR_execve);
#endif
#ifdef __NR_execveat
        ALLOW_SYSCALL(__NR_execveat);
#endif
#ifdef __NR_clone
        ALLOW_SYSCALL(__NR_clone);
#endif
#ifdef __NR_clone3
        ALLOW_SYSCALL(__NR_clone3);
#endif
#ifdef __NR_fork
        ALLOW_SYSCALL(__NR_fork);
#endif
#ifdef __NR_vfork
        ALLOW_SYSCALL(__NR_vfork);
#endif
#ifdef __NR_wait4
        ALLOW_SYSCALL(__NR_wait4);
#endif
#ifdef __NR_waitid
        ALLOW_SYSCALL(__NR_waitid);
#endif
    }

    // =====================================================================
    // 4. CAPACIDADE: REDE E CONEXÕES (-N / --net)
    // =====================================================================
    if (allow_net) {
#ifdef __NR_socket
        ALLOW_SYSCALL(__NR_socket);
#endif
#ifdef __NR_connect
        ALLOW_SYSCALL(__NR_connect);
#endif
#ifdef __NR_bind
        ALLOW_SYSCALL(__NR_bind);
#endif
#ifdef __NR_listen
        ALLOW_SYSCALL(__NR_listen);
#endif
#ifdef __NR_accept
        ALLOW_SYSCALL(__NR_accept);
#endif
#ifdef __NR_accept4
        ALLOW_SYSCALL(__NR_accept4);
#endif
#ifdef __NR_sendto
        ALLOW_SYSCALL(__NR_sendto);
#endif
#ifdef __NR_recvfrom
        ALLOW_SYSCALL(__NR_recvfrom);
#endif
#ifdef __NR_sendmsg
        ALLOW_SYSCALL(__NR_sendmsg);
#endif
#ifdef __NR_recvmsg
        ALLOW_SYSCALL(__NR_recvmsg);
#endif
#ifdef __NR_getsockname
        ALLOW_SYSCALL(__NR_getsockname);
#endif
#ifdef __NR_getpeername
        ALLOW_SYSCALL(__NR_getpeername);
#endif
#ifdef __NR_setsockopt
        ALLOW_SYSCALL(__NR_setsockopt);
#endif
#ifdef __NR_getsockopt
        ALLOW_SYSCALL(__NR_getsockopt);
#endif
    }

    // =====================================================================
    // 5. CAPACIDADE: IPC E MEMÓRIA COMPARTILHADA (-I / --ipc)
    // =====================================================================
    if (allow_ipc) {
#ifdef __NR_shmget
        ALLOW_SYSCALL(__NR_shmget);
#endif
#ifdef __NR_shmat
        ALLOW_SYSCALL(__NR_shmat);
#endif
#ifdef __NR_shmdt
        ALLOW_SYSCALL(__NR_shmdt);
#endif
#ifdef __NR_shmctl
        ALLOW_SYSCALL(__NR_shmctl);
#endif
#ifdef __NR_semget
        ALLOW_SYSCALL(__NR_semget);
#endif
#ifdef __NR_semop
        ALLOW_SYSCALL(__NR_semop);
#endif
#ifdef __NR_msgget
        ALLOW_SYSCALL(__NR_msgget);
#endif
#ifdef __NR_msgsnd
        ALLOW_SYSCALL(__NR_msgsnd);
#endif
#ifdef __NR_msgrcv
        ALLOW_SYSCALL(__NR_msgrcv);
#endif
    }

    // =====================================================================
    // 6. CAPACIDADE: SINAIS E KILL (-K / --signal)
    // =====================================================================
    if (allow_sig) {
#ifdef __NR_kill
        ALLOW_SYSCALL(__NR_kill);
#endif
#ifdef __NR_tkill
        ALLOW_SYSCALL(__NR_tkill);
#endif
#ifdef __NR_tgkill
        ALLOW_SYSCALL(__NR_tgkill);
#endif
#ifdef __NR_pidfd_send_signal
        ALLOW_SYSCALL(__NR_pidfd_send_signal);
#endif
    }

    // DEFAULT-DENY: Bloqueia qualquer syscall não autorizada
    uint32_t deny_action = kill_on_violation ? SECCOMP_RET_KILL_PROCESS : (SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA));
    filter[pc++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, deny_action);

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
                                int allow_net, int allow_write, int allow_exec, int allow_ipc, int allow_sig) {
    FILE *fp = fopen(log_file, "a");
    if (!fp) return;

    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

    fprintf(fp, "=================================================================================\n");
    fprintf(fp, "  [ JAIL 3.0 CAPABILITY AUDIT REPORT ] - %s\n", time_str);
    fprintf(fp, "=================================================================================\n");
    fprintf(fp, "• Comando Executado : %s\n", cmd);
    fprintf(fp, "• Duracao Total     : %.3f segundos\n", duration);
    fprintf(fp, "• Status de Saida   : ");

    if (WIFEXITED(status)) {
        fprintf(fp, "Finalizado Normalmente (Exit Code: %d)\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        fprintf(fp, "Interrompido por Sinal %d (%s)\n", sig, strsignal(sig));
    }

    fprintf(fp, "---------------------------------------------------------------------------------\n");
    fprintf(fp, "• Capacidades Desbloqueadas (Granted Privileges):\n");
    fprintf(fp, "  - Rede (-N / --net)       : %s\n", allow_net ? "DESBLOQUEADA (Permitida)" : "Bloqueada (Padrao)");
    fprintf(fp, "  - Escrita Disco (-W)      : %s\n", allow_write ? "DESBLOQUEADA (Permitida)" : "Bloqueada (Padrao)");
    fprintf(fp, "  - Subprocessos (-E)       : %s\n", allow_exec ? "DESBLOQUEADA (Permitida)" : "Bloqueada (Padrao)");
    fprintf(fp, "  - SysV IPC (-I / --ipc)   : %s\n", allow_ipc ? "DESBLOQUEADA (Permitida)" : "Bloqueada (Padrao)");
    fprintf(fp, "  - Sinais (-K / --signal)  : %s\n", allow_sig ? "DESBLOQUEADA (Permitida)" : "Bloqueada (Padrao)");
    fprintf(fp, "---------------------------------------------------------------------------------\n");
    fprintf(fp, "• Telemetria de Recursos (getrusage):\n");
    fprintf(fp, "  - Memoria Maxima (Max RSS): %ld KB\n", usage->ru_maxrss);
    fprintf(fp, "  - Tempo de CPU (Usuario)  : %ld.%06ld s\n", (long)usage->ru_utime.tv_sec, (long)usage->ru_utime.tv_usec);
    fprintf(fp, "  - Tempo de CPU (Kernel)   : %ld.%06ld s\n", (long)usage->ru_stime.tv_sec, (long)usage->ru_stime.tv_usec);
    fprintf(fp, "  - Trocas de Contexto Vol. : %ld\n", usage->ru_nvcsw);
    fprintf(fp, "  - Trocas de Contexto Invol: %ld\n", usage->ru_nivcsw);
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
    int allow_net = 0, allow_write = 0, allow_exec = 0;
    int allow_ipc = 0, allow_sig = 0, opt_kill = 0, quiet = 0;
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

        if (strcmp(argv[i], "-N") == 0 || strcmp(argv[i], "--net") == 0) { allow_net = 1; continue; }
        if (strcmp(argv[i], "-W") == 0 || strcmp(argv[i], "--write") == 0) { allow_write = 1; continue; }
        if (strcmp(argv[i], "-E") == 0 || strcmp(argv[i], "--exec") == 0) { allow_exec = 1; continue; }
        if (strcmp(argv[i], "-I") == 0 || strcmp(argv[i], "--ipc") == 0) { allow_ipc = 1; continue; }
        if (strcmp(argv[i], "-K") == 0 || strcmp(argv[i], "--signal") == 0) { allow_sig = 1; continue; }
        if (strcmp(argv[i], "-A") == 0 || strcmp(argv[i], "--all") == 0 || strcmp(argv[i], "--unconfined") == 0) {
            allow_net = allow_write = allow_exec = allow_ipc = allow_sig = 1;
            continue;
        }

        if (strcmp(argv[i], "--kill") == 0) { opt_kill = 1; continue; }

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

            // Flags Curtas Combinadas (ex: -N, -W, -E, -A, -NW, -NE)
            size_t flen = strlen(argv[i]);
            for (size_t j = 1; j < flen; j++) {
                char opt = argv[i][j];
                if (opt == 'N') allow_net = 1;
                else if (opt == 'W') allow_write = 1;
                else if (opt == 'E') allow_exec = 1;
                else if (opt == 'I') allow_ipc = 1;
                else if (opt == 'K') allow_sig = 1;
                else if (opt == 'A') {
                    allow_net = allow_write = allow_exec = allow_ipc = allow_sig = 1;
                } else if (opt == 'q') quiet = 1;
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

    if (cmd_idx == -1 || cmd_idx >= argc) {
        print_help();
        return 1;
    }

    if (!quiet) {
        printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", COLOR_SEC, COLOR_RESET);
        printf("%s│%s  %s[ 🛡️ JAIL 3.0 - Hermetic Default-Deny Sandbox (Capabilities Model) ]%s    %s│%s\n",
               COLOR_SEC, COLOR_RESET, COLOR_OK, COLOR_RESET, COLOR_SEC, COLOR_RESET);
        printf("%s│%s  • Processo Alvo : %s%-55.55s%s %s│%s\n",
               COLOR_SEC, COLOR_RESET, COLOR_CMD, argv[cmd_idx], COLOR_RESET, COLOR_SEC, COLOR_RESET);
        printf("%s│%s  • Confinamento  : %sDefault-Deny Estrito (Tudo bloqueado exceto grants)%s    %s│%s\n",
               COLOR_SEC, COLOR_RESET, COLOR_WARN, COLOR_RESET, COLOR_SEC, COLOR_RESET);
        printf("%s│%s  • Rede: %s | Escrita Disco: %s | Subprocessos: %s         %s│%s\n",
               COLOR_SEC, COLOR_RESET,
               allow_net ? "\033[1;32mGRANTED\033[0m" : "\033[1;31mBLOCKED\033[0m",
               allow_write ? "\033[1;32mGRANTED\033[0m" : "\033[1;31mBLOCKED\033[0m",
               allow_exec ? "\033[1;32mGRANTED\033[0m" : "\033[1;31mBLOCKED\033[0m",
               COLOR_SEC, COLOR_RESET);
        printf("%s│%s  • IPC/Shm: %s | Sinais/Kill: %s                             %s│%s\n",
               COLOR_SEC, COLOR_RESET,
               allow_ipc ? "\033[1;32mGRANTED\033[0m" : "\033[1;31mBLOCKED\033[0m",
               allow_sig ? "\033[1;32mGRANTED\033[0m" : "\033[1;31mBLOCKED\033[0m",
               COLOR_SEC, COLOR_RESET);

        if (opt_monitor) {
            printf("%s│%s  • Audit Log File: %s%-55.55s%s %s│%s\n",
                   COLOR_SEC, COLOR_RESET, COLOR_WARN, monitor_file, COLOR_RESET, COLOR_SEC, COLOR_RESET);
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
        // Processo Filho Confinado
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

        if (setup_seccomp_jail(allow_net, allow_write, allow_exec, allow_ipc, allow_sig, opt_kill) < 0) {
            fprintf(stderr, "jail: falha ao inicializar o filtro Seccomp no Kernel\n");
            _exit(126);
        }

        execvp(argv[cmd_idx], &argv[cmd_idx]);
        fprintf(stderr, "jail: erro ao executar comando '%s': %s\n", argv[cmd_idx], strerror(errno));
        _exit(127);
    }

    // Processo Pai Monitor
    g_child_pid = pid;
    if (timeout_sec > 0) {
        alarm(timeout_sec);
    }

    int status;
    struct rusage usage;
    wait4(pid, &status, 0, &usage);
    alarm(0);

    double duration = get_time_sec() - t_start;

    if (opt_monitor) {
        dump_monitor_report(monitor_file, argv[cmd_idx], status, duration, &usage,
                            allow_net, allow_write, allow_exec, allow_ipc, allow_sig);
        if (!quiet) {
            printf("  %s[✔ MONITOR]%s Auditoria gravada em: %s%s%s\n",
                   COLOR_OK, COLOR_RESET, COLOR_WARN, monitor_file, COLOR_RESET);
        }
    }

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
            fprintf(stderr, "\n  %s[🛡️ SECCOMP VIOLATION]%s Syscall nao autorizada bloqueada (Default-Deny).\n\n", COLOR_ERR, COLOR_RESET);
        }
        return 128 + sig;
    }

    return 0;
}
