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
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_LABEL   "\033[1;36m"
#define COLOR_VAL     "\033[1;32m"
#define COLOR_WARN    "\033[1;33m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_GRAY    "\033[0;90m"

static const char *sparklines[] = {" ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

static double get_time_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static double measure_ping(const char *host, int port) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char p_str[16];
    snprintf(p_str, sizeof(p_str), "%d", port);

    if (getaddrinfo(host, p_str, &hints, &res) != 0) return -1.0;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return -1.0;
    }

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    double t_start = get_time_sec();
    int conn = connect(fd, res->ai_addr, res->ai_addrlen);
    double rtt = -1.0;

    if (conn == 0) {
        rtt = (get_time_sec() - t_start) * 1000.0;
    } else if (errno == EINPROGRESS) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
        if (select(fd + 1, NULL, &fds, NULL, &tv) > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err == 0) {
                rtt = (get_time_sec() - t_start) * 1000.0;
            }
        }
    }
    close(fd);
    freeaddrinfo(res);
    return rtt;
}

static void run_download_test(double *out_mbps, size_t *out_bytes, double *out_time) {
    printf("\n  %s[2/2] Testando Velocidade de Download (CDN Stream)...%s\n", COLOR_LABEL, COLOR_RESET);
    fflush(stdout);

    const char *host = "speed.cloudflare.com";
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, "80", &hints, &res) != 0) {
        printf("  %sErro: Falha ao conectar ao servidor de teste.%s\n", COLOR_ERR, COLOR_RESET);
        return;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return;
    }

    struct timeval tv = { .tv_sec = 6, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        printf("  %sErro de conexão com a CDN.%s\n", COLOR_ERR, COLOR_RESET);
        close(fd);
        freeaddrinfo(res);
        return;
    }
    freeaddrinfo(res);

    // Solicita payload de 15 MB para o teste
    const char *req = "GET /__down?bytes=15000000 HTTP/1.1\r\n"
                      "Host: speed.cloudflare.com\r\n"
                      "User-Agent: utils-in-c-speedtest/1.0\r\n"
                      "Connection: close\r\n\r\n";
    send(fd, req, strlen(req), 0);

    char buf[65536];
    size_t total_received = 0;
    double t_start = get_time_sec();
    double last_update = t_start;
    double current_mbps = 0.0;
    int is_header_skipped = 0;

    while (1) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;

        if (!is_header_skipped) {
            char *body = strstr(buf, "\r\n\r\n");
            if (body) {
                size_t h_len = (body + 4) - buf;
                total_received += (n - h_len);
                is_header_skipped = 1;
            } else {
                continue;
            }
        } else {
            total_received += n;
        }

        double now = get_time_sec();
        double elapsed = now - t_start;

        if (now - last_update >= 0.15 && elapsed > 0.05) {
            last_update = now;
            current_mbps = ((double)total_received * 8.0) / (elapsed * 1000000.0);

            int spark_idx = (int)((current_mbps / 100.0) * 8.0);
            if (spark_idx > 8) spark_idx = 8; if (spark_idx < 0) spark_idx = 0;

            printf("\r    • Baixando: %s%.2f MB%s | Velocidade: %s%s %.2f Mbps%s   ",
                   COLOR_VAL, (double)total_received / (1024.0 * 1024.0), COLOR_RESET,
                   COLOR_VAL, sparklines[spark_idx], current_mbps, COLOR_RESET);
            fflush(stdout);
        }

        if (elapsed >= 5.0) break; // Limite de 5 segundos de teste
    }
    close(fd);

    double total_elapsed = get_time_sec() - t_start;
    if (total_elapsed > 0) {
        *out_mbps = ((double)total_received * 8.0) / (total_elapsed * 1000000.0);
    }
    *out_bytes = total_received;
    *out_time = total_elapsed;
    printf("\n");
}

int main(void) {
    utilipc_init();

    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ speedtest - Internet Speed & Latency Benchmark ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);

    printf("  %s[1/2] Medindo Latência e Jitter (Cloudflare 1.1.1.1)...%s\n", COLOR_LABEL, COLOR_RESET);
    fflush(stdout);

    double pings[5];
    int valid = 0;
    double ping_sum = 0.0;

    for (int i = 0; i < 5; i++) {
        double rtt = measure_ping("1.1.1.1", 80);
        if (rtt > 0) {
            pings[valid++] = rtt;
            ping_sum += rtt;
        }
        usleep(100000);
    }

    double avg_ping = (valid > 0) ? (ping_sum / valid) : 0.0;
    double jitter = (valid >= 2) ? (pings[valid - 1] > pings[0] ? pings[valid - 1] - pings[0] : pings[0] - pings[valid - 1]) : 0.0;

    if (valid > 0) {
        printf("    • Latência Média : %s%.2f ms%s\n", COLOR_VAL, avg_ping, COLOR_RESET);
        printf("    • Jitter         : %s%.2f ms%s\n", COLOR_VAL, jitter, COLOR_RESET);
    } else {
        printf("    • Latência       : %sTimeout / Sem Conexão%s\n", COLOR_ERR, COLOR_RESET);
    }

    double final_mbps = 0.0;
    size_t total_bytes = 0;
    double elapsed_time = 0.0;

    run_download_test(&final_mbps, &total_bytes, &elapsed_time);

    printf("%s--------------------------------------------------------%s\n", COLOR_GRAY, COLOR_RESET);
    printf("  %sResultado Final:%s\n", COLOR_TITLE, COLOR_RESET);
    printf("    • Velocidade de Download : %s%.2f Mbps%s\n", COLOR_VAL, final_mbps, COLOR_RESET);
    printf("    • Volume Transferido     : %.2f MB\n", (double)total_bytes / (1024.0 * 1024.0));
    printf("    • Tempo do Teste         : %.2f segundos\n", elapsed_time);
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "speedtest: %.2f Mbps | Ping: %.1fms", final_mbps, avg_ping);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
