#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_LABEL   "\033[1;36m"
#define COLOR_VAL     "\033[1;32m"
#define COLOR_WARN    "\033[1;33m"

static void render_ipc_dashboard(void) {
    utilipc_data_t data;
    if (utilipc_read_status(&data) < 0) {
        printf("Error: Failed to read IPC shared memory.\n");
        return;
    }

    char time_str[64] = "Never / Unset";
    if (data.last_updated > 0) {
        struct tm *tm_info = localtime(&data.last_updated);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    }

    printf("%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ utils-in-c - IPC Shared Memory Monitor ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("  %s• RAM Usage:   %s", COLOR_LABEL, COLOR_VAL);
    if (data.ram_total_mb > 0) {
        printf("%.2f MB / %.2f MB (%.1f%%)%s\n", data.ram_used_mb, data.ram_total_mb, (data.ram_used_mb / data.ram_total_mb) * 100.0, COLOR_RESET);
    } else {
        printf("N/A%s\n", COLOR_RESET);
    }

    printf("  %s• CPU Load 1m: %s", COLOR_LABEL, COLOR_VAL);
    if (data.cpu_load1 >= 0) {
        printf("%.2f%s\n", data.cpu_load1, COLOR_RESET);
    } else {
        printf("N/A%s\n", COLOR_RESET);
    }

    printf("  %s• Total Calls: %s%u%s\n", COLOR_LABEL, COLOR_VAL, data.total_ipc_calls, COLOR_RESET);
    printf("  %s• Last Action: %s%s%s\n", COLOR_LABEL, COLOR_WARN, strlen(data.last_action) > 0 ? data.last_action : "None", COLOR_RESET);
    printf("  %s• Last Update: %s%s%s\n", COLOR_LABEL, COLOR_RESET, time_str, COLOR_RESET);
    printf("%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);
}

int main(int argc, char *argv[]) {
    if (utilipc_init() < 0) {
        printf("Error: Could not initialize IPC shared memory.\n");
        return 1;
    }

    if (argc >= 2 && (strcmp(argv[1], "-w") == 0 || strcmp(argv[1], "--watch") == 0)) {
        printf("\033[H\033[J"); // Limpa a tela
        while (1) {
            printf("\033[H"); // Move o cursor para o topo
            render_ipc_dashboard();
            printf("\n[Live Mode - Press Ctrl+C to exit]\n");
            fflush(stdout);
            usleep(500000); // Atualiza a cada 500ms
        }
    } else {
        render_ipc_dashboard();
    }

    utilipc_close();
    return 0;
}
