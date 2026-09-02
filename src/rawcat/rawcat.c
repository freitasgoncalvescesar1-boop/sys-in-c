#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET "\033[0m"
#define COLOR_TITLE "\033[1;35m"
#define COLOR_OK    "\033[1;32m"
#define COLOR_ERR   "\033[1;31m"
#define COLOR_TAG   "\033[1;33m"

#define BUF_SZ 8192

static void print_help(void) {
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ rawcat - Bidirectional Raw TCP/UDP Stream Socket ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  rawcat <HOST> <PORT>             (Modo Cliente TCP: Conecta em um socket)\n");
    printf("  rawcat -l <PORT>                 (Modo Servidor TCP: Escuta em uma porta)\n");
    printf("  rawcat -u <HOST> <PORT>          (Modo UDP)\n");
    printf("  rawcat --help                    (Exibe esta ajuda)\n\n");
    printf("Exemplos:\n");
    printf("  printf \"GET / HTTP/1.0\\r\\n\\r\\n\" | rawcat example.com 80\n");
    printf("  rawcat -l 9999                   (Abre porta 9999 e recebe dados)\n");
    printf("  cat backup.tar | rawcat 192.168.1.50 9999\n");
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

static void bridge_streams(int sock_fd) {
    char buf[BUF_SZ];
    fd_set fds;

    while (1) {
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(sock_fd, &fds);

        int max_fd = (STDIN_FILENO > sock_fd) ? STDIN_FILENO : sock_fd;
        int sel = select(max_fd + 1, &fds, NULL, NULL, NULL);
        if (sel < 0) break;

        // Dados do Teclado/Pipe -> Envia para o Socket
        if (FD_ISSET(STDIN_FILENO, &fds)) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) {
                // Fechou STDIN, envia shutdown de escrita no socket
                shutdown(sock_fd, SHUT_WR);
            } else {
                send(sock_fd, buf, n, 0);
            }
        }

        // Dados do Socket -> Envia para STDOUT
        if (FD_ISSET(sock_fd, &fds)) {
            ssize_t n = recv(sock_fd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            write(STDOUT_FILENO, buf, n);
        }
    }
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help();
        utilipc_close();
        return 0;
    }

    int listen_mode = 0;
    int udp_mode = 0;
    const char *host = NULL;
    int port = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            listen_mode = 1;
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-u") == 0) {
            udp_mode = 1;
        } else if (!host) {
            host = argv[i];
        } else if (port == 0) {
            port = atoi(argv[i]);
        }
    }

    if (listen_mode) {
        if (port <= 0) { fprintf(stderr, "rawcat: porta invalida\n"); return 1; }

        int sfd = socket(AF_INET, SOCK_STREAM, 0);
        int opt = 1;
        setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);

        if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(sfd, 1) < 0) {
            fprintf(stderr, "rawcat: erro no bind/listen na porta %d: %s\n", port, strerror(errno));
            close(sfd);
            return 1;
        }

        fprintf(stderr, "rawcat: escutando na porta %d...\n", port);
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        int cfd = accept(sfd, (struct sockaddr *)&client, &clen);
        if (cfd >= 0) {
            fprintf(stderr, "rawcat: conexao estabelecida de %s\n", inet_ntoa(client.sin_addr));
            bridge_streams(cfd);
            close(cfd);
        }
        close(sfd);
        return 0;
    }

    if (!host || port <= 0) {
        print_help();
        return 1;
    }

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = udp_mode ? SOCK_DGRAM : SOCK_STREAM;
    char pstr[16]; snprintf(pstr, sizeof(pstr), "%d", port);

    if (getaddrinfo(host, pstr, &hints, &res) != 0) {
        fprintf(stderr, "rawcat: erro ao resolver host '%s'\n", host);
        return 1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return 1; }

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        fprintf(stderr, "rawcat: falha ao conectar em %s:%d: %s\n", host, port, strerror(errno));
        close(fd);
        freeaddrinfo(res);
        return 1;
    }
    freeaddrinfo(res);

    bridge_streams(fd);
    close(fd);

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "rawcat: streamed to %s:%d", host, port);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
