#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_WARN    "\033[1;33m"
#define COLOR_TAG     "\033[1;33m"
#define COLOR_VAL     "\033[1;36m"
#define COLOR_LABEL   "\033[1;37m"
#define COLOR_MUTED   "\033[0;90m"

#define SSDP_MCAST_ADDR "239.255.255.250"
#define SSDP_MCAST_PORT 1900
#define MAX_DEVICES     64

typedef struct {
    char ip[INET_ADDRSTRLEN];
    int  port;
    char friendly_name[128];
    char manufacturer[64];
    char model_name[64];
    char device_type[64];
    char location_url[256];
} IotDevice;

static IotDevice devices[MAX_DEVICES];
static int device_count = 0;

static void print_help(void) {
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ iotscan 1.0 - Smart TV, Chromecast, IoT & UPnP Multicast Radar ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  iotscan [OPTIONS]\n\n");
    printf("Options:\n");
    printf("  %s-t, --timeout <SECS>%s    Listening timeout in seconds [Default: 3s]\n", COLOR_OK, COLOR_RESET);
    printf("  %s-v, --verbose%s           Show raw description XML URLs and server headers\n", COLOR_OK, COLOR_RESET);
    printf("  %s-h, --help%s              Display this formatted help guide and exit\n\n", COLOR_OK, COLOR_RESET);
    printf("Aparelhos Detectados:\n");
    printf("  • Smart TVs (Samsung Tizen, LG webOS, Android TV, Roku)\n");
    printf("  • Dispositivos de Cast (Google Chromecast, Fire TV, Apple AirPlay)\n");
    printf("  • Roteadores e Modems Wi-Fi (UPnP InternetGatewayDevice)\n");
    printf("  • Caixas de Som Inteligentes (Echo/Alexa, Sonos, Caixas Wi-Fi)\n");
    printf("  • Consoles de Videogame (PlayStation 4/5, Xbox)\n\n", COLOR_RESET);
    printf("Exemplos:\n");
    printf("  • %siotscan%s                                (Descobre todos os aparelhos no Wi-Fi)\n", COLOR_TAG, COLOR_RESET);
    printf("  • %siotscan -t 5 -v%s                         (Escaneia por 5s com informacoes detalhadas)\n", COLOR_TAG, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

// Extrai o conteúdo dentro de tags XML <tag>conteudo</tag>
static void extract_xml_tag(const char *xml, const char *tag, char *out, size_t out_sz) {
    out[0] = '\0';
    char open_tag[64], close_tag[64];
    snprintf(open_tag, sizeof(open_tag), "<%s>", tag);
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag);

    const char *start = strcasestr(xml, open_tag);
    if (!start) {
        // Tenta formato com namespace <device:tag>
        snprintf(open_tag, sizeof(open_tag), ":%s>", tag);
        start = strcasestr(xml, open_tag);
        if (start) start += strlen(open_tag);
    } else {
        start += strlen(open_tag);
    }

    if (!start) return;

    const char *end = strcasestr(start, close_tag);
    if (!end) {
        snprintf(close_tag, sizeof(close_tag), ":%s>", tag);
        end = strcasestr(start, close_tag);
        if (end) {
            const char *p = end;
            while (p > start && *p != '<') p--;
            if (p > start) end = p;
        }
    }

    if (!end || end <= start) return;

    size_t len = end - start;
    if (len >= out_sz) len = out_sz - 1;
    strncpy(out, start, len);
    out[len] = '\0';
}

// Busca rápida de metadados XML via conexão HTTP expressa (< 500ms)
static void fetch_device_xml_details(const char *url, IotDevice *dev) {
    if (!url || strncasecmp(url, "http://", 7) != 0) return;

    char host[128] = "";
    char path[256] = "/";
    int port = 80;

    const char *p = url + 7;
    const char *slash = strchr(p, '/');
    if (slash) {
        size_t hlen = slash - p;
        if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
        strncpy(host, p, hlen);
        host[hlen] = '\0';
        strncpy(path, slash, sizeof(path) - 1);
    } else {
        strncpy(host, p, sizeof(host) - 1);
    }

    char *colon = strchr(host, ':');
    if (colon) {
        *colon = '\0';
        port = atoi(colon + 1);
    }

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char pstr[16];
    snprintf(pstr, sizeof(pstr), "%d", port);

    if (getaddrinfo(host, pstr, &hints, &res) != 0) return;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return; }

    // Timeout ultrarrápido de 400ms para não travar o radar
    struct timeval tv = { .tv_sec = 0, .tv_usec = 400000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) == 0) {
        char req[512];
        snprintf(req, sizeof(req),
                 "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUser-Agent: sys-in-c-iotscan/1.0\r\nConnection: close\r\n\r\n",
                 path, host, port);
        send(fd, req, strlen(req), 0);

        char xml_buf[4096];
        ssize_t n = recv(fd, xml_buf, sizeof(xml_buf) - 1, 0);
        if (n > 0) {
            xml_buf[n] = '\0';

            char fn[128] = "", mf[64] = "", mn[64] = "", dt[64] = "";
            extract_xml_tag(xml_buf, "friendlyName", fn, sizeof(fn));
            extract_xml_tag(xml_buf, "manufacturer", mf, sizeof(mf));
            extract_xml_tag(xml_buf, "modelName", mn, sizeof(mn));
            extract_xml_tag(xml_buf, "deviceType", dt, sizeof(dt));

            if (strlen(fn) > 0) strncpy(dev->friendly_name, fn, sizeof(dev->friendly_name) - 1);
            if (strlen(mf) > 0) strncpy(dev->manufacturer, mf, sizeof(dev->manufacturer) - 1);
            if (strlen(mn) > 0) strncpy(dev->model_name, mn, sizeof(dev->model_name) - 1);

            // Categorização do Dispositivo
            if (strstr(dt, "MediaRenderer") || strstr(fn, "TV") || strstr(mn, "TV") || strstr(mf, "Samsung") || strstr(mf, "LG")) {
                strcpy(dev->device_type, "Smart TV / Display");
            } else if (strstr(dt, "InternetGatewayDevice") || strstr(fn, "Router") || strstr(mn, "Router")) {
                strcpy(dev->device_type, "Roteador / Gateway");
            } else if (strstr(fn, "Chromecast") || strstr(fn, "Google Cast")) {
                strcpy(dev->device_type, "Google Cast / Dongle");
            } else if (strstr(fn, "Speaker") || strstr(fn, "Echo") || strstr(fn, "Sonos")) {
                strcpy(dev->device_type, "Caixa de Som Inteligente");
            } else {
                strcpy(dev->device_type, "Aparelho UPnP / IoT");
            }
        }
    }
    close(fd);
    freeaddrinfo(res);
}

// Verifica se o dispositivo já foi registrado (deduplicação por IP e Nome)
static int is_duplicate(const char *ip, const char *friendly_name) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].ip, ip) == 0) {
            if (strlen(friendly_name) > 0 && strcmp(devices[i].friendly_name, "Desconhecido") == 0) {
                strncpy(devices[i].friendly_name, friendly_name, sizeof(devices[i].friendly_name) - 1);
            }
            return 1;
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    utilipc_init();

    int timeout_sec = 3;
    int verbose = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            utilipc_close();
            return 0;
        }
        if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--timeout") == 0) && i + 1 < argc) {
            timeout_sec = atoi(argv[++i]);
            if (timeout_sec < 1) timeout_sec = 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        }
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        perror("iotscan: socket");
        utilipc_close();
        return 1;
    }

    // Habilita Broadcast / Multicast
    int loop = 1;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    struct sockaddr_in mcast_addr;
    memset(&mcast_addr, 0, sizeof(mcast_addr));
    mcast_addr.sin_family = AF_INET;
    mcast_addr.sin_addr.s_addr = inet_addr(SSDP_MCAST_ADDR);
    mcast_addr.sin_port = htons(SSDP_MCAST_PORT);

    // Pacote M-SEARCH universal do protocolo SSDP (RFC / UPnP Device Architecture)
    const char *msearch_probe =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: ssdp:all\r\n"
        "\r\n";

    printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s│%s  %s[ 📡 iotscan 1.0 - Radar de Smart TVs, Chromecast & Aparelhos IoT ]%s    %s│%s\n",
           COLOR_TITLE, COLOR_RESET, COLOR_OK, COLOR_RESET, COLOR_TITLE, COLOR_RESET);
    printf("%s├────────────────────────────────────────────────────────────────────────────┤%s\n", COLOR_TITLE, COLOR_RESET);
    printf("  • Protocolo          : %sSSDP / UPnP Multicast (239.255.255.250:1900)%s\n", COLOR_VAL, COLOR_RESET);
    printf("  • Tempo de Escuta    : %s%d segundos%s\n", COLOR_VAL, timeout_sec, COLOR_RESET);
    printf("  • Buscando           : Smart TVs, Chromecasts, Consoles, Caixas de Som e Roteadores...\n");
    printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n", COLOR_TITLE, COLOR_RESET);

    // Dispara a busca em Multicast na rede local
    sendto(sock, msearch_probe, strlen(msearch_probe), 0, (struct sockaddr *)&mcast_addr, sizeof(mcast_addr));

    time_t start_time = time(NULL);
    struct pollfd pfd = { .fd = sock, .events = POLLIN };

    while (time(NULL) - start_time < timeout_sec) {
        int pr = poll(&pfd, 1, 300);
        if (pr <= 0) continue;

        char buf[2048];
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);

        ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&sender, &slen);
        if (n <= 0) continue;
        buf[n] = '\0';

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sender.sin_addr, ip_str, sizeof(ip_str));

        char location[256] = "";
        char server[128] = "";

        char *loc_ptr = strcasestr(buf, "LOCATION:");
        if (loc_ptr) {
            loc_ptr += 9;
            while (*loc_ptr == ' ') loc_ptr++;
            char *end_l = strpbrk(loc_ptr, "\r\n");
            if (end_l) snprintf(location, sizeof(location), "%.*s", (int)(end_l - loc_ptr), loc_ptr);
        }

        char *srv_ptr = strcasestr(buf, "SERVER:");
        if (srv_ptr) {
            srv_ptr += 7;
            while (*srv_ptr == ' ') srv_ptr++;
            char *end_s = strpbrk(srv_ptr, "\r\n");
            if (end_s) snprintf(server, sizeof(server), "%.*s", (int)(end_s - srv_ptr), srv_ptr);
        }

        if (is_duplicate(ip_str, "")) continue;

        if (device_count < MAX_DEVICES) {
            IotDevice *dev = &devices[device_count++];
            strncpy(dev->ip, ip_str, sizeof(dev->ip) - 1);
            dev->port = ntohs(sender.sin_port);
            strcpy(dev->friendly_name, "Dispositivo UPnP");
            strcpy(dev->manufacturer, "Generico");
            strcpy(dev->model_name, "-");
            strcpy(dev->device_type, "Aparelho Conectado");
            strncpy(dev->location_url, location, sizeof(dev->location_url) - 1);

            if (strlen(server) > 0) {
                strncpy(dev->friendly_name, server, sizeof(dev->friendly_name) - 1);
            }

            // Busca os detalhes amigáveis em segundo plano no XML do aparelho
            if (strlen(location) > 0) {
                fetch_device_xml_details(location, dev);
            }
        }
    }
    close(sock);

    // =========================================================================
    // EXIBIÇÃO DA TABELA FORMATADA
    // =========================================================================
    printf("  %-26.26s  %-15s  %-22.22s  %s\n", "NOME DO DISPOSITIVO", "ENDEREÇO IP", "TIPO ESTIMADO", "FABRICANTE");
    printf("  ---------------------------------------------------------------------------------\n");

    for (int i = 0; i < device_count; i++) {
        IotDevice *d = &devices[i];
        printf("  %s%-26.26s%s  %s%-15s%s  %s%-22.22s%s  %s%s%s\n",
               COLOR_OK, d->friendly_name, COLOR_RESET,
               COLOR_VAL, d->ip, COLOR_RESET,
               COLOR_TAG, d->device_type, COLOR_RESET,
               COLOR_MUTED, d->manufacturer, COLOR_RESET);

        if (verbose && strlen(d->location_url) > 0) {
            printf("    %s└─ URL XML: %s%s\n", COLOR_MUTED, d->location_url, COLOR_RESET);
        }
    }

    printf("  ---------------------------------------------------------------------------------\n");
    if (device_count == 0) {
        printf("  %s• Nenhum dispositivo UPnP/SSDP respondeu durante a janela de %ds.%s\n", COLOR_WARN, timeout_sec, COLOR_RESET);
        printf("    (Dica: certifique-se de estar conectado ao Wi-Fi com TVs/aparelhos ligados)\n\n");
    } else {
        printf("  %s✔ Varredura concluída! Dispositivos inteligentes encontrados: %d%s\n\n", COLOR_OK, device_count, COLOR_RESET);
    }

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "iotscan: discovered %d UPnP/IoT devices on LAN", device_count);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
