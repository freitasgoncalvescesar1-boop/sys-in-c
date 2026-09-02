#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include "../libutilipc/utilipc.h"

#define SAMPLES 26
#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_CPU     "\033[1;32m"
#define COLOR_RAM     "\033[1;36m"
#define COLOR_SWAP    "\033[1;33m"
#define COLOR_NET     "\033[1;34m"
#define COLOR_GRAY    "\033[0;90m"

static const char *sparklines[] = {" ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

static double cpu_history[SAMPLES] = {0};
static double ram_history[SAMPLES] = {0};
static double swap_history[SAMPLES] = {0};
static double net_history[SAMPLES] = {0};
static int sample_count = 0;

static unsigned long long prev_idle = 0;
static unsigned long long prev_total = 0;

static void cleanup_and_exit(int sig) {
    (void)sig;
    printf("\033[?25h\033[0m\n"); // Restaura o cursor e cores
    fflush(stdout);
    utilipc_close();
    exit(0);
}

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

static double get_ram_and_swap(double *out_used_mb, double *out_total_mb, double *out_swap_used_mb, double *out_swap_total_mb) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return 0.0;

    long total_kb = 0, avail_kb = 0, free_kb = 0;
    long swap_total_kb = 0, swap_free_kb = 0;
    char label[64];
    long val;

    while (fscanf(fp, "%63s %ld kB", label, &val) == 2) {
        if (strcmp(label, "MemTotal:") == 0) total_kb = val;
        else if (strcmp(label, "MemFree:") == 0) free_kb = val;
        else if (strcmp(label, "MemAvailable:") == 0) avail_kb = val;
        else if (strcmp(label, "SwapTotal:") == 0) swap_total_kb = val;
        else if (strcmp(label, "SwapFree:") == 0) swap_free_kb = val;
    }
    fclose(fp);

    if (avail_kb == 0) avail_kb = free_kb;

    long used_kb = (total_kb > avail_kb) ? (total_kb - avail_kb) : 0;
    long swap_used_kb = (swap_total_kb > swap_free_kb) ? (swap_total_kb - swap_free_kb) : 0;

    if (out_used_mb) *out_used_mb = (double)used_kb / 1024.0;
    if (out_total_mb) *out_total_mb = (double)total_kb / 1024.0;
    if (out_swap_used_mb) *out_swap_used_mb = (double)swap_used_kb / 1024.0;
    if (out_swap_total_mb) *out_swap_total_mb = (double)swap_total_kb / 1024.0;

    return (total_kb > 0) ? (((double)used_kb / (double)total_kb) * 100.0) : 0.0;
}

static double measure_tcp_latency(const char *host, int port) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char p_str[10]; snprintf(p_str, sizeof(p_str), "%d", port);

    if (getaddrinfo(host, p_str, &hints, &res) != 0) return -1.0;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return -1.0; }

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int conn = connect(fd, res->ai_addr, res->ai_addrlen);
    double rtt = -1.0;

    if (conn == 0) {
        clock_gettime(CLOCK_MONOTONIC, &end);
        rtt = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1e6;
    } else if (errno == EINPROGRESS) {
        fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 400000 };
        if (select(fd + 1, NULL, &fds, NULL, &tv) > 0) {
            int err = 0; socklen_t len = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err == 0) {
                clock_gettime(CLOCK_MONOTONIC, &end);
                rtt = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1e6;
            }
        }
    }
    close(fd); freeaddrinfo(res);
    return rtt;
}

static void push_samples(double cpu, double ram, double swap, double net) {
    if (sample_count < SAMPLES) {
        cpu_history[sample_count] = cpu;
        ram_history[sample_count] = ram;
        swap_history[sample_count] = swap;
        net_history[sample_count] = net;
        sample_count++;
    } else {
        for (int i = 0; i < SAMPLES - 1; i++) {
            cpu_history[i] = cpu_history[i + 1];
            ram_history[i] = ram_history[i + 1];
            swap_history[i] = swap_history[i + 1];
            net_history[i] = net_history[i + 1];
        }
        cpu_history[SAMPLES - 1] = cpu;
        ram_history[SAMPLES - 1] = ram;
        swap_history[SAMPLES - 1] = swap;
        net_history[SAMPLES - 1] = net;
    }
}

static const char *val_to_spark(double val_pct) {
    int idx = (int)((val_pct / 100.0) * 8.0);
    if (idx < 0) idx = 0;
    if (idx > 8) idx = 8;
    return sparklines[idx];
}

static void render_plot(void) {
    double used_mb = 0, total_mb = 0, swap_used_mb = 0, swap_total_mb = 0;
    double current_cpu = get_cpu_load();
    double current_ram = get_ram_and_swap(&used_mb, &total_mb, &swap_used_mb, &swap_total_mb);
    double current_swap = (swap_total_mb > 0) ? ((swap_used_mb / swap_total_mb) * 100.0) : 0.0;

    double net_lat = measure_tcp_latency("1.1.1.1", 53);
    if (net_lat < 0) net_lat = measure_tcp_latency("8.8.8.8", 53);
    double net_pct = (net_lat >= 0) ? ((net_lat / 150.0) * 100.0) : 0.0;
    if (net_pct > 100.0) net_pct = 100.0;

    push_samples(current_cpu, current_ram, current_swap, net_pct);

    int cpu_cores = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_cores <= 0) cpu_cores = 1;

    double uptime_secs = 0;
    FILE *fp = fopen("/proc/uptime", "r");
    if (fp) {
        if (fscanf(fp, "%lf", &uptime_secs) != 1) uptime_secs = 0;
        fclose(fp);
    }
    int days = (int)uptime_secs / 86400;
    int hours = ((int)uptime_secs % 86400) / 3600;
    int mins = ((int)uptime_secs % 3600) / 60;
    int secs = (int)uptime_secs % 60;

    double l1 = 0, l5 = 0, l15 = 0;
    char threads_str[32] = "N/A";
    fp = fopen("/proc/loadavg", "r");
    if (fp) {
        char proc_field[32];
        if (fscanf(fp, "%lf %lf %lf %31s", &l1, &l5, &l15, proc_field) == 4) {
            strncpy(threads_str, proc_field, sizeof(threads_str) - 1);
        }
        fclose(fp);
    }

    printf("\033[H");
    printf("%s========================================================%s\033[K\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ cpuplot - Real-Time System Dashboard ]%s\033[K\n", COLOR_TITLE, COLOR_RESET);
    printf("%s========================================================%s\033[K\n", COLOR_TITLE, COLOR_RESET);

    printf("  %sCPU [%05.1f%%] (%d Cores):%s ", COLOR_CPU, current_cpu, cpu_cores, COLOR_RESET);
    for (int i = 0; i < sample_count; i++) printf("%s%s%s", COLOR_CPU, val_to_spark(cpu_history[i]), COLOR_RESET);
    printf("\033[K\n");

    printf("  %sRAM [%05.1f%%] (%.1fG/%.1fG):%s ", COLOR_RAM, current_ram, used_mb / 1024.0, total_mb / 1024.0, COLOR_RESET);
    for (int i = 0; i < sample_count; i++) printf("%s%s%s", COLOR_RAM, val_to_spark(ram_history[i]), COLOR_RESET);
    printf("\033[K\n");

    printf("  %sSWP [%05.1f%%] (%.1fG/%.1fG):%s ", COLOR_SWAP, current_swap, swap_used_mb / 1024.0, swap_total_mb / 1024.0, COLOR_RESET);
    for (int i = 0; i < sample_count; i++) printf("%s%s%s", COLOR_SWAP, val_to_spark(swap_history[i]), COLOR_RESET);
    printf("\033[K\n");

    if (net_lat >= 0) {
        printf("  %sNET [%05.1f ms] (Cloudflare):%s ", COLOR_NET, net_lat, COLOR_RESET);
    } else {
        printf("  %sNET [TIMEOUT ] (Cloudflare):%s ", COLOR_NET, COLOR_RESET);
    }
    for (int i = 0; i < sample_count; i++) printf("%s%s%s", COLOR_NET, val_to_spark(net_history[i]), COLOR_RESET);
    printf("\033[K\n");

    printf("%s--------------------------------------------------------%s\033[K\n", COLOR_GRAY, COLOR_RESET);
    printf("  • Uptime   : %dd %02dh %02dm %02ds\033[K\n", days, hours, mins, secs);
    printf("  • Load Avg : %.2f (1m), %.2f (5m), %.2f (15m)\033[K\n", l1, l5, l15);
    printf("  • Tasks    : %s (Active / Total)\033[K\n", threads_str);
    printf("%s========================================================%s\033[K\n", COLOR_TITLE, COLOR_RESET);
    printf("[Live Graph - Refresh: 500ms | Ctrl+C to Exit]\033[K\n");
    fflush(stdout);

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "cpuplot: CPU %.1f%% | RAM %.1f%% | Net %.1fms", current_cpu, current_ram, net_lat);
    utilipc_write_status(used_mb, total_mb, current_cpu / 100.0, log_msg);
}

int main(int argc, char *argv[]) {
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        printf("Usage: cpuplot\n");
        printf("Real-time terminal graph monitor for CPU, RAM, SWAP and Network latency.\n");
        return 0;
    }

    signal(SIGINT, cleanup_and_exit);
    signal(SIGTERM, cleanup_and_exit);
    atexit((void (*)(void))cleanup_and_exit);

    utilipc_init();

    get_cpu_load();
    usleep(100000);

    printf("\033[?25l\033[H\033[J");
    while (1) {
        render_plot();
        usleep(500000);
    }

    utilipc_close();
    return 0;
}
