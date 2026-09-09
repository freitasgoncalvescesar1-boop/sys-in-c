#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "low.h"

#define COLOR_RESET "\033[0m"
#define COLOR_VAL   "\033[1;36m"
#define COLOR_TAG   "\033[1;33m"

static void print_help(void) {
    low_print_banner("ldate");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./ldate [OPTIONS] [+FORMAT]\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Lightweight, standalone, nanosecond-precise POSIX date & timestamp utility.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-u, --utc%s          Print time in Coordinated Universal Time (UTC)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-s, --sec%s          Print raw Unix Epoch timestamp in seconds\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-n, --nano%s         Print high-precision timestamp with nanoseconds\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-i, --iso%s          Print date in ISO-8601 format (YYYY-MM-DDTHH:MM:SS)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-r, --rfc%s          Print date in RFC-2822 format\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s         Display this formatted help guide and exit\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./ldate%s                                (Data e hora local)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./ldate -u%s                             (Hora UTC pura)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./ldate -n%s                             (Timestamp com precisao de nanossegundos)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./ldate +\"%%Y-%%m-%%d %%H:%%M:%%S\"%s            (Formatacao personalizada)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

int main(int argc, char *argv[]) {
    int opt_utc = 0, opt_sec = 0, opt_nano = 0, opt_iso = 0, opt_rfc = 0;
    const char *custom_fmt = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--utc") == 0) opt_utc = 1;
        else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--sec") == 0) opt_sec = 1;
        else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--nano") == 0) opt_nano = 1;
        else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--iso") == 0) opt_iso = 1;
        else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--rfc") == 0) opt_rfc = 1;
        else if (argv[i][0] == '+') custom_fmt = argv[i] + 1;
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    if (opt_sec) {
        printf("%lld\n", (long long)ts.tv_sec);
        return 0;
    }

    if (opt_nano) {
        printf("%lld.%09ld\n", (long long)ts.tv_sec, ts.tv_nsec);
        return 0;
    }

    struct tm *tm_info = opt_utc ? gmtime(&ts.tv_sec) : localtime(&ts.tv_sec);
    char buf[256];

    if (custom_fmt) {
        strftime(buf, sizeof(buf), custom_fmt, tm_info);
    } else if (opt_iso) {
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", tm_info);
    } else if (opt_rfc) {
        strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S %z", tm_info);
    } else {
        strftime(buf, sizeof(buf), "%a %b %e %H:%M:%S %Z %Y", tm_info);
    }

    printf("%s\n", buf);
    return 0;
}
