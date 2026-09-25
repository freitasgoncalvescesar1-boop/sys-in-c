#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * syscall - tiny numeric syscall caller
 *
 * The syscall number and up to six arguments are accepted as decimal or
 * hexadecimal integers. Arguments are passed as machine-word values, so
 * pointer-sized hexadecimal addresses are supported as well.
 */

static unsigned long parse_arg(const char *s, int *ok)
{
    char *end;
    unsigned long long value;

    errno = 0;

    if (*s == '-') {
        long long signed_value = strtoll(s, &end, 0);

        if (errno != 0 || end == s || *end != '\0') {
            *ok = 0;
            return 0;
        }

        *ok = 1;
        return (unsigned long)signed_value;
    }

    value = strtoull(s, &end, 0);
    if (errno != 0 || end == s || *end != '\0' || value > ULONG_MAX) {
        *ok = 0;
        return 0;
    }

    *ok = 1;
    return (unsigned long)value;
}

static long parse_syscall_number(const char *s, int *ok)
{
    unsigned long value = parse_arg(s, ok);

    if (!*ok || value > LONG_MAX) {
        *ok = 0;
        return 0;
    }

    return (long)value;
}

static long call_syscall(long number, int argc, char **argv)
{
    unsigned long args[6] = {0};

    for (int i = 0; i < argc; ++i) {
        int ok;

        args[i] = parse_arg(argv[i], &ok);
        if (!ok) {
            fprintf(stderr, "argument %d must be an integer: %s\n", i + 1, argv[i]);
            exit(2);
        }
    }

    return syscall(number,
                   args[0], args[1], args[2],
                   args[3], args[4], args[5]);
}

int main(int argc, char **argv)
{
    int ok;
    long number;

    if (argc < 2 || argc > 8) {
        fprintf(stderr, "usage: %s <syscall> [arg1 ... arg6]\n", argv[0]);
        return 2;
    }

    number = parse_syscall_number(argv[1], &ok);
    if (!ok) {
        fprintf(stderr, "invalid syscall number: %s\n", argv[1]);
        return 2;
    }

    errno = 0;
    long ret = call_syscall(number, argc - 2, argv + 2);

    if (ret == -1) {
        fprintf(stderr, "syscall(%ld) failed: %s (errno=%d)\n",
                number, strerror(errno), errno);
        return 1;
    }

    printf("syscall(%ld) = %ld\n", number, ret);
    return 0;
}
