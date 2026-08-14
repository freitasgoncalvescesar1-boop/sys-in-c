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

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 3) {
        printf("Usage: %s <host> <port>\n", argv[0]);
        printf("Example: %s google.com 443\n", argv[0]);
        utilipc_close();
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);

    if (port <= 0 || port > 65535) {
        printf("Error: Invalid port number (1-65535).\n");
        utilipc_close();
        return 1;
    }

    printf("======================\n");
    printf("[Checking %s:%d...]\n", host, port);

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[10];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        printf("[Result: HOST UNRESOLVED]\n");
        printf("======================\n");
        utilipc_close();
        return 1;
    }

    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        printf("[Result: SOCKET ERROR]\n");
        freeaddrinfo(res);
        printf("======================\n");
        utilipc_close();
        return 1;
    }

    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int conn_result = connect(sockfd, res->ai_addr, res->ai_addrlen);
    int is_open = 0;

    if (conn_result == 0) {
        is_open = 1;
    } else if (errno == EINPROGRESS) {
        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(sockfd, &fdset);

        struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };

        if (select(sockfd + 1, NULL, &fdset, NULL, &tv) > 0) {
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len);
            if (so_error == 0) is_open = 1;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double time_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;

    if (is_open) {
        printf("[Status: OPEN]\n");
        printf("[Latency: %.2f ms]\n", time_ms);
    } else {
        printf("[Status: CLOSED / TIMEOUT]\n");
    }

    close(sockfd);
    freeaddrinfo(res);
    printf("======================\n");

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "portcheck: %s:%d [%s]", host, port, is_open ? "OPEN" : "CLOSED");
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
