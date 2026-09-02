#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <poll.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET "\033[0m"
#define COLOR_OK    "\033[1;32m"
#define COLOR_ERR   "\033[1;31m"
#define COLOR_HOST  "\033[1;36m"
#define COLOR_RTT   "\033[1;33m"

#define DEFAULT_PAYLOAD_SIZE 56
#define MAX_PACKET_SIZE 4096

typedef struct {
    struct icmphdr hdr;
    struct timespec send_time;
    char payload[MAX_PACKET_SIZE];
} PingPacket;

static int g_sockfd = -1;
static volatile sig_atomic_t keep_running = 1;
static unsigned long total_tx = 0;
static unsigned long total_rx = 0;
static double rtt_min = 999999.0;
static double rtt_max = 0.0;
static double rtt_sum = 0.0;
static double rtt_sum_sq = 0.0;
static char target_host_str[256] = "";
static char target_ip_str[INET_ADDRSTRLEN] = "";
static double t_bench_start = 0;

static double get_time_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

// Checksum RFC 1071
static uint16_t calculate_checksum(const void *data, size_t len) {
    const uint16_t *buf = (const uint16_t *)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const uint8_t *)buf;
    }

    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return (uint16_t)(~sum);
}

// Sinal seguro: APENAS altera a flag atômica, evitando Deadlock de printf
static void safe_sigint_handler(int sig) {
    (void)sig;
    keep_running = 0;
}

static void print_summary(void) {
    double total_time_ms = (get_time_sec() - t_bench_start) * 1000.0;
    double loss_pct = (total_tx > 0) ? ((double)(total_tx - total_rx) / (double)total_tx) * 100.0 : 0.0;

    printf("\n--- %s%s%s ping statistics ---\n", COLOR_HOST, target_host_str, COLOR_RESET);
    printf("  %lu packets transmitted, %lu received, %s%.1f%% packet loss%s, time %.0f ms\n",
           total_tx, total_rx,
           (loss_pct > 0.0) ? COLOR_ERR : COLOR_OK, loss_pct, COLOR_RESET,
           total_time_ms);

    if (total_rx > 0) {
        double rtt_avg = rtt_sum / total_rx;
        double variance = (rtt_sum_sq / total_rx) - (rtt_avg * rtt_avg);
        double rtt_mdev = (variance > 0.0) ? sqrt(variance) : 0.0;

        printf("  rtt min/avg/max/mdev = %s%.3f%s/%s%.3f%s/%s%.3f%s/%s%.3f%s ms\n\n",
               COLOR_RTT, rtt_min, COLOR_RESET,
               COLOR_RTT, rtt_avg, COLOR_RESET,
               COLOR_RTT, rtt_max, COLOR_RESET,
               COLOR_RTT, rtt_mdev, COLOR_RESET);
    } else {
        printf("\n");
    }
}

static void print_help(void) {
    low_print_banner("ping");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./ping [OPTIONS] <HOST>\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Send ICMP ECHO_REQUEST packets to network hosts (Default: 5 pings max).\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-c <COUNT>%s           Number of packets to send (0 = infinite) [Default: 5]\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-i <INTERVAL>%s        Wait INTERVAL seconds between sending packets [Default: 1.0s]\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-W <TIMEOUT>%s         Time to wait for a response in seconds [Default: 2.0s]\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-s <SIZE>%s            Payload data size in bytes [Default: 56]\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s           Display this formatted help guide and exit\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --version%s        Display version and repository information\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./ping 1.1.1.1%s                 (Envia 5 pings padrao e sai sozinho)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./ping google.com -c 0%s         (Ping continuo infinito)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./ping 192.168.1.1 -c 10 -i 0.2%s (10 pings rapidos de 200ms)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 1;
    }

    int count_limit = 5; // Padrão: 5 pings automáticos!
    double interval_sec = 1.0;
    double timeout_sec = 2.0;
    size_t payload_sz = DEFAULT_PAYLOAD_SIZE;
    const char *host_arg = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            print_help();
            return 0;
        }

        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            count_limit = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            interval_sec = atof(argv[++i]);
            if (interval_sec < 0.01) interval_sec = 0.01;
        } else if (strcmp(argv[i], "-W") == 0 && i + 1 < argc) {
            timeout_sec = atof(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            payload_sz = (size_t)atoi(argv[++i]);
            if (payload_sz > MAX_PACKET_SIZE - sizeof(struct icmphdr) - sizeof(struct timespec)) {
                payload_sz = MAX_PACKET_SIZE - sizeof(struct icmphdr) - sizeof(struct timespec);
            }
        } else if (!host_arg) {
            host_arg = argv[i];
        }
    }

    if (!host_arg) {
        print_help();
        return 1;
    }

    strncpy(target_host_str, host_arg, sizeof(target_host_str) - 1);

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_RAW;

    if (getaddrinfo(host_arg, NULL, &hints, &res) != 0) {
        fprintf(stderr, "ping: host desconhecido '%s'\n", host_arg);
        return 1;
    }

    struct sockaddr_in dest_addr = *(struct sockaddr_in *)res->ai_addr;
    inet_ntop(AF_INET, &dest_addr.sin_addr, target_ip_str, sizeof(target_ip_str));
    freeaddrinfo(res);

    int is_dgram = 0;
    g_sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (g_sockfd < 0) {
        g_sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
        if (g_sockfd < 0) {
            fprintf(stderr, "ping: erro ao abrir socket ICMP: %s\n", strerror(errno));
            return 1;
        }
        is_dgram = 1;
    }

    // Configuração de Sinal Anti-Deadlock
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = safe_sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    pid_t ident = getpid() & 0xFFFF;
    size_t packet_len = sizeof(struct icmphdr) + sizeof(struct timespec) + payload_sz;

    printf("PING %s%s%s (%s%s%s) %zu(%zu) bytes of data. [Max: %d pings]\n",
           COLOR_HOST, target_host_str, COLOR_RESET,
           COLOR_HOST, target_ip_str, COLOR_RESET,
           payload_sz, packet_len + (is_dgram ? 0 : 20),
           count_limit > 0 ? count_limit : 9999);

    t_bench_start = get_time_sec();
    uint16_t seq = 1;

    while (keep_running) {
        if (count_limit > 0 && total_tx >= (unsigned long)count_limit) break;

        PingPacket pkt;
        memset(&pkt, 0, sizeof(pkt));

        pkt.hdr.type = ICMP_ECHO;
        pkt.hdr.code = 0;
        pkt.hdr.un.echo.id = htons(ident);
        pkt.hdr.un.echo.sequence = htons(seq);
        clock_gettime(CLOCK_MONOTONIC, &pkt.send_time);

        for (size_t k = 0; k < payload_sz; k++) {
            pkt.payload[k] = (char)(k & 0xFF);
        }

        if (!is_dgram) {
            pkt.hdr.checksum = calculate_checksum(&pkt, packet_len);
        }

        double t_send = get_time_sec();
        ssize_t sent = sendto(g_sockfd, &pkt, packet_len, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (sent < 0) {
            if (errno != EINTR) fprintf(stderr, "ping: sendto: %s\n", strerror(errno));
        } else {
            total_tx++;
        }

        // Aguarda resposta via poll com pequenos blocos de 30ms para verificar interrupção
        int reply_received = 0;
        double deadline = get_time_sec() + timeout_sec;

        while (keep_running && get_time_sec() < deadline && !reply_received) {
            struct pollfd pfd;
            pfd.fd = g_sockfd;
            pfd.events = POLLIN;

            int poll_ret = poll(&pfd, 1, 30);
            if (poll_ret > 0 && (pfd.revents & POLLIN)) {
                unsigned char recv_buf[MAX_PACKET_SIZE + sizeof(struct iphdr)];
                struct sockaddr_in reply_addr;
                socklen_t addr_len = sizeof(reply_addr);

                ssize_t recvd = recvfrom(g_sockfd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&reply_addr, &addr_len);
                double t_recv = get_time_sec();

                if (recvd > 0) {
                    struct icmphdr *r_icmp = NULL;
                    uint8_t ttl = 64;

                    if (is_dgram) {
                        r_icmp = (struct icmphdr *)recv_buf;
                    } else {
                        struct iphdr *ip = (struct iphdr *)recv_buf;
                        size_t ip_hdr_len = ip->ihl * 4;
                        ttl = ip->ttl;
                        if (recvd >= (ssize_t)(ip_hdr_len + sizeof(struct icmphdr))) {
                            r_icmp = (struct icmphdr *)(recv_buf + ip_hdr_len);
                        }
                    }

                    if (r_icmp && r_icmp->type == ICMP_ECHOREPLY) {
                        double rtt_ms = (t_recv - t_send) * 1000.0;
                        total_rx++;
                        rtt_sum += rtt_ms;
                        rtt_sum_sq += (rtt_ms * rtt_ms);
                        if (rtt_ms < rtt_min) rtt_min = rtt_ms;
                        if (rtt_ms > rtt_max) rtt_max = rtt_ms;

                        char reply_ip[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &reply_addr.sin_addr, reply_ip, sizeof(reply_ip));

                        printf("%zd bytes from %s%s%s: icmp_seq=%u ttl=%u time=%s%.2f ms%s\n",
                               recvd - (is_dgram ? 0 : 20),
                               COLOR_HOST, reply_ip, COLOR_RESET,
                               seq, ttl,
                               COLOR_RTT, rtt_ms, COLOR_RESET);
                        reply_received = 1;
                    }
                }
            }
        }

        if (!reply_received && keep_running) {
            printf("Request timeout for icmp_seq %u\n", seq);
        }

        seq++;

        if (count_limit > 0 && total_tx >= (unsigned long)count_limit) break;

        // Intervalo entre pacotes em ciclos de 20ms (interrompe instantaneamente no Ctrl+C)
        double next_tx = get_time_sec() + interval_sec;
        while (keep_running && get_time_sec() < next_tx) {
            usleep(20000); // 20ms
        }
    }

    if (g_sockfd >= 0) {
        close(g_sockfd);
        g_sockfd = -1;
    }

    print_summary();
    return (total_rx > 0) ? 0 : 1;
}
