#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../libutilipc/utilipc.h"

#define SAMPLES 32
#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_CPU     "\033[1;32m"
#define COLOR_RAM     "\033[1;36m"

static const char *sparklines[] = {" ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

static double cpu_history[SAMPLES] = {0};
static double ram_history[SAMPLES] = {0};
static int sample_count = 0;

static unsigned long long prev_idle = 0;
static unsigned long long prev_total = 0;

static double get_cpu_load(void) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 0.0;

    char line[256];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return 0.0;
    }
    fclose(fp);

    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    if (sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) < 4) {
        return 0.0;
    }

    unsigned long long current_idle = idle + iowait;
    unsigned long long current_total = user + nice + system + idle + iowait + irq + softirq + steal;

    unsigned long long total_delta = current_total - prev_total;
    unsigned long long idle_delta = current_idle - prev_idle;

    prev_total = current_total;
    prev_idle = current_idle;

    if (total_delta == 0) return 0.0;

    double load = (1.0 - ((double)idle_delta / (double)total_delta)) * 100.0;
    if (load < 0.0) load = 0.0;
    if (load > 100.0) load = 100.0;

    return load;
}

static double get_ram_load(double *out_used_mb, double *out_total_mb) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return 0.0;

    long total_kb = 0, avail_kb = 0, free_kb = 0;
    char label[64];
    long val;

    while (fscanf(fp, "%63s %ld kB", label, &val) == 2) {
        if (strcmp(label, "MemTotal:") == 0) total_kb = val;
        else if (strcmp(label, "MemFree:") == 0) free_kb = val;
        else if (strcmp(label, "MemAvailable:") == 0) avail_kb = val;
    }
    fclose(fp);

    if (avail_kb == 0) avail_kb = free_kb;
    if (total_kb <= 0) return 0.0;

    long used_kb = total_kb - avail_kb;
    if (out_used_mb) *out_used_mb = (double)used_kb / 1024.0;
    if (out_total_mb) *out_total_mb = (double)total_kb / 1024.0;

    return ((double)used_kb / (double)total_kb) * 100.0;
}

static void push_sample(double cpu, double ram) {
    if (sample_count < SAMPLES) {
        cpu_history[sample_count] = cpu;
        ram_history[sample_count] = ram;
        sample_count++;
    } else {
        for (int i = 0; i < SAMPLES - 1; i++) {
            cpu_history[i] = cpu_history[i + 1];
            ram_history[i] = ram_history[i + 1];
        }
        cpu_history[SAMPLES - 1] = cpu;
        ram_history[SAMPLES - 1] = ram;
    }
}

static const char *val_to_spark(double val_pct) {
    int idx = (int)((val_pct / 100.0) * 8.0);
    if (idx < 0) idx = 0;
    if (idx > 8) idx = 8;
    return sparklines[idx];
}

static void render_plot(void) {
    double used_mb = 0, total_mb = 0;
    double current_cpu = get_cpu_load();
    double current_ram = get_ram_load(&used_mb, &total_mb);

    push_sample(current_cpu, current_ram);

    /* Output redirection hook - Redirect output here */
    /* Generic output stream */
    printf("\033[H");
    printf("%s==========================================%s\033[K\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ cpuplot - Live System Load Graph ]%s\033[K\n", COLOR_TITLE, COLOR_RESET);
    printf("%s==========================================%s\033[K\n", COLOR_TITLE, COLOR_RESET);

    printf("  %sCPU [%05.1f%%]:%s ", COLOR_CPU, current_cpu, COLOR_RESET);
    for (int i = 0; i < sample_count; i++) {
        printf("%s%s%s", COLOR_CPU, val_to_spark(cpu_history[i]), COLOR_RESET);
    }
    printf("\033[K\n");

    printf("  %sRAM [%05.1f%%]:%s ", COLOR_RAM, current_ram, COLOR_RESET);
    for (int i = 0; i < sample_count; i++) {
        printf("%s%s%s", COLOR_RAM, val_to_spark(ram_history[i]), COLOR_RESET);
    }
    printf("\033[K\n");

    printf("------------------------------------------\033[K\n");
    printf("  • RAM Usage : %.1f MB / %.1f MB\033[K\n", used_mb, total_mb);
    printf("%s==========================================%s\033[K\n", COLOR_TITLE, COLOR_RESET);
    printf("[Live Graph - Refresh: 500ms | Ctrl+C to Exit]\033[K\n");
    fflush(stdout);

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "cpuplot: CPU %.1f%% | RAM %.1f%%", current_cpu, current_ram);
    utilipc_write_status(used_mb, total_mb, current_cpu / 100.0, log_msg);
}

int main(void) {
    utilipc_init();

    get_cpu_load();
    usleep(100000);

    printf("\033[H\033[J");
    while (1) {
        render_plot();
        usleep(500000);
    }

    utilipc_close();
    return 0;
}
