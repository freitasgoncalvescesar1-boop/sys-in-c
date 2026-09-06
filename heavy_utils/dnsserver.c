#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <termios.h>
#include "../src/libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_WARN    "\033[1;33m"
#define COLOR_TAG     "\033[1;33m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_LABEL   "\033[1;36m"
#define COLOR_VAL     "\033[1;37m"
#define COLOR_MUTED   "\033[0;90m"

#define MAX_LOCAL_HOSTS  128
#define MAX_BLOCKLIST    512
#define MAX_CACHE        256
#define DNS_PACKET_MAX   512

#define DNS_TYPE_A     1
#define DNS_TYPE_NS    2
#define DNS_TYPE_CNAME 5
#define DNS_TYPE_PTR   12
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

typedef struct {
    char domain[128];
    char ip[INET_ADDRSTRLEN];
    uint32_t ttl;
} LocalHostEntry;

typedef struct {
    char domain[128];
} BlocklistEntry;

typedef struct {
    char domain[128];
    uint16_t qtype;
    uint8_t packet[DNS_PACKET_MAX];
    size_t packet_len;
    time_t expire_time;
    int is_used;
} DNSCacheEntry;

static LocalHostEntry local_hosts[MAX_LOCAL_HOSTS];
static int local_hosts_count = 0;

static BlocklistEntry blocklist[MAX_BLOCKLIST];
static int blocklist_count = 0;

static DNSCacheEntry dns_cache[MAX_CACHE];
static int cache_count = 0;

static volatile sig_atomic_t keep_running = 1;
static int adblock_paused = 0;

static unsigned long total_queries = 0;
static unsigned long blocked_queries = 0;
static unsigned long local_queries = 0;
static unsigned long cached_queries = 0;
static unsigned long forwarded_queries = 0;
static double start_time_sec = 0.0;

static double get_time_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static void sig_handler(int sig) {
    (void)sig;
    keep_running = 0;
}

static void add_local_host(const char *domain, const char *ip, uint32_t ttl) {
    if (local_hosts_count >= MAX_LOCAL_HOSTS) return;
    strncpy(local_hosts[local_hosts_count].domain, domain, sizeof(local_hosts[0].domain) - 1);
    strncpy(local_hosts[local_hosts_count].ip, ip, sizeof(local_hosts[0].ip) - 1);
    local_hosts[local_hosts_count].ttl = ttl;
    local_hosts_count++;
}

static void add_blocklist_domain(const char *domain) {
    if (blocklist_count >= MAX_BLOCKLIST) return;
    strncpy(blocklist[blocklist_count].domain, domain, sizeof(blocklist[0].domain) - 1);
    blocklist_count++;
}

static void init_default_tables(void) {
    add_local_host("servidor.lan", "192.168.1.100", 60);
    add_local_host("roteador.local", "192.168.1.1", 60);
    add_local_host("meusite.test", "127.0.0.1", 60);
    add_local_host("termux.local", "127.0.0.1", 60);
    add_local_host("sysbox.local", "127.0.0.1", 60);

    add_blocklist_domain("telemetry.google.com");
    add_blocklist_domain("metrics.icloud.com");
    add_blocklist_domain("ads.facebook.com");
    add_blocklist_domain("doubleclick.net");
    add_blocklist_domain("pagead2.googlesyndication.com");
    add_blocklist_domain("adservice.google.com");
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

static const char *type_to_str(uint16_t t) {
    if (t == DNS_TYPE_A) return "A";
    if (t == DNS_TYPE_AAAA) return "AAAA";
    if (t == DNS_TYPE_PTR) return "PTR";
    if (t == DNS_TYPE_TXT) return "TXT";
    if (t == DNS_TYPE_CNAME) return "CNAME";
    return "QUERY";
}

static int is_domain_blocked(const char *domain) {
    if (adblock_paused) return 0;
    for (int i = 0; i < blocklist_count; i++) {
        if (strcasecmp(domain, blocklist[i].domain) == 0) return 1;
        size_t bl_len = strlen(blocklist[i].domain);
        size_t d_len = strlen(domain);
        if (d_len > bl_len && domain[d_len - bl_len - 1] == '.' &&
            strcasecmp(domain + d_len - bl_len, blocklist[i].domain) == 0) {
            return 1;
        }
    }
    return 0;
}

static const char *find_local_host(const char *domain, uint32_t *out_ttl) {
    for (int i = 0; i < local_hosts_count; i++) {
        if (strcasecmp(domain, local_hosts[i].domain) == 0) {
            *out_ttl = local_hosts[i].ttl;
            return local_hosts[i].ip;
        }
    }
    return NULL;
}

static const char *find_ptr_reverse_host(const char *query_domain) {
    for (int i = 0; i < local_hosts_count; i++) {
        int a, b, c, d;
        if (sscanf(local_hosts[i].ip, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
            char expected_ptr[128];
            snprintf(expected_ptr, sizeof(expected_ptr), "%d.%d.%d.%d.in-addr.arpa", d, c, b, a);
            if (strcasecmp(expected_ptr, query_domain) == 0) {
                return local_hosts[i].domain;
            }
        }
    }
    return NULL;
}

static size_t build_authoritative_response(const uint8_t *req_packet, size_t req_len,
                                          const char *ip_str, uint32_t ttl, uint8_t *out_packet) {
    if (req_len < sizeof(DNSHeader)) return 0;
    memcpy(out_packet, req_packet, req_len);

    DNSHeader *hdr = (DNSHeader *)out_packet;
    hdr->flags = htons(0x8580); // Response + Authoritative + Recursion Avail
    hdr->ancount = htons(1);
    hdr->nscount = 0;
    hdr->arcount = 0;

    uint8_t *ans_ptr = out_packet + req_len;
    *ans_ptr++ = 0xC0; *ans_ptr++ = 0x0C;
    *((uint16_t *)ans_ptr) = htons(DNS_TYPE_A); ans_ptr += 2;
    *((uint16_t *)ans_ptr) = htons(1); ans_ptr += 2;
    *((uint32_t *)ans_ptr) = htonl(ttl); ans_ptr += 4;
    *((uint16_t *)ans_ptr) = htons(4); ans_ptr += 2;

    struct in_addr addr;
    inet_pton(AF_INET, ip_str, &addr);
    memcpy(ans_ptr, &addr.s_addr, 4);
    ans_ptr += 4;
    return ans_ptr - out_packet;
}

static DNSCacheEntry *find_in_cache(const char *domain, uint16_t qtype) {
    time_t now = time(NULL);
    for (int i = 0; i < cache_count; i++) {
        if (dns_cache[i].is_used && dns_cache[i].qtype == qtype && strcasecmp(dns_cache[i].domain, domain) == 0) {
            if (dns_cache[i].expire_time >= now) return &dns_cache[i];
            else dns_cache[i].is_used = 0;
        }
    }
    return NULL;
}

static void save_to_cache(const char *domain, uint16_t qtype, const uint8_t *packet, size_t len, uint32_t ttl) {
    if (len > DNS_PACKET_MAX) return;
    int slot = -1;
    time_t now = time(NULL);
    for (int i = 0; i < cache_count; i++) {
        if (!dns_cache[i].is_used || dns_cache[i].expire_time < now) { slot = i; break; }
    }
    if (slot == -1 && cache_count < MAX_CACHE) slot = cache_count++;
    else if (slot == -1) slot = rand() % MAX_CACHE;

    strncpy(dns_cache[slot].domain, domain, sizeof(dns_cache[0].domain) - 1);
    dns_cache[slot].qtype = qtype;
    memcpy(dns_cache[slot].packet, packet, len);
    dns_cache[slot].packet_len = len;
    dns_cache[slot].expire_time = now + (ttl > 0 ? ttl : 60);
    dns_cache[slot].is_used = 1;
}

static ssize_t forward_dns_query(const char *upstream_ip, const uint8_t *req, size_t req_len,
                                 uint8_t *resp_buf, size_t resp_max, double *out_rtt) {
    int fwd_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fwd_sock < 0) return -1;

    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(fwd_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in up_addr;
    memset(&up_addr, 0, sizeof(up_addr));
    up_addr.sin_family = AF_INET;
    up_addr.sin_port = htons(53);
    inet_pton(AF_INET, upstream_ip, &up_addr.sin_addr);

    double t0 = get_time_sec();
    sendto(fwd_sock, req, req_len, 0, (struct sockaddr *)&up_addr, sizeof(up_addr));

    struct sockaddr_in from;
    socklen_t flen = sizeof(from);
    ssize_t n = recvfrom(fwd_sock, resp_buf, resp_max, 0, (struct sockaddr *)&from, &flen);
    *out_rtt = (get_time_sec() - t0) * 1000.0;
    close(fwd_sock);
    return n;
}

int main(int argc, char *argv[]) {
    utilipc_init();
    init_default_tables();

    int port = (geteuid() == 0) ? 53 : 5353;
    const char *upstream_ip = "1.1.1.1";

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc) port = atoi(argv[++i]);
        else if ((strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--upstream") == 0) && i + 1 < argc) upstream_ip = argv[++i];
    }

    int server_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_fd < 0) return 1;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    srv_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        fprintf(stderr, "dnsserver: erro no bind porta %d: %s\n", port, strerror(errno));
        close(server_fd);
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    start_time_sec = get_time_sec();

    printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s│%s  %s[ 🛡️ dnsserver 2.0 - DNS Autoritativo, AdBlock & Live Keyboard Control ]%s   %s│%s\n",
           COLOR_TITLE, COLOR_RESET, COLOR_OK, COLOR_RESET, COLOR_TITLE, COLOR_RESET);
    printf("%s├────────────────────────────────────────────────────────────────────────────┤%s\n", COLOR_TITLE, COLOR_RESET);
    printf("  • Escutando em       : %s0.0.0.0:%d (UDP)%s\n", COLOR_VAL, port, COLOR_RESET);
    printf("  • Upstream           : %s%s:53%s\n", COLOR_VAL, upstream_ip, COLOR_RESET);
    printf("  • Atalhos de Teclado : %s[P]%s Pausar AdBlock | %s[C]%s Limpar Cache | %s[Ctrl+C]%s Sair\n",
           COLOR_TAG, COLOR_RESET, COLOR_TAG, COLOR_RESET, COLOR_TAG, COLOR_RESET);
    printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n", COLOR_TITLE, COLOR_RESET);

    struct pollfd pfds[2] = {
        { .fd = server_fd, .events = POLLIN },
        { .fd = STDIN_FILENO, .events = POLLIN }
    };

    while (keep_running) {
        int pr = poll(pfds, 2, 500);
        if (pr <= 0) continue;

        // Comandos de teclado em tempo real
        if (pfds[1].revents & POLLIN) {
            char key;
            if (read(STDIN_FILENO, &key, 1) > 0) {
                if (key == 'p' || key == 'P') {
                    adblock_paused = !adblock_paused;
                    printf("  %s[AÇÃO TECLADO]%s AdBlock/Sinkhole %s%s%s!\n",
                           COLOR_TAG, COLOR_RESET, adblock_paused ? COLOR_ERR : COLOR_OK,
                           adblock_paused ? "PAUSADO" : "REATIVADO", COLOR_RESET);
                } else if (key == 'c' || key == 'C') {
                    cache_count = 0;
                    printf("  %s[AÇÃO TECLADO]%s Cache de RAM limpo com sucesso!\n", COLOR_TAG, COLOR_RESET);
                }
            }
        }

        // Requisições DNS recebidas
        if (pfds[0].revents & POLLIN) {
            uint8_t req_buf[DNS_PACKET_MAX];
            struct sockaddr_in client_addr;
            socklen_t clen = sizeof(client_addr);

            ssize_t n = recvfrom(server_fd, req_buf, sizeof(req_buf), 0, (struct sockaddr *)&client_addr, &clen);
            if (n < (ssize_t)sizeof(DNSHeader)) continue;

            total_queries++;
            DNSHeader *hdr = (DNSHeader *)req_buf;
            if (ntohs(hdr->qdcount) < 1) continue;

            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

            const unsigned char *reader = req_buf + sizeof(DNSHeader);
            char query_domain[256] = "";
            reader += read_dns_name(reader, req_buf, n, query_domain);
            uint16_t qtype = ntohs(*((uint16_t *)reader));

            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            char time_str[32];
            strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

            // 1. Sinkhole
            if (is_domain_blocked(query_domain)) {
                blocked_queries++;
                uint8_t resp[DNS_PACKET_MAX];
                size_t rlen = build_authoritative_response(req_buf, n, "0.0.0.0", 300, resp);
                sendto(server_fd, resp, rlen, 0, (struct sockaddr *)&client_addr, clen);
                printf("  \033[0;90m[%s]\033[0m %s[🛑 SINKHOLE]%s  %-15s -> %-30s [%-5s] => \033[1;31m0.0.0.0\033[0m\n",
                       time_str, COLOR_ERR, COLOR_RESET, client_ip, query_domain, type_to_str(qtype));
                continue;
            }

            // 2. Reverse DNS PTR
            if (qtype == DNS_TYPE_PTR && strstr(query_domain, ".in-addr.arpa")) {
                const char *rev = find_ptr_reverse_host(query_domain);
                if (rev) {
                    local_queries++;
                    printf("  \033[0;90m[%s]\033[0m %s[🔄 REVERSE ]%s  %-15s -> %-30s [PTR  ] => %s%s%s\n",
                           time_str, COLOR_OK, COLOR_RESET, client_ip, query_domain, COLOR_VAL, rev, COLOR_RESET);
                }
            }

            // 3. Local Host
            uint32_t local_ttl = 60;
            const char *local_ip = find_local_host(query_domain, &local_ttl);
            if (local_ip && qtype == DNS_TYPE_A) {
                local_queries++;
                uint8_t resp[DNS_PACKET_MAX];
                size_t rlen = build_authoritative_response(req_buf, n, local_ip, local_ttl, resp);
                sendto(server_fd, resp, rlen, 0, (struct sockaddr *)&client_addr, clen);
                printf("  \033[0;90m[%s]\033[0m %s[🏠 LOCAL LAN]%s %-15s -> %-30s [%-5s] => %s%s%s\n",
                       time_str, COLOR_OK, COLOR_RESET, client_ip, query_domain, type_to_str(qtype), COLOR_VAL, local_ip, COLOR_RESET);
                continue;
            }

            // 4. Cache
            DNSCacheEntry *cached = find_in_cache(query_domain, qtype);
            if (cached) {
                cached_queries++;
                DNSHeader *c_hdr = (DNSHeader *)cached->packet;
                c_hdr->id = hdr->id;
                sendto(server_fd, cached->packet, cached->packet_len, 0, (struct sockaddr *)&client_addr, clen);
                printf("  \033[0;90m[%s]\033[0m %s[⚡ RAM CACHE]%s %-15s -> %-30s [%-5s] => (Hit <0.1ms)\n",
                       time_str, COLOR_VAL, COLOR_RESET, client_ip, query_domain, type_to_str(qtype));
                continue;
            }

            // 5. Upstream
            uint8_t fwd_resp[DNS_PACKET_MAX];
            double rtt = 0.0;
            ssize_t fwd_len = forward_dns_query(upstream_ip, req_buf, n, fwd_resp, sizeof(fwd_resp), &rtt);
            if (fwd_len > 0) {
                forwarded_queries++;
                sendto(server_fd, fwd_resp, fwd_len, 0, (struct sockaddr *)&client_addr, clen);
                save_to_cache(query_domain, qtype, fwd_resp, fwd_len, 120);
                printf("  \033[0;90m[%s]\033[0m %s[🌐 FORWARD  ]%s %-15s -> %-30s [%-5s] => (via %s: %.1f ms)\n",
                       time_str, COLOR_LABEL, COLOR_RESET, client_ip, query_domain, type_to_str(qtype), upstream_ip, rtt);
            }
        }
    }
    close(server_fd);
    utilipc_close();
    return 0;
}
