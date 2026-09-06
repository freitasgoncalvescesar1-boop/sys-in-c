#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <poll.h>
#include <errno.h>
#include "../libutilipc/utilipc.h"

#define DEFAULT_PORT 7878
#define CHUNK_SIZE   65536

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_LABEL   "\033[1;36m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_VAL     "\033[1;32m"
#define COLOR_WARN    "\033[1;33m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_MUTED   "\033[0;90m"
#define COLOR_GRAY    "\033[0;90m"

static volatile int udp_beacon_running = 1;

static void copy_to_clipboard(const char *text) {
    FILE *pipe = NULL;
    if (access("/data/data/com.termux/files/usr/bin/termux-clipboard-set", X_OK) == 0 ||
        system("which termux-clipboard-set >/dev/null 2>&1") == 0) {
        pipe = popen("termux-clipboard-set", "w");
    } else if (system("which wl-copy >/dev/null 2>&1") == 0) {
        pipe = popen("wl-copy", "w");
    } else if (system("which xclip >/dev/null 2>&1") == 0) {
        pipe = popen("xclip -selection clipboard", "w");
    }

    if (pipe) {
        fputs(text, pipe);
        pclose(pipe);
    }
}

// Thread que responde a anúncios broadcast na rede local
static void *udp_discovery_responder(void *arg) {
    int port = *(int *)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return NULL;

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return NULL;
    }

    char my_host[64];
    if (gethostname(my_host, sizeof(my_host)) != 0) strcpy(my_host, "netclip-device");

    while (udp_beacon_running) {
        struct pollfd pfd = { .fd = sock, .events = POLLIN };
        if (poll(&pfd, 1, 500) <= 0) continue;

        char buf[128];
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&client, &clen);
        if (n > 0) {
            buf[n] = '\0';
            if (strncmp(buf, "NETCLIP_DISCOVER", 16) == 0) {
                char reply[256];
                snprintf(reply, sizeof(reply), "NETCLIP_HERE:%s:%d", my_host, port);
                sendto(sock, reply, strlen(reply), 0, (struct sockaddr *)&client, clen);
            }
        }
    }
    close(sock);
    return NULL;
}

// Descobre aparelhos rodando netclip na rede local via broadcast UDP
static int discover_peers(char out_ip[16], int *out_port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    struct sockaddr_in baddr;
    memset(&baddr, 0, sizeof(baddr));
    baddr.sin_family = AF_INET;
    baddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    baddr.sin_port = htons(DEFAULT_PORT);

    printf("  %s[RADAR P2P]%s Buscando aparelhos com netclip aberto na sua rede Wi-Fi...\n", COLOR_LABEL, COLOR_RESET);
    sendto(sock, "NETCLIP_DISCOVER", 16, 0, (struct sockaddr *)&baddr, sizeof(baddr));

    char peers_ip[8][16];
    char peers_name[8][64];
    int peers_port[8];
    int peer_count = 0;

    struct pollfd pfd = { .fd = sock, .events = POLLIN };
    time_t start = time(NULL);

    while (time(NULL) - start < 2 && peer_count < 8) {
        if (poll(&pfd, 1, 300) > 0) {
            char buf[256];
            struct sockaddr_in sender;
            socklen_t slen = sizeof(sender);
            ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&sender, &slen);
            if (n > 0) {
                buf[n] = '\0';
                if (strncmp(buf, "NETCLIP_HERE:", 13) == 0) {
                    char host[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &sender.sin_addr, host, sizeof(host));

                    int exists = 0;
                    for (int i = 0; i < peer_count; i++) {
                        if (strcmp(peers_ip[i], host) == 0) { exists = 1; break; }
                    }
                    if (!exists) {
                        strncpy(peers_ip[peer_count], host, 15);
                        peers_port[peer_count] = DEFAULT_PORT;
                        char *name_token = buf + 13;
                        char *port_token = strchr(name_token, ':');
                        if (port_token) {
                            *port_token = '\0';
                            peers_port[peer_count] = atoi(port_token + 1);
                        }
                        strncpy(peers_name[peer_count], name_token, sizeof(peers_name[0]) - 1);
                        peer_count++;
                    }
                }
            }
        }
    }
    close(sock);

    if (peer_count == 0) {
        printf("  %s• Nenhum dispositivo detectado automaticamente.%s\n", COLOR_WARN, COLOR_RESET);
        printf("    (Certifique-se de que o outro aparelho está com 'netclip listen' rodando)\n\n");
        return -1;
    }

    if (peer_count == 1) {
        strncpy(out_ip, peers_ip[0], 15);
        *out_port = peers_port[0];
        printf("  %s✔ Conectando automaticamente ao único aparelho:%s %s%s%s (%s)\n\n",
               COLOR_OK, COLOR_RESET, COLOR_VAL, peers_name[0], COLOR_RESET, peers_ip[0]);
        return 0;
    }

    printf("\n  %sDispositivos encontrados na rede:%s\n", COLOR_LABEL, COLOR_RESET);
    for (int i = 0; i < peer_count; i++) {
        printf("    [%d] %s%-20.20s%s - IP: %s%s:%d%s\n",
               i + 1, COLOR_VAL, peers_name[i], COLOR_RESET, COLOR_LABEL, peers_ip[i], peers_port[i], COLOR_RESET);
    }
    printf("  Escolha o dispositivo (1-%d): ", peer_count);
    fflush(stdout);

    int choice = 1;
    if (scanf("%d", &choice) == 1 && choice >= 1 && choice <= peer_count) {
        strncpy(out_ip, peers_ip[choice - 1], 15);
        *out_port = peers_port[choice - 1];
        return 0;
    }

    strncpy(out_ip, peers_ip[0], 15);
    *out_port = peers_port[0];
    return 0;
}

static void run_listener(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("netclip: socket");
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("netclip: bind");
        close(server_fd);
        return;
    }

    listen(server_fd, 5);

    pthread_t beacon_tid;
    pthread_create(&beacon_tid, NULL, udp_discovery_responder, &port);

    printf("\n%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ netclip 2.0 - Receptor P2P Wi-Fi & Auto-Discovery Ativo ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("  • Porta TCP/UDP    : %s%d%s\n", COLOR_VAL, port, COLOR_RESET);
    printf("  • Auto-Descoberta  : %sATIVA (Aparelhos na rede acham este dispositivo sem digitar IP)%s\n", COLOR_OK, COLOR_RESET);
    printf("  ---------------------------------------------------------------------------------\n");
    printf("  %s[Aguardando conexoes... Pressione Ctrl+C para encerrar]%s\n\n", COLOR_MUTED, COLOR_RESET);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) continue;

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, sizeof(client_ip));

        char header[256] = "";
        size_t h_idx = 0;
        char ch;
        int newlines = 0;

        while (h_idx < sizeof(header) - 1 && read(client_fd, &ch, 1) == 1) {
            header[h_idx++] = ch;
            if (ch == '\n') {
                newlines++;
                if (newlines == 2 || (h_idx >= 2 && header[h_idx - 2] == '\n')) break;
            } else if (ch != '\r') {
                newlines = 0;
            }
        }
        header[h_idx] = '\0';

        if (strncmp(header, "NETCLIP1\nTEXT", 13) == 0) {
            size_t payload_len = 0;
            sscanf(header, "NETCLIP1\nTEXT\n%zu", &payload_len);

            if (payload_len > 0) {
                char *text_buf = malloc(payload_len + 1);
                if (text_buf) {
                    size_t received = 0;
                    while (received < payload_len) {
                        ssize_t n = read(client_fd, text_buf + received, payload_len - received);
                        if (n <= 0) break;
                        received += n;
                    }
                    text_buf[received] = '\0';

                    time_t now = time(NULL);
                    struct tm *tm_info = localtime(&now);
                    char t_str[32];
                    strftime(t_str, sizeof(t_str), "%H:%M:%S", tm_info);

                    printf("\033[1;32m[%s] Texto Recebido de %s:\033[0m\n", t_str, client_ip);
                    printf("  %s%s%s\n", COLOR_VAL, text_buf, COLOR_RESET);
                    copy_to_clipboard(text_buf);
                    printf("  \033[0;90m└─✔ Copiado automaticamente para a Área de Transferência!\033[0m\n\n");
                    free(text_buf);
                }
            }
            write(client_fd, "OK\n", 3);
        } else if (strncmp(header, "NETCLIP1\nFILE", 13) == 0) {
            char filename[128] = "received_file";
            size_t file_len = 0;
            sscanf(header, "NETCLIP1\nFILE\n%127[^\n]\n%zu", filename, &file_len);

            printf("  \033[1;36m[Arquivo de %s]:\033[0m '%s' (%.2f MB)...\n", client_ip, filename, (double)file_len / (1024.0 * 1024.0));

            FILE *fp = fopen(filename, "wb");
            if (fp) {
                char buffer[CHUNK_SIZE];
                size_t total_received = 0;

                while (total_received < file_len) {
                    size_t to_read = file_len - total_received;
                    if (to_read > sizeof(buffer)) to_read = sizeof(buffer);
                    ssize_t n = read(client_fd, buffer, to_read);
                    if (n <= 0) break;
                    fwrite(buffer, 1, n, fp);
                    total_received += n;
                }
                fclose(fp);
                printf("  \033[1;32m└─✔ Salvo com sucesso em ./%s\033[0m\n\n", filename);
                write(client_fd, "OK\n", 3);
            }
        }
        close(client_fd);
    }
    close(server_fd);
}

static int connect_to_host(const char *host, int port) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char p_str[16];
    snprintf(p_str, sizeof(p_str), "%d", port);

    if (getaddrinfo(host, p_str, &hints, &res) != 0) return -1;
    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) { freeaddrinfo(res); return -1; }

    struct timeval tv = { .tv_sec = 4, .tv_usec = 0 };
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sockfd, res->ai_addr, res->ai_addrlen) != 0) {
        close(sockfd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);
    return sockfd;
}

static void send_text(const char *host, int port, const char *text) {
    int sockfd = connect_to_host(host, port);
    if (sockfd < 0) {
        printf("  %s[ERRO]%s Nao foi possivel conectar a %s:%d\n", COLOR_ERR, COLOR_RESET, host, port);
        return;
    }

    size_t len = strlen(text);
    char header[128];
    snprintf(header, sizeof(header), "NETCLIP1\nTEXT\n%zu\n\n", len);

    write(sockfd, header, strlen(header));
    write(sockfd, text, len);

    char ack[16] = "";
    read(sockfd, ack, sizeof(ack) - 1);
    printf("  %s✔ Texto enviado para %s:%d!%s\n\n", COLOR_OK, host, port, COLOR_RESET);
    close(sockfd);
}

static void send_file(const char *host, int port, const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        fprintf(stderr, "netclip: erro ao abrir '%s': %s\n", filepath, strerror(errno));
        return;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;

    int sockfd = connect_to_host(host, port);
    if (sockfd < 0) {
        fclose(fp);
        printf("  %s[ERRO]%s Nao foi possivel conectar a %s:%d\n", COLOR_ERR, COLOR_RESET, host, port);
        return;
    }

    char header[256];
    snprintf(header, sizeof(header), "NETCLIP1\nFILE\n%s\n%ld\n\n", filename, fsize);
    write(sockfd, header, strlen(header));

    printf("  Enviando '%s' (%.2f MB)...\n", filename, (double)fsize / (1024.0 * 1024.0));
    char buffer[CHUNK_SIZE];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        write(sockfd, buffer, n);
    }
    fclose(fp);

    char ack[16] = "";
    read(sockfd, ack, sizeof(ack) - 1);
    printf("  %s✔ Arquivo enviado com sucesso para %s:%d!%s\n\n", COLOR_OK, host, port, COLOR_RESET);
    close(sockfd);
}

int main(int argc, char *argv[]) {
    utilipc_init();
    int port = DEFAULT_PORT;

    if (argc < 2) {
        printf("Usage:\n");
        printf("  netclip listen [port]                 (Fica escutando com auto-descoberta ativada)\n");
        printf("  netclip send \"texto\"                  (Descobre o receptor automaticamente na rede)\n");
        printf("  netclip send -f <arquivo>             (Descobre e envia arquivo sem digitar IP)\n");
        printf("  netclip send <IP> \"texto\"             (Envio direto informando o IP)\n");
        printf("  netclip send <IP> -f <arquivo>        (Envio direto de arquivo)\n");
        utilipc_close();
        return 0;
    }

    if (strcmp(argv[1], "listen") == 0 || strcmp(argv[1], "-l") == 0) {
        if (argc >= 3) port = atoi(argv[2]);
        run_listener(port);
        utilipc_close();
        return 0;
    }

    if (strcmp(argv[1], "send") == 0) {
        char target_ip[16] = "";
        int target_port = DEFAULT_PORT;

        if (argc == 3 && strcmp(argv[2], "-f") != 0) {
            if (discover_peers(target_ip, &target_port) == 0) {
                send_text(target_ip, target_port, argv[2]);
            }
            utilipc_close();
            return 0;
        } else if (argc == 4 && strcmp(argv[2], "-f") == 0) {
            if (discover_peers(target_ip, &target_port) == 0) {
                send_file(target_ip, target_port, argv[3]);
            }
            utilipc_close();
            return 0;
        }

        if (argc >= 4) {
            const char *host = argv[2];
            if (strcmp(argv[3], "-f") == 0 && argc >= 5) {
                send_file(host, port, argv[4]);
            } else {
                send_text(host, port, argv[3]);
            }
        }
        utilipc_close();
        return 0;
    }

    utilipc_close();
    return 0;
}
