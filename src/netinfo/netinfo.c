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
#include <errno.h>
#include <time.h>
#include <signal.h>
#include "../libutilipc/utilipc.h"

static void cleanup_and_exit(int sig) {
    (void)sig;
    printf("\033[?25h\033[0m\n");
    fflush(stdout);
    utilipc_close();
    exit(0);
}

static void fetch_public_ip(char *out_ip, size_t max_len) {
    strncpy(out_ip, "Offline / Unavailable", max_len);

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo("api.ipify.org", "80", &hints, &res) != 0) return;

    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        freeaddrinfo(res);
        return;
    }

    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sockfd, res->ai_addr, res->ai_addrlen) == 0) {
        const char *req = "GET / HTTP/1.1\r\nHost: api.ipify.org\r\nUser-Agent: utils-netinfo\r\nConnection: close\r\n\r\n";
        send(sockfd, req, strlen(req), 0);

        char buf[1024];
        ssize_t bytes = recv(sockfd, buf, sizeof(buf) - 1, 0);
        if (bytes > 0) {
            buf[bytes] = '\0';
            char *body = strstr(buf, "\r\n\r\n");
            if (body) {
                body += 4;
                body[strcspn(body, "\r\n")] = '\0';
                if (strlen(body) > 0 && strlen(body) < max_len) {
                    strncpy(out_ip, body, max_len);
                }
            }
        }
    }
    close(sockfd);
    freeaddrinfo(res);
}

static void print_local_interfaces(void) {
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        printf("  Failed to retrieve network interfaces.\n");
        return;
    }

    printf("  [Local Interfaces & IPs]\n");
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;

        if (ifa->ifa_addr->sa_family == AF_INET) {
            char host[INET_ADDRSTRLEN];
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &(sa->sin_addr), host, sizeof(host));

            if (strcmp(ifa->ifa_name, "lo") != 0) {
                printf("    • %-12s : %s\n", ifa->ifa_name, host);
            } else {
                printf("    • %-12s : %s (loopback)\n", ifa->ifa_name, host);
            }
        }
    }
    freeifaddrs(ifaddr);
}

static void call_portcheck(const char *target, const char *port_str) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "./portcheck %s %s 2>/dev/null || portcheck %s %s", target, port_str, target, port_str);
    system(cmd);
}

static int try_read_proc_net(unsigned long long *rx, unsigned long long *tx) {
    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) return 0;
    *rx = 0; *tx = 0;
    char line[256];
    int count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strchr(line, ':')) {
            unsigned long long r_bytes, t_bytes;
            if (sscanf(strchr(line, ':') + 1, "%llu %*u %*u %*u %*u %*u %*u %*u %llu", &r_bytes, &t_bytes) == 2) {
                *rx += r_bytes;
                *tx += t_bytes;
                count++;
            }
        }
    }
    fclose(fp);
    return count > 0;
}

static double measure_tcp_latency(const char *host, int port) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0) return -1.0;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return -1.0;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int conn = connect(fd, res->ai_addr, res->ai_addrlen);
    double rtt = -1.0;

    if (conn == 0) {
        clock_gettime(CLOCK_MONOTONIC, &end);
        rtt = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
    } else if (errno == EINPROGRESS) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 500000 };
        if (select(fd + 1, NULL, &fds, NULL, &tv) > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err == 0) {
                clock_gettime(CLOCK_MONOTONIC, &end);
                rtt = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
            }
        }
    }
    close(fd);
    freeaddrinfo(res);
    return rtt;
}

static void run_tui(void) {
    signal(SIGINT, cleanup_and_exit);
    signal(SIGTERM, cleanup_and_exit);

    printf("\033[?25l\033[H\033[J");

    char public_ip[128] = "Fetching...";
    fetch_public_ip(public_ip, sizeof(public_ip));

    const char *spark[] = {" ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    double history[20] = {0};

    unsigned long long rx_prev = 0, tx_prev = 0;
    int proc_available = try_read_proc_net(&rx_prev, &tx_prev);

    while (1) {
        printf("\033[H");
        printf("\033[1;35m==========================================\033[0m\n");
        printf("\033[1;35m[ netinfo TUI - Live Network Monitor ]\033[0m\n");
        printf("\033[1;35m==========================================\033[0m\n");
        printf("  \033[1;36m• Public IP   :\033[0m %s\033[K\n", public_ip);

        if (proc_available) {
            sleep(1);
            unsigned long long rx_now = 0, tx_now = 0;
            try_read_proc_net(&rx_now, &tx_now);
            double rx_mbps = (double)(rx_now - rx_prev) / (1024.0 * 1024.0);
            double tx_mbps = (double)(tx_now - tx_prev) / (1024.0 * 1024.0);
            rx_prev = rx_now; tx_prev = tx_now;

            for (int i = 0; i < 19; i++) history[i] = history[i+1];
            history[19] = rx_mbps;

            printf("  \033[1;32m• RX Download : %.2f MB/s\033[0m\033[K\n  ", rx_mbps);
            for (int i = 0; i < 20; i++) {
                int idx = (int)(history[i] * 4.0);
                if (idx > 8) idx = 8; if (idx < 0) idx = 0;
                printf("\033[1;32m%s\033[0m", spark[idx]);
            }
            printf("\n  \033[1;36m• TX Upload   : %.2f MB/s\033[0m\033[K\n", tx_mbps);
        } else {
            double lat = measure_tcp_latency("1.1.1.1", 53);
            if (lat < 0) lat = measure_tcp_latency("8.8.8.8", 53);

            for (int i = 0; i < 19; i++) history[i] = history[i+1];
            history[19] = (lat > 0) ? lat : 0;

            if (lat >= 0) {
                printf("  \033[1;32m• Live Latency: %.2f ms (Cloudflare DNS)\033[0m\033[K\n  ", lat);
            } else {
                printf("  \033[1;31m• Live Latency: Timeout / Offline\033[0m\033[K\n  ");
            }

            for (int i = 0; i < 20; i++) {
                int idx = (int)((history[i] / 120.0) * 8.0);
                if (idx > 8) idx = 8; if (idx < 0) idx = 0;
                printf("\033[1;32m%s\033[0m", spark[idx]);
            }
            printf("\n  \033[0;33m[Android Mode: Active Non-Root Latency Graph]\033[0m\033[K\n");
            usleep(800000);
        }

        printf("\033[1;35m==========================================\033[0m\n");
        printf("[Live Mode - Press Ctrl+C to Exit]\033[0m\033[K\n");
        fflush(stdout);
    }
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc >= 2 && strcmp(argv[1], "-tui") == 0) {
        run_tui();
        utilipc_close();
        return 0;
    }

    if (argc >= 2 && strcmp(argv[1], "-p") == 0) {
        if (argc < 3) {
            printf("Usage: netinfo -p <host> [port]\n");
            return 1;
        }
        const char *host = argv[2];
        if (argc >= 4) {
            call_portcheck(host, argv[3]);
        } else {
            printf("======================\n");
            printf("[Scanning Common Ports on %s...]\n", host);
            printf("======================\n");
            static const char *common_ports[] = {"22", "53", "80", "443", "8080", NULL};
            for (int i = 0; common_ports[i] != NULL; i++) {
                call_portcheck(host, common_ports[i]);
            }
        }
        utilipc_close();
        return 0;
    }

    printf("======================\n");
    printf("[netinfo - Network Information]\n");
    printf("======================\n");

    print_local_interfaces();

    char public_ip[128];
    fetch_public_ip(public_ip, sizeof(public_ip));
    printf("  [Public IP]          : %s\n", public_ip);
    printf("======================\n");
    printf("Tip: Run 'netinfo -tui' for live monitoring screen.\n");

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "netinfo: Public IP %s", public_ip);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
