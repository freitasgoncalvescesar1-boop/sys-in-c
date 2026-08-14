#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include "../libutilipc/utilipc.h"

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

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
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

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc >= 2 && strcmp(argv[1], "-p") == 0) {
        if (argc < 3) {
            printf("Usage: netinfo -p <host> [port]\n");
            printf("Examples:\n  netinfo -p google.com 443\n  netinfo -p 192.168.1.1\n");
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

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "netinfo: Public IP %s", public_ip);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
