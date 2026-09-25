#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

typedef enum { COLOR_AUTO, COLOR_ALWAYS, COLOR_NEVER } color_mode_t;
static int use_color = 0;

#define C_RESET "\033[0m"
#define C_RED   "\033[31m"
#define C_GREEN "\033[32m"

static void usage(const char *prog)
{
    printf("usage: %s [OPTIONS] [NAME=VALUE ...] [COMMAND [ARG ...]]\n", prog);
    puts("\nOptions:");
    puts("  -i, --ignore-environment  start with an empty environment");
    puts("  -C, --color               force colored section headers");
    puts("      --no-color            disable colored output");
    puts("  -h, --help                show this help");
    puts("\nSensitive variables are redacted by default when the environment is displayed.");
}

static const char *paint(const char *color, const char *text)
{
    static char out[512];
    if (!use_color)
        return text;
    snprintf(out, sizeof(out), "%s%s%s", color, text, C_RESET);
    return out;
}

static int is_sensitive_name(const char *name)
{
    static const char *const needles[] = {
        "token", "password", "passwd", "pass", "secret",
        "apikey", "api_key", "access_key", "private_key",
        "credential", "auth", "authorization", "session",
        "cookie", "root", "ssh_key", "client_secret"
    };
    char lower[256];
    size_t n = strlen(name);

    if (n == 0)
        return 0;
    if (n >= sizeof(lower))
        n = sizeof(lower) - 1;

    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c >= 'A' && c <= 'Z')
            c = (unsigned char)(c - 'A' + 'a');
        lower[i] = (char)c;
    }
    lower[n] = '\0';

    for (size_t i = 0; i < sizeof(needles) / sizeof(needles[0]); ++i)
        if (strstr(lower, needles[i]) != NULL)
            return 1;

    return 0;
}

static int is_sensitive_entry(const char *entry)
{
    const char *equals = strchr(entry, '=');
    size_t name_len;
    char *name;
    int sensitive;

    if (equals == NULL)
        return 0;

    name_len = (size_t)(equals - entry);
    name = malloc(name_len + 1);
    if (name == NULL)
        return -1;

    memcpy(name, entry, name_len);
    name[name_len] = '\0';
    sensitive = is_sensitive_name(name);
    free(name);
    return sensitive;
}

static void print_environment(void)
{
    char **entry;

    puts(paint(C_RED, "[BLOCKED]"));
    for (entry = environ; *entry != NULL; ++entry) {
        int sensitive = is_sensitive_entry(*entry);
        if (sensitive < 0) {
            perror("malloc");
            return;
        }
        if (sensitive)
            printf("  %.*s=[CENSORED]\n",
                   (int)(strchr(*entry, '=') - *entry), *entry);
    }

    puts(paint(C_GREEN, "\n[NORMAL ENV]"));
    for (entry = environ; *entry != NULL; ++entry) {
        int sensitive = is_sensitive_entry(*entry);
        if (sensitive < 0) {
            perror("malloc");
            return;
        }
        if (!sensitive)
            puts(*entry);
    }
}

static int set_environment(const char *assignment)
{
    const char *equals = strchr(assignment, '=');
    size_t name_len;
    char *name;
    int ok;

    if (equals == NULL || equals == assignment) {
        fprintf(stderr, "invalid assignment: %s\n", assignment);
        return 0;
    }

    name_len = (size_t)(equals - assignment);
    name = malloc(name_len + 1);
    if (name == NULL) {
        perror("malloc");
        return 0;
    }

    memcpy(name, assignment, name_len);
    name[name_len] = '\0';
    ok = setenv(name, equals + 1, 1) == 0;

    if (!ok)
        perror("setenv");

    free(name);
    return ok;
}

static void exec_command(char **argv, int index)
{
    execvp(argv[index], &argv[index]);
    fprintf(stderr, "%s: %s\n", argv[index], strerror(errno));
    exit(127);
}

int main(int argc, char **argv)
{
    int index = 1;
    int clear = 0;
    color_mode_t color_mode = COLOR_AUTO;

    while (index < argc) {
        if (strcmp(argv[index], "-i") == 0 ||
            strcmp(argv[index], "--ignore-environment") == 0) {
            clear = 1;
            ++index;
        } else if (strcmp(argv[index], "-C") == 0 ||
                   strcmp(argv[index], "--color") == 0) {
            color_mode = COLOR_ALWAYS;
            ++index;
        } else if (strcmp(argv[index], "--no-color") == 0) {
            color_mode = COLOR_NEVER;
            ++index;
        } else if (strcmp(argv[index], "-h") == 0 ||
                   strcmp(argv[index], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[index][0] == '-') {
            fprintf(stderr, "%s: unknown option: %s\n", argv[0], argv[index]);
            return 2;
        } else {
            break;
        }
    }

    if (color_mode == COLOR_ALWAYS)
        use_color = 1;
    else if (color_mode == COLOR_AUTO)
        use_color = isatty(STDOUT_FILENO);

    if (clear && clearenv() != 0) {
        perror("clearenv");
        return 1;
    }

    while (index < argc && strchr(argv[index], '=') != NULL) {
        if (!set_environment(argv[index]))
            return 2;
        ++index;
    }

    if (index == argc) {
        print_environment();
        return 0;
    }

    exec_command(argv, index);
    return 127;
}
