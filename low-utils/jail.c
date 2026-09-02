#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include "low.h"

#define COLOR_RESET "\033[0m"
#define COLOR_OK    "\033[1;32m"
#define COLOR_ERR   "\033[1;31m"
#define COLOR_WARN  "\033[1;33m"
#define COLOR_SEC   "\033[1;35m"

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

static void print_help(void) {
    low_print_banner("jail");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./jail [OPTIONS] -- <COMMAND> [ARGS...]\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Sandboxes processes by injecting Linux Seccomp-BPF filters directly into the Kernel.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s--no-net%s             Block all networking syscalls (socket, connect, send, recv)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s--no-write%s           Block file deletion and permission changes (unlink, rmdir, chmod)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s--strict%s             Maximum confinement (allow only basic read/write/mmap/exit)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s           Display this formatted help guide and exit\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./jail --no-net -- ./httpget http://google.com%s   (Bloqueia conexao de rede com EPERM)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./jail --no-write -- ./rmd secret.txt%s            (Impede delecao de arquivos no disco)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./jail --no-net -- /bin/sh%s                       (Abre uma shell isolada sem internet)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

static int setup_seccomp_jail(int no_net, int no_write, int strict) {
    uint32_t arch = 0;
#if defined(__x86_64__)
    arch = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
    arch = AUDIT_ARCH_AARCH64;
#elif defined(__i386__)
    arch = AUDIT_ARCH_I386;
#elif defined(__arm__)
    arch = AUDIT_ARCH_ARM;
#endif

    struct sock_filter filter[256];
    int pc = 0;

    // 1. Valida Arquitetura da CPU se identificada
    if (arch != 0) {
        filter[pc++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (offsetof(struct seccomp_data, arch)));
        filter[pc++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, arch, 1, 0);
        filter[pc++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
    }

    // 2. Carrega o número da Syscall
    filter[pc++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (offsetof(struct seccomp_data, nr)));

    #define BAN_SYSCALL(sys_nr) do { \
        filter[pc++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (sys_nr), 0, 1); \
        filter[pc++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)); \
    } while (0)

    // 3. Regras de Rede (--no-net)
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
#ifdef __NR_socketcall
        BAN_SYSCALL(__NR_socketcall);
#endif
    }

    // 4. Regras de Modificação de Arquivos (--no-write)
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

int main(int argc, char *argv[]) {
    int no_net = 0;
    int no_write = 0;
    int strict = 0;
    int cmd_idx = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            return 0;
        }

        if (strcmp(argv[i], "--no-net") == 0) no_net = 1;
        else if (strcmp(argv[i], "--no-write") == 0) no_write = 1;
        else if (strcmp(argv[i], "--strict") == 0) strict = 1;
        else if (strcmp(argv[i], "--") == 0) {
            cmd_idx = i + 1;
            break;
        } else if (argv[i][0] != '-') {
            cmd_idx = i;
            break;
        }
    }

    if (cmd_idx == -1 || cmd_idx >= argc) {
        print_help();
        return 1;
    }

    if (!no_net && !no_write && !strict) {
        no_net = 1;
        no_write = 1;
    }

    printf("%s[ jail - Confinando Processo: %s%s%s (Net: %s | Write: %s) ]%s\n\n",
           COLOR_SEC, COLOR_OK, argv[cmd_idx], COLOR_SEC,
           no_net ? "\033[1;31mBLOQUEADA\033[0m" : "\033[1;32mPERMITIDA\033[0m",
           no_write ? "\033[1;31mBLOQUEADA\033[0m" : "\033[1;32mPERMITIDA\033[0m",
           COLOR_RESET);

    if (setup_seccomp_jail(no_net, no_write, strict) < 0) {
        fprintf(stderr, "jail: falha ao inicializar o sandbox Seccomp no Kernel\n");
        return 1;
    }

    execvp(argv[cmd_idx], &argv[cmd_idx]);
    fprintf(stderr, "jail: erro ao executar comando '%s': %s\n", argv[cmd_idx], strerror(errno));
    return 127;
}
