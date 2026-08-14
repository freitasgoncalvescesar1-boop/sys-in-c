#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "../libutilipc/utilipc.h"

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 2) {
        printf("Usage: watchcmd [interval_secs] \"<command>\"\n");
        printf("Examples:\n");
        printf("  watchcmd 2 \"netinfo\"\n");
        printf("  watchcmd 1 \"sysinfo\"\n");
        printf("  watchcmd \"calc 10 + 20\"\n");
        utilipc_close();
        return 1;
    }

    int interval = 2;
    const char *cmd = NULL;

    if (argc >= 3) {
        interval = atoi(argv[1]);
        if (interval <= 0) interval = 2;
        cmd = argv[2];
    } else {
        cmd = argv[1];
    }

    unsigned long iteration = 0;

    printf("\033[H\033[J");
    while (1) {
        iteration++;
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

        printf("\033[H");
        printf("\033[1;35mEvery %ds: %s\033[0m \033[0;33m[Iter: %lu | Last: %s]\033[0m\n", interval, cmd, iteration, time_str);
        printf("--------------------------------------------------\n");
        fflush(stdout);

        system(cmd);

        char log_msg[UTILIPC_MAX_MSG];
        snprintf(log_msg, sizeof(log_msg), "watchcmd: executed '%s'", cmd);
        utilipc_write_status(-1, -1, -1, log_msg);

        sleep(interval);
    }

    utilipc_close();
    return 0;
}
