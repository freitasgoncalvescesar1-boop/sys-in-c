#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
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

static void print_help(void) {
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ dnsquery - RFC 1035 Raw UDP DNS Packet Inspector ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  dnsquery <DOMAIN> [TYPE] [-s <SERVER_IP>]\n\n");
    printf("Record Types Suportados:\n");
    printf("  A      (IPv4 Address - Padrao)\n");
    printf("  AAAA   (IPv6 Address)\n");
    printf("  MX     (Mail Exchange Servers)\n");
    printf("  TXT    (Text / SPF Records)\n");
    printf("  CNAME  (Canonical Name Redirection)\n");
    printf("  NS     (Nameservers)\n\n");
    printf("Exemplos:\n");
    printf("  dnsquery google.com\n");
    printf("  dnsquery google.com AAAA\n");
    printf("  dnsquery github.com MX -s 8.8.8.8\n");
    printf("  dnsquery cloudflare.com TXT\n");
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

// Converte "google.com" para o formato de labels DNS: "\x06google\x03com\x00"
static void domain_to_dns_format(const char *domain, unsigned char *buf, size_t *out_len) {
    size_t len = strlen(domain);
    size_t pos = 0;
    size_t label_start = 0;

    buf[pos++] = 0; // Placeholder para tamanho da primeira label

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
    buf[pos++] = 0; // Null terminator do QNAME
    *out_len = pos;
}

// Decodifica nomes de domínio com descompressão de ponteiros (0xC0)
static size_t read_dns_name(const unsigned char *reader, const unsigned char *buffer, size_t buf_len, char *out_name) {
    size_t p = 0;
    int jumped = 0;
    size_t bytes_consumed = 0;
    int max_jumps = 10;

    out_name[0] = '\0';

    while (*reader != 0) {
        if ((size_t)(reader - buffer) >= buf_len) break;

        // Ponteiro de compressão DNS (inicia com 11xxxxxx em binário = 0xC0)
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

static uint16_t parse_type(const char *str) {
    if (strcasecmp(str, "A") == 0) return DNS_TYPE_A;
    if (strcasecmp(str, "AAAA") == 0) return DNS_TYPE_AAAA;
    if (strcasecmp(str, "MX") == 0) return DNS_TYPE_MX;
    if (strcasecmp(str, "TXT") == 0) return DNS_TYPE_TXT;
    if (strcasecmp(str, "CNAME") == 0) return DNS_TYPE_CNAME;
    if (strcasecmp(str, "NS") == 0) return DNS_TYPE_NS;
    return DNS_TYPE_A;
}

static const char *type_to_str(uint16_t t) {
    if (t == DNS_TYPE_A) return "A";
    if (t == DNS_TYPE_AAAA) return "AAAA";
    if (t == DNS_TYPE_MX) return "MX";
    if (t == DNS_TYPE_TXT) return "TXT";
    if (t == DNS_TYPE_CNAME) return "CNAME";
    if (t == DNS_TYPE_NS) return "NS";
    return "UNKNOWN";
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help();
        utilipc_close();
        return 0;
    }

    const char *domain = argv[1];
    uint16_t qtype = DNS_TYPE_A;
    const char *server_ip = "1.1.1.1"; // Cloudflare DNS por padrão

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            server_ip = argv[++i];
        } else {
            qtype = parse_type(argv[i]);
        }
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        perror("socket");
        utilipc_close();
        return 1;
    }

    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(53);
    inet_pton(AF_INET, server_ip, &dest.sin_addr);

    // 1. Montagem do Pacote DNS (RFC 1035)
    unsigned char packet[512];
    memset(packet, 0, sizeof(packet));

    DNSHeader *hdr = (DNSHeader *)packet;
    srand(time(NULL));
    hdr->id = (uint16_t)htons(rand() % 65535);
    hdr->flags = htons(0x0100); // Standard query com Recursion Desired (RD = 1)
    hdr->qdcount = htons(1);

    size_t qname_len = 0;
    domain_to_dns_format(domain, packet + sizeof(DNSHeader), &qname_len);

    unsigned char *q_ptr = packet + sizeof(DNSHeader) + qname_len;
    *((uint16_t *)q_ptr) = htons(qtype); q_ptr += 2;
    *((uint16_t *)q_ptr) = htons(1);     q_ptr += 2; // QCLASS 1 = IN (Internet)

    size_t packet_len = q_ptr - packet;

    printf("\n  %sEnviando query DNS [%s] para '%s' via %s:53 (UDP)...%s\n",
           COLOR_TITLE, type_to_str(qtype), domain, server_ip, COLOR_RESET);

    double t_start = get_time_sec();
    sendto(sockfd, packet, packet_len, 0, (struct sockaddr *)&dest, sizeof(dest));

    unsigned char response[1024];
    socklen_t dest_len = sizeof(dest);
    ssize_t resp_len = recvfrom(sockfd, response, sizeof(response), 0, (struct sockaddr *)&dest, &dest_len);
    double elapsed = (get_time_sec() - t_start) * 1000.0;
    close(sockfd);

    if (resp_len < (ssize_t)sizeof(DNSHeader)) {
        fprintf(stderr, "  %s[ERRO]%s Timeout na resposta do servidor DNS %s (Sem resposta em 3s)\n\n", COLOR_ERR, COLOR_RESET, server_ip);
        utilipc_close();
        return 1;
    }

    DNSHeader *r_hdr = (DNSHeader *)response;
    uint16_t r_flags = ntohs(r_hdr->flags);
    int rcode = r_flags & 0x0F;
    uint16_t ancount = ntohs(r_hdr->ancount);

    if (rcode != 0) {
        printf("  %s[STATUS: %s]%s (RCODE: %d) em %.2f ms\n\n",
               COLOR_ERR, (rcode == 3) ? "NXDOMAIN (Dominio nao existe)" : "SERVFAIL / REFUSED", COLOR_RESET, rcode, elapsed);
        utilipc_close();
        return 0;
    }

    printf("  %s[STATUS: NOERROR]%s Respostas: %d | Latencia: %s%.2f ms%s\n",
           COLOR_OK, COLOR_RESET, ancount, COLOR_OK, elapsed, COLOR_RESET);
    printf("  ---------------------------------------------------------------------------------\n");

    const unsigned char *reader = response + sizeof(DNSHeader);

    // Pula a seção Question na resposta
    char dummy_name[256];
    reader += read_dns_name(reader, response, resp_len, dummy_name);
    reader += 4; // QTYPE + QCLASS

    // 2. Decodificação das Respostas (Answers)
    for (int i = 0; i < ancount; i++) {
        if ((size_t)(reader - response) >= (size_t)resp_len) break;

        char ans_name[256];
        reader += read_dns_name(reader, response, resp_len, ans_name);

        uint16_t type = ntohs(*((uint16_t *)reader)); reader += 2;
        reader += 2; // CLASS
        uint32_t ttl = ntohl(*((uint32_t *)reader));  reader += 4;
        uint16_t data_len = ntohs(*((uint16_t *)reader)); reader += 2;

        if (type == DNS_TYPE_A && data_len == 4) {
            char ip_str[16];
            inet_ntop(AF_INET, reader, ip_str, sizeof(ip_str));
            printf("  • %-20s  TTL: %-5u  %s%-5s%s  %s%s%s\n",
                   ans_name, ttl, COLOR_TYPE, "A", COLOR_RESET, COLOR_VAL, ip_str, COLOR_RESET);
            reader += 4;
        } else if (type == DNS_TYPE_AAAA && data_len == 16) {
            char ip6_str[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, reader, ip6_str, sizeof(ip6_str));
            printf("  • %-20s  TTL: %-5u  %s%-5s%s  %s%s%s\n",
                   ans_name, ttl, COLOR_TYPE, "AAAA", COLOR_RESET, COLOR_VAL, ip6_str, COLOR_RESET);
            reader += 16;
        } else if (type == DNS_TYPE_CNAME) {
            char cname[256];
            read_dns_name(reader, response, resp_len, cname);
            printf("  • %-20s  TTL: %-5u  %s%-5s%s  %s%s%s\n",
                   ans_name, ttl, COLOR_TYPE, "CNAME", COLOR_RESET, COLOR_MUTED, cname, COLOR_RESET);
            reader += data_len;
        } else if (type == DNS_TYPE_MX) {
            uint16_t pref = ntohs(*((uint16_t *)reader));
            char mx_host[256];
            read_dns_name(reader + 2, response, resp_len, mx_host);
            printf("  • %-20s  TTL: %-5u  %s%-5s%s  Pref: %-3u %s%s%s\n",
                   ans_name, ttl, COLOR_TYPE, "MX", COLOR_RESET, pref, COLOR_VAL, mx_host, COLOR_RESET);
            reader += data_len;
        } else if (type == DNS_TYPE_TXT) {
            uint8_t txt_len = *reader;
            char txt_buf[512] = "";
            snprintf(txt_buf, sizeof(txt_buf), "%.*s", (int)txt_len, (const char *)(reader + 1));
            printf("  • %-20s  TTL: %-5u  %s%-5s%s  \"%s%s%s\"\n",
                   ans_name, ttl, COLOR_TYPE, "TXT", COLOR_RESET, COLOR_VAL, txt_buf, COLOR_RESET);
            reader += data_len;
        } else {
            printf("  • %-20s  TTL: %-5u  %sTYPE %u%s  (Tamanho: %u bytes)\n",
                   ans_name, ttl, COLOR_TYPE, type, COLOR_RESET, data_len);
            reader += data_len;
        }
    }
    printf("  ---------------------------------------------------------------------------------\n\n");

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "dnsquery: %s [%s] (%.2fms via %s)", domain, type_to_str(qtype), elapsed, server_ip);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
