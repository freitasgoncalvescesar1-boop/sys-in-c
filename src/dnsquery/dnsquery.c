#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_TYPE    "\033[1;33m"
#define COLOR_VAL     "\033[1;36m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_MUTED   "\033[0;90m"

#define DNS_TYPE_A     1
#define DNS_TYPE_NS    2
#define DNS_TYPE_CNAME 5
#define DNS_TYPE_SOA   6
#define DNS_TYPE_PTR   12
#define DNS_TYPE_MX    15
#define DNS_TYPE_TXT   16
#define DNS_TYPE_AAAA  28

#pragma pack(push, 1)
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} DNSHeader;
#pragma pack(pop)

static double get_time_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static void domain_to_dns_format(const char *domain, unsigned char *buf, size_t *out_len) {
    size_t len = strlen(domain);
    size_t pos = 0;
    size_t label_start = 0;
    buf[pos++] = 0;

    for (size_t i = 0; i < len; i++) {
        if (domain[i] == '.') {
            buf[label_start] = (unsigned char)(pos - label_start - 1);
            label_start = pos;
            buf[pos++] = 0;
        } else {
            buf[pos++] = domain[i];
        }
    }
    buf[label_start] = (unsigned char)(pos - label_start - 1);
    buf[pos++] = 0;
    *out_len = pos;
}

static size_t read_dns_name(const unsigned char *reader, const unsigned char *buffer, size_t buf_len, char *out_name) {
    size_t p = 0;
    int jumped = 0;
    size_t bytes_consumed = 0;
    int max_jumps = 10;
    out_name[0] = '\0';

    while (*reader != 0) {
        if ((size_t)(reader - buffer) >= buf_len) break;
        if ((*reader & 0xC0) == 0xC0) {
            uint16_t offset = ((*reader & 0x3F) << 8) | *(reader + 1);
            if (!jumped) bytes_consumed += 2;
            reader = buffer + offset;
            jumped = 1;
            if (--max_jumps <= 0) break;
            continue;
        }
        uint8_t len = *reader++;
        if (!jumped) bytes_consumed++;
        for (uint8_t i = 0; i < len; i++) {
            out_name[p++] = *reader++;
            if (!jumped) bytes_consumed++;
        }
        out_name[p++] = '.';
    }
    if (!jumped) bytes_consumed++;
    if (p > 0 && out_name[p - 1] == '.') out_name[p - 1] = '\0';
    else out_name[p] = '\0';
    return bytes_consumed;
}

int main(int argc, char *argv[]) {
    utilipc_init();
    if (argc < 2) {
        printf("Usage:\n  dnsquery <DOMAIN> [TYPE] [-s <SERVER>] [-p <PORT>]\n  dnsquery -x <IP> [-s <SERVER>]\n");
        utilipc_close();
        return 0;
    }

    char domain[256] = "";
    uint16_t qtype = DNS_TYPE_A;
    char server_ip[128] = "1.1.1.1";
    int server_port = 53;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-x") == 0 && i + 1 < argc) {
            int a, b, c, d;
            if (sscanf(argv[++i], "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
                snprintf(domain, sizeof(domain), "%d.%d.%d.%d.in-addr.arpa", d, c, b, a);
                qtype = DNS_TYPE_PTR;
            }
        } else if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--server") == 0) && i + 1 < argc) {
            strncpy(server_ip, argv[++i], sizeof(server_ip) - 1);
            char *colon = strchr(server_ip, ':');
            if (colon) { *colon = '\0'; server_port = atoi(colon + 1); }
            else if (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0])) server_port = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc) {
            server_port = atoi(argv[++i]);
        } else if (strlen(domain) == 0 && argv[i][0] != '-') {
            strncpy(domain, argv[i], sizeof(domain) - 1);
        }
    }

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    char pstr[16]; snprintf(pstr, sizeof(pstr), "%d", server_port);

    if (getaddrinfo(server_ip, pstr, &hints, &res) != 0) {
        fprintf(stderr, "dnsquery: erro ao resolver servidor '%s'\n", server_ip);
        utilipc_close();
        return 1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    unsigned char packet[512];
    memset(packet, 0, sizeof(packet));
    DNSHeader *hdr = (DNSHeader *)packet;
    hdr->id = htons(rand() % 65535);
    hdr->flags = htons(0x0100);
    hdr->qdcount = htons(1);

    size_t qname_len = 0;
    domain_to_dns_format(domain, packet + sizeof(DNSHeader), &qname_len);

    unsigned char *q_ptr = packet + sizeof(DNSHeader) + qname_len;
    *((uint16_t *)q_ptr) = htons(qtype); q_ptr += 2;
    *((uint16_t *)q_ptr) = htons(1);     q_ptr += 2;

    double t0 = get_time_sec();
    sendto(fd, packet, q_ptr - packet, 0, res->ai_addr, res->ai_addrlen);

    unsigned char resp[1024];
    struct sockaddr_in from;
    socklen_t flen = sizeof(from);
    ssize_t n = recvfrom(fd, resp, sizeof(resp), 0, (struct sockaddr *)&from, &flen);
    double elapsed = (get_time_sec() - t0) * 1000.0;
    close(fd);
    freeaddrinfo(res);

    if (n < (ssize_t)sizeof(DNSHeader)) {
        printf("  %s[ERRO]%s Timeout na resposta do DNS %s:%d\n\n", COLOR_ERR, COLOR_RESET, server_ip, server_port);
        utilipc_close();
        return 1;
    }

    DNSHeader *rh = (DNSHeader *)resp;
    uint16_t rflags = ntohs(rh->flags);
    int is_aa = (rflags & 0x0400) != 0;

    printf("\n  %s[STATUS: %s]%s Latencia: %s%.2f ms%s | Flags: %s%s%s\n",
           COLOR_OK, (rflags & 0xF) == 0 ? "NOERROR" : "ERRO", COLOR_RESET,
           COLOR_VAL, elapsed, COLOR_RESET,
           is_aa ? "\033[1;33mAUTORITATIVO (AA)" : "\033[0;90mRECURSIVO", COLOR_RESET, "");
    printf("  ---------------------------------------------------------------------------------\n");

    const unsigned char *reader = resp + sizeof(DNSHeader);
    char dummy[256];
    reader += read_dns_name(reader, resp, n, dummy);
    reader += 4;

    for (int i = 0; i < ntohs(rh->ancount); i++) {
        char ans_name[256];
        reader += read_dns_name(reader, resp, n, ans_name);
        uint16_t type = ntohs(*((uint16_t *)reader)); reader += 2;
        reader += 2;
        uint32_t ttl = ntohl(*((uint32_t *)reader));  reader += 4;
        uint16_t dlen = ntohs(*((uint16_t *)reader)); reader += 2;

        if (type == DNS_TYPE_A && dlen == 4) {
            char ip[16];
            inet_ntop(AF_INET, reader, ip, sizeof(ip));
            printf("  • %-20s  TTL: %-5u  A     => %s%s%s\n", ans_name, ttl, COLOR_VAL, ip, COLOR_RESET);
            reader += 4;
        } else if (type == DNS_TYPE_PTR) {
            char ptr_host[256];
            read_dns_name(reader, resp, n, ptr_host);
            printf("  • %-20s  TTL: %-5u  PTR   => %s%s%s\n", ans_name, ttl, COLOR_OK, ptr_host, COLOR_RESET);
            reader += dlen;
        } else {
            reader += dlen;
        }
    }
    printf("  ---------------------------------------------------------------------------------\n\n");

    utilipc_close();
    return 0;
}
