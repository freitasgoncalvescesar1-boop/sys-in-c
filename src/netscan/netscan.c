#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_IP      "\033[1;36m"
#define COLOR_PORTS   "\033[1;33m"
#define COLOR_TYPE    "\033[1;35m"
#define COLOR_MUTED   "\033[0;90m"

#define MAX_HOSTS 256
#define THREAD_POOL_SIZE 48

static const int probe_ports[] = {80, 443, 22, 53, 8080, 5353, 445, 0};

typedef struct {
    char ip[16];
    double latency_ms;
    int is_alive;
    char hostname[64];
    char open_ports_str[64];
    char device_type[64];
} HostResult;

static HostResult results[MAX_HOSTS];
static int total_probed = 0;
static pthread_mutex_t progress_lock = PTHREAD_MUTEX_INITIALIZER;

static double get_time_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static void print_help(void) {
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ netscan - Advanced Local Network & Wi-Fi Radar ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  netscan                       (Varre a sub-rede Wi-Fi local automaticamente)\n");
    printf("  netscan <SUBNET_BASE>         (Ex: netscan 192.168.0)\n");
    printf("  netscan --help                (Exibe esta ajuda)\n\n");
    printf("Recursos:\n");
    printf("  • Varredura multithread paralela em ~2 segundos\n");
    printf("  • Deteccao de Portas Abertas (80, 443, 22, 53, 8080, 5353, 445)\n");
    printf("  • Estimativa de Tipo de Aparelho (Roteador, Celular, PC/Servidor, IoT)\n");
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

static int probe_tcp_port(const char *ip, int port, double *out_rtt) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    double t_start = get_time_sec();

    int conn = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    int is_open = 0;

    if (conn == 0) {
        is_open = 1;
        *out_rtt = (get_time_sec() - t_start) * 1000.0;
    } else if (errno == EINPROGRESS) {
        fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 300000 }; // 300ms timeout
        if (select(fd + 1, NULL, &fds, NULL, &tv) > 0) {
            int err = 0; socklen_t len = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err == 0) {
                is_open = 1;
                *out_rtt = (get_time_sec() - t_start) * 1000.0;
            }
        }
    }
    close(fd);
    return is_open;
}

typedef struct {
    int start_idx;
    int end_idx;
    char base_ip[32];
} ScanTask;

static void *scanner_thread(void *arg) {
    ScanTask *task = (ScanTask *)arg;

    for (int i = task->start_idx; i <= task->end_idx; i++) {
        char target_ip[32];
        snprintf(target_ip, sizeof(target_ip), "%s.%d", task->base_ip, i);

        strncpy(results[i].ip, target_ip, 15);
        results[i].is_alive = 0;
        results[i].latency_ms = 0.0;
        results[i].open_ports_str[0] = '\0';
        strcpy(results[i].device_type, "Dispositivo Generico");
        strcpy(results[i].hostname, "-");

        int ports_found[8];
        int p_cnt = 0;
        double best_rtt = 999.0;

        for (int p = 0; probe_ports[p] != 0; p++) {
            double rtt = 0;
            if (probe_tcp_port(target_ip, probe_ports[p], &rtt)) {
                ports_found[p_cnt++] = probe_ports[p];
                if (rtt < best_rtt) best_rtt = rtt;
            }
        }

        if (p_cnt > 0) {
            results[i].is_alive = 1;
            results[i].latency_ms = best_rtt;

            char p_buf[64] = "";
            for (int k = 0; k < p_cnt; k++) {
                char tmp[10]; snprintf(tmp, sizeof(tmp), "%d%s", ports_found[k], (k < p_cnt - 1) ? "," : "");
                strcat(p_buf, tmp);
            }
            strncpy(results[i].open_ports_str, p_buf, sizeof(results[i].open_ports_str) - 1);

            // Inferência do Tipo de Aparelho
            if (strstr(p_buf, "53") || i == 1) strcpy(results[i].device_type, "Roteador / Gateway");
            else if (strstr(p_buf, "22")) strcpy(results[i].device_type, "Linux / Servidor SSH");
            else if (strstr(p_buf, "445")) strcpy(results[i].device_type, "Windows PC / Samba");
            else if (strstr(p_buf, "5353")) strcpy(results[i].device_type, "Apple / Google Cast / IoT");
            else if (strstr(p_buf, "80") || strstr(p_buf, "443") || strstr(p_buf, "8080")) strcpy(results[i].device_type, "Web Server / Camera IP");
            else strcpy(results[i].device_type, "Smartphone / Host");

            // Resolução de Hostname
            struct sockaddr_in sa;
            memset(&sa, 0, sizeof(sa));
            sa.sin_family = AF_INET;
            inet_pton(AF_INET, target_ip, &sa.sin_addr);
            char hbuf[64];
            if (getnameinfo((struct sockaddr *)&sa, sizeof(sa), hbuf, sizeof(hbuf), NULL, 0, NI_NAMEREQD) == 0) {
                strncpy(results[i].hostname, hbuf, sizeof(results[i].hostname) - 1);
            }
        }

        pthread_mutex_lock(&progress_lock);
        total_probed++;
        printf("\r  Escaneando sub-rede: \033[1;32m%d%%\033[0m (%d/254 hosts) ",
               (total_probed * 100) / 254, total_probed);
        fflush(stdout);
        pthread_mutex_unlock(&progress_lock);
    }
    return NULL;
}

static int get_local_subnet_base(char *base_out, size_t sz) {
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) return -1;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, "lo") == 0) continue;

        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        char host[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(sa->sin_addr), host, sizeof(host));

        char *last_dot = strrchr(host, '.');
        if (last_dot) {
            *last_dot = '\0';
            snprintf(base_out, sz, "%s", host);
            freeifaddrs(ifaddr);
            return 0;
        }
    }
    freeifaddrs(ifaddr);
    return -1;
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_help();
        utilipc_close();
        return 0;
    }

    char base_ip[32] = "";
    if (argc >= 2) {
        strncpy(base_ip, argv[1], sizeof(base_ip) - 1);
        char *d = strrchr(base_ip, '.');
        if (d && *(d + 1) != '\0' && atoi(d + 1) == 0) *d = '\0';
    } else {
        if (get_local_subnet_base(base_ip, sizeof(base_ip)) < 0) {
            strcpy(base_ip, "192.168.1");
        }
    }

    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ netscan - Radar de Dispositivos Wi-Fi & LAN (Multithread 48 workers) ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("  • Sub-rede Alvo : \033[1;36m%s.1 ate %s.254\033[0m\n\n", base_ip, base_ip);

    double t_start = get_time_sec();
    pthread_t threads[THREAD_POOL_SIZE];
    ScanTask tasks[THREAD_POOL_SIZE];
    int chunk = 254 / THREAD_POOL_SIZE;

    for (int t = 0; t < THREAD_POOL_SIZE; t++) {
        tasks[t].start_idx = t * chunk + 1;
        tasks[t].end_idx = (t == THREAD_POOL_SIZE - 1) ? 254 : (t + 1) * chunk;
        strncpy(tasks[t].base_ip, base_ip, sizeof(tasks[t].base_ip) - 1);
        pthread_create(&threads[t], NULL, scanner_thread, &tasks[t]);
    }

    for (int t = 0; t < THREAD_POOL_SIZE; t++) {
        pthread_join(threads[t], NULL);
    }

    double elapsed = get_time_sec() - t_start;
    printf("\n\n  %-16s %-9s %-16s %-15s %s\n", "IP ADDRESS", "LATENCY", "OPEN PORTS", "TIPO ESTIMADO", "HOSTNAME");
    printf("  ---------------------------------------------------------------------------------\n");

    int found_count = 0;
    for (int i = 1; i <= 254; i++) {
        if (results[i].is_alive) {
            found_count++;
            printf("  %s%-16s%s %s%6.1f ms%s %s%-16.16s%s %s%-15.15s%s %s%s%s\n",
                   COLOR_IP, results[i].ip, COLOR_RESET,
                   COLOR_OK, results[i].latency_ms, COLOR_RESET,
                   COLOR_PORTS, results[i].open_ports_str, COLOR_RESET,
                   COLOR_TYPE, results[i].device_type, COLOR_RESET,
                   COLOR_MUTED, results[i].hostname, COLOR_RESET);
        }
    }

    printf("  ---------------------------------------------------------------------------------\n");
    printf("  \033[1;32m✔ Varredura concluida em %.2f segundos! Dispositivos ativos encontrados: %d\033[0m\n", elapsed, found_count);
    printf("%s=================================================================================%s\n\n", COLOR_TITLE, COLOR_RESET);

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "netscan: found %d devices in %s.0/24 (%.2fs)", found_count, base_ip, elapsed);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
