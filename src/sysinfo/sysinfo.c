#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include "../libutilipc/utilipc.h"

static void detect_os(char *os_buf, size_t buf_size) {
    FILE *fp = fopen("/etc/os-release", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                char *val = line + 12;
                if (*val == '"') val++;
                size_t len = strlen(val);
                if (len > 0 && val[len - 1] == '\n') val[--len] = '\0';
                if (len > 0 && val[len - 1] == '"') val[--len] = '\0';
                snprintf(os_buf, buf_size, "%s", val);
                fclose(fp);
                return;
            }
        }
        fclose(fp);
    }

    if (getenv("ANDROID_ROOT") != NULL || getenv("TERMUX_VERSION") != NULL ||
        access("/system/bin/app_process", F_OK) == 0) {
        snprintf(os_buf, buf_size, "Android (Linux Environment / Termux)");
        return;
    }

    snprintf(os_buf, buf_size, "Linux (Unknown Distribution)");
}

int main(void) {
    utilipc_init();

    struct utsname sys_info;
    uname(&sys_info);

    char os_name[256];
    detect_os(os_name, sizeof(os_name));

    double uptime_secs = 0;
    FILE *fp = fopen("/proc/uptime", "r");
    if (fp) {
        if (fscanf(fp, "%lf", &uptime_secs) != 1) uptime_secs = 0;
        fclose(fp);
    }

    int days = (int)uptime_secs / 86400;
    int hours = ((int)uptime_secs % 86400) / 3600;
    int mins = ((int)uptime_secs % 3600) / 60;

    long total_ram_kb = 0, free_ram_kb = 0, avail_ram_kb = 0;
    fp = fopen("/proc/meminfo", "r");
    if (fp) {
        char label[64];
        long val;
        while (fscanf(fp, "%63s %ld kB", label, &val) == 2) {
            if (strcmp(label, "MemTotal:") == 0) total_ram_kb = val;
            else if (strcmp(label, "MemFree:") == 0) free_ram_kb = val;
            else if (strcmp(label, "MemAvailable:") == 0) avail_ram_kb = val;
        }
        fclose(fp);
    }

    if (avail_ram_kb == 0) avail_ram_kb = free_ram_kb;
    double used_ram_mb = (double)(total_ram_kb - avail_ram_kb) / 1024.0;
    double total_ram_mb = (double)total_ram_kb / 1024.0;

    double load1 = 0, load5 = 0, load15 = 0;
    fp = fopen("/proc/loadavg", "r");
    if (fp) {
        if (fscanf(fp, "%lf %lf %lf", &load1, &load5, &load15) != 3) {
            load1 = load5 = load15 = 0.0;
        }
        fclose(fp);
    }

    printf("======================\n");
    printf("[System Information]\n");
    printf("======================\n");
    printf("  OS:          %s\n", os_name);
    printf("  Kernel:      %s %s\n", sys_info.sysname, sys_info.release);
    printf("  Arch:        %s\n", sys_info.machine);
    printf("  Uptime:      %dd %dh %dm\n", days, hours, mins);
    printf("  RAM Usage:   %.2f MB / %.2f MB\n", used_ram_mb, total_ram_mb);
    printf("  CPU Load:    %.2f (1m), %.2f (5m), %.2f (15m)\n", load1, load5, load15);
    printf("======================\n");

    // Escreve as métricas no IPC
    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "sysinfo: updated metrics (%s)", os_name);
    utilipc_write_status(used_ram_mb, total_ram_mb, load1, log_msg);

    utilipc_close();
    return 0;
}
