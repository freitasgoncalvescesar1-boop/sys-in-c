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

// Métricas do Servidor
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

static void print_help(void) {
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ dnsserver 1.0 - Authoritative DNS, High-Speed Cache & AdBlock Sinkhole ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  dnsserver [OPTIONS]\n\n");
    printf("Options:\n");
    printf("  %s-p, --port <PORT>%s         UDP Port to bind [Default: 53 if root, 5353 if user]\n", COLOR_OK, COLOR_RESET);
    printf("  %s-u, --upstream <IP>%s       Upstream DNS resolver [Default: 1.1.1.1]\n", COLOR_OK, COLOR_RESET);
    printf("  %s-f, --hosts <FILE>%s        Load custom domain-to-IP hosts file\n", COLOR_OK, COLOR_RESET);
    printf("  %s-b, --blocklist <FILE>%s    Load custom sinkhole / adblock domain list\n", COLOR_OK, COLOR_RESET);
    printf("  %s--no-sinkhole%s             Disable built-in ad/telemetry sinkhole\n", COLOR_OK, COLOR_RESET);
    printf("  %s--no-forward%s              Authoritative only mode (never forward to internet)\n", COLOR_OK, COLOR_RESET);
    printf("  %s-h, --help%s                Display this formatted help guide and exit\n\n", COLOR_OK, COLOR_RESET);
    printf("Built-in Local Domains:\n");
    printf("  • %sservidor.lan%s   -> 192.168.1.100\n", COLOR_TAG, COLOR_RESET);
    printf("  • %sroteador.local%s -> 192.168.1.1\n", COLOR_TAG, COLOR_RESET);
    printf("  • %smeusite.test%s   -> 127.0.0.1\n\n", COLOR_TAG, COLOR_RESET);
    printf("Exemplos:\n");
    printf("  • %s./dnsserver%s                         (Inicia servidor DNS na porta 5353/53)\n", COLOR_TAG, COLOR_RESET);
    printf("  • %s./dnsserver -p 53 -u 8.8.8.8%s        (Modo Root com upstream Google)\n", COLOR_TAG, COLOR_RESET);
    printf("  • %s./dnsquery servidor.lan A -s 127.0.0.1%s (Testa consulta local)\n", COLOR_TAG, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
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
    // Domínios Locais Embutidos
    add_local_host("servidor.lan", "192.168.1.100", 60);
    add_local_host("roteador.local", "192.168.1.1", 60);
    add_local_host("meusite.test", "127.0.0.1", 60);
    add_local_host("termux.local", "127.0.0.1", 60);
    add_local_host("sysbox.local", "127.0.0.1", 60);

    // Domínios de Bloqueio (Sinkhole / Anti-Rastreador)
    add_blocklist_domain("telemetry.google.com");
    add_blocklist_domain("metrics.icloud.com");
    add_blocklist_domain("ads.facebook.com");
    add_blocklist_domain("graph.instagram.com");
    add_blocklist_domain("analytics.yahoo.com");
    add_blocklist_domain("tracking.tiktok.com");
    add_blocklist_domain("adservice.google.com");
    add_blocklist_domain("doubleclick.net");
    add_blocklist_domain("pagead2.googlesyndication.com");
}

static void load_hosts_file(const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) return;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\0' || *p == '\n') continue;

        char ip[64] = "", dom[128] = "";
        if (sscanf(p, "%63s %127s", ip, dom) == 2) {
            add_local_host(dom, ip, 60);
        }
    }
    fclose(fp);
}

static void load_blocklist_file(const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) return;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\0' || *p == '\n') continue;
        size_t l = strlen(p);
        while (l > 0 && isspace((unsigned char)p[l - 1])) p[--l] = '\0';
        if (l > 0) add_blocklist_domain(p);
    }
    fclose(fp);
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
    if (t == DNS_TYPE_MX) return "MX";
    if (t == DNS_TYPE_TXT) return "TXT";
    if (t == DNS_TYPE_CNAME) return "CNAME";
    if (t == DNS_TYPE_NS) return "NS";
    return "QUERY";
}

// Verifica se o domínio está na lista de bloqueio
static int is_domain_blocked(const char *domain) {
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

// Localiza IP no roteador local
static const char *find_local_host(const char *domain, uint32_t *out_ttl) {
    for (int i = 0; i < local_hosts_count; i++) {
        if (strcasecmp(domain, local_hosts[i].domain) == 0) {
            *out_ttl = local_hosts[i].ttl;
            return local_hosts[i].ip;
        }
    }
    return NULL;
}

// Monta resposta DNS autoritativa / local (IPv4 A Record)
static size_t build_authoritative_response(const uint8_t *req_packet, size_t req_len,
                                          const char *ip_str, uint32_t ttl,
                                          uint8_t *out_packet) {
    if (req_len < sizeof(DNSHeader)) return 0;

    memcpy(out_packet, req_packet, req_len);

    DNSHeader *hdr = (DNSHeader *)out_packet;
    hdr->flags = htons(0x8180); // Response + Recursion Available + No Error
    hdr->ancount = htons(1);
    hdr->nscount = 0;
    hdr->arcount = 0;

    uint8_t *ans_ptr = out_packet + req_len;

    // Nome: Ponteiro para o nome da questão no offset 12 (0xC00C)
    *ans_ptr++ = 0xC0;
    *ans_ptr++ = 0x0C;

    // TYPE (A = 0x0001)
    *((uint16_t *)ans_ptr) = htons(DNS_TYPE_A); ans_ptr += 2;
    // CLASS (IN = 0x0001)
    *((uint16_t *)ans_ptr) = htons(1); ans_ptr += 2;
    // TTL
    *((uint32_t *)ans_ptr) = htonl(ttl); ans_ptr += 4;
    // RDLENGTH (4 bytes para IPv4)
    *((uint16_t *)ans_ptr) = htons(4); ans_ptr += 2;

    // RDATA (IP binário)
    struct in_addr addr;
    inet_pton(AF_INET, ip_str, &addr);
    memcpy(ans_ptr, &addr.s_addr, 4);
    ans_ptr += 4;

    return ans_ptr - out_packet;
}

// Busca resposta no Cache em RAM
static DNSCacheEntry *find_in_cache(const char *domain, uint16_t qtype) {
    time_t now = time(NULL);
    for (int i = 0; i < cache_count; i++) {
        if (dns_cache[i].is_used && dns_cache[i].qtype == qtype &&
            strcasecmp(dns_cache[i].domain, domain) == 0) {
            if (dns_cache[i].expire_time >= now) {
                return &dns_cache[i];
            } else {
                dns_cache[i].is_used = 0;
            }
        }
    }
    return NULL;
}

// Salva resposta no Cache em RAM
static void save_to_cache(const char *domain, uint16_t qtype, const uint8_t *packet, size_t len, uint32_t ttl) {
    if (len > DNS_PACKET_MAX) return;

    int slot = -1;
    time_t now = time(NULL);

    for (int i = 0; i < cache_count; i++) {
        if (!dns_cache[i].is_used || dns_cache[i].expire_time < now) {
            slot = i;
            break;
        }
    }

    if (slot == -1 && cache_count < MAX_CACHE) {
        slot = cache_count++;
    } else if (slot == -1) {
        slot = rand() % MAX_CACHE;
    }

    strncpy(dns_cache[slot].domain, domain, sizeof(dns_cache[0].domain) - 1);
    dns_cache[slot].qtype = qtype;
    memcpy(dns_cache[slot].packet, packet, len);
    dns_cache[slot].packet_len = len;
    dns_cache[slot].expire_time = now + (ttl > 0 ? ttl : 60);
    dns_cache[slot].is_used = 1;
}

// Encaminha a consulta para o servidor Upstream (1.1.1.1 ou 8.8.8.8)
static ssize_t forward_dns_query(const char *upstream_ip, const uint8_t *req, size_t req_len,
                                 uint8_t *resp_buf, size_t resp_max, double *out_rtt) {
    int fwd_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fwd_sock < 0) return -1;

    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(fwd_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fwd_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

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

static void print_server_summary(int port, const char *upstream_ip) {
    double uptime = get_time_sec() - start_time_sec;
    double block_rate = (total_queries > 0) ? ((double)blocked_queries / (double)total_queries) * 100.0 : 0.0;
    double cache_hit_rate = (total_queries > 0) ? ((double)cached_queries / (double)total_queries) * 100.0 : 0.0;
    double qps = (uptime > 0.0) ? ((double)total_queries / uptime) : 0.0;

    printf("\n%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ dnsserver - Relatório de Atividade & Estatísticas da Sessão ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n\n", COLOR_TITLE, COLOR_RESET);
    printf("  • Porta UDP        : %s%d%s\n", COLOR_VAL, port, COLOR_RESET);
    printf("  • Upstream DNS     : %s%s:53%s\n", COLOR_VAL, upstream_ip, COLOR_RESET);
    printf("  • Uptime Total     : %s%.2f segundos%s\n", COLOR_VAL, uptime, COLOR_RESET);
    printf("  • Total de Queries : %s%lu chamadas%s (Média: %s%.2f QPS%s)\n", COLOR_VAL, total_queries, COLOR_RESET, COLOR_VAL, qps, COLOR_RESET);
    printf("  ---------------------------------------------------------------------------------\n");
    printf("  • Domínios Locais  : %s%lu atendidos%s\n", COLOR_OK, local_queries, COLOR_RESET);
    printf("  • Respostas Cache  : %s%lu servidas%s (%s%.1f%% Cache Hit%s)\n", COLOR_OK, cached_queries, COLOR_RESET, COLOR_VAL, cache_hit_rate, COLOR_RESET);
    printf("  • Consultas Upstream: %s%lu encaminhadas%s\n", COLOR_VAL, forwarded_queries, COLOR_RESET);
    printf("  • Anúncios/Bloqueios: %s%lu sinkholed%s (%s%.1f%% Bloqueados%s)\n", COLOR_ERR, blocked_queries, COLOR_RESET, COLOR_WARN, block_rate, COLOR_RESET);
    printf("%s=================================================================================%s\n\n", COLOR_TITLE, COLOR_RESET);
}

int main(int argc, char *argv[]) {
    utilipc_init();
    init_default_tables();

    int port = (geteuid() == 0) ? 53 : 5353;
    const char *upstream_ip = "1.1.1.1";
    int enable_sinkhole = 1;
    int enable_forward = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            utilipc_close();
            return 0;
        }

        if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--upstream") == 0) && i + 1 < argc) {
            upstream_ip = argv[++i];
        } else if ((strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--hosts") == 0) && i + 1 < argc) {
            load_hosts_file(argv[++i]);
        } else if ((strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--blocklist") == 0) && i + 1 < argc) {
            load_blocklist_file(argv[++i]);
        } else if (strcmp(argv[i], "--no-sinkhole") == 0) {
            enable_sinkhole = 0;
        } else if (strcmp(argv[i], "--no-forward") == 0) {
            enable_forward = 0;
        }
    }

    int server_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_fd < 0) {
        perror("dnsserver: socket");
        utilipc_close();
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    srv_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        fprintf(stderr, "\n  %s[ERRO]%s Nao foi possivel escutar na porta UDP %d: %s\n", COLOR_ERR, COLOR_RESET, port, strerror(errno));
        if (port == 53 && geteuid() != 0) {
            fprintf(stderr, "  %s[DICA]%s Portas abaixo de 1024 exigem root. Tente rodar com '-p 5353'\n\n", COLOR_WARN, COLOR_RESET);
        }
        close(server_fd);
        utilipc_close();
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    start_time_sec = get_time_sec();

    printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s│%s  %s[ 🛡️ dnsserver 1.0 - Servidor DNS Autoritativo, Cache & Sinkhole ]%s        %s│%s\n",
           COLOR_TITLE, COLOR_RESET, COLOR_OK, COLOR_RESET, COLOR_TITLE, COLOR_RESET);
    printf("%s├────────────────────────────────────────────────────────────────────────────┤%s\n", COLOR_TITLE, COLOR_RESET);
    printf("  %s• Porta UDP Ativa   :%s %s%d%s (%s)\n", COLOR_LABEL, COLOR_RESET, COLOR_VAL, port, COLOR_RESET, (port == 53) ? "Padrão DNS" : "Porta de Usuário");
    printf("  %s• Upstream Resolver :%s %s%s:53%s\n", COLOR_LABEL, COLOR_RESET, COLOR_VAL, upstream_ip, COLOR_RESET);
    printf("  %s• Domínios Locais   :%s %s%d ativos%s (servidor.lan, roteador.local, meusite.test)\n", COLOR_LABEL, COLOR_RESET, COLOR_OK, local_hosts_count, COLOR_RESET);
    printf("  %s• Filtro AdBlock    :%s %s%d domínios de rastreadores bloqueados (0.0.0.0)%s\n", COLOR_LABEL, COLOR_RESET, COLOR_WARN, blocklist_count, COLOR_RESET);
    printf("  %s• Cache em RAM      :%s %sAtivo (Respostas instantâneas em < 0.2ms)%s\n", COLOR_LABEL, COLOR_RESET, COLOR_OK, COLOR_RESET);
    printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n", COLOR_TITLE, COLOR_RESET);
    printf("  \033[0;90m[Aguardando requisições DNS... Pressione Ctrl+C para encerrar]\033[0m\n\n");

    while (keep_running) {
        struct pollfd pfd;
        pfd.fd = server_fd;
        pfd.events = POLLIN;

        int pr = poll(&pfd, 1, 500);
        if (pr <= 0) continue;

        uint8_t req_buf[DNS_PACKET_MAX];
        struct sockaddr_in client_addr;
        socklen_t clen = sizeof(client_addr);

        ssize_t n = recvfrom(server_fd, req_buf, sizeof(req_buf), 0, (struct sockaddr *)&client_addr, &clen);
        if (n < (ssize_t)sizeof(DNSHeader)) continue;

        total_queries++;
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

        DNSHeader *hdr = (DNSHeader *)req_buf;
        uint16_t qdcount = ntohs(hdr->qdcount);

        if (qdcount < 1) continue;

        const unsigned char *reader = req_buf + sizeof(DNSHeader);
        char query_domain[256] = "";
        reader += read_dns_name(reader, req_buf, n, query_domain);
        uint16_t qtype = ntohs(*((uint16_t *)reader));

        // 1. Verificação de Sinkhole / Bloqueio de Anúncios
        if (enable_sinkhole && is_domain_blocked(query_domain)) {
            blocked_queries++;
            uint8_t resp[DNS_PACKET_MAX];
            size_t rlen = build_authoritative_response(req_buf, n, "0.0.0.0", 300, resp);
            sendto(server_fd, resp, rlen, 0, (struct sockaddr *)&client_addr, clen);

            printf("  \033[0;90m[%s]\033[0m %s[🛑 SINKHOLE]%s  %-15s -> %-30s [%-5s] => \033[1;31m0.0.0.0 (BLOQUEADO)\033[0m\n",
                   time_str, COLOR_ERR, COLOR_RESET, client_ip, query_domain, type_to_str(qtype));
            continue;
        }

        // 2. Verificação de Domínio Local (Autoritativo)
        uint32_t local_ttl = 60;
        const char *local_ip = find_local_host(query_domain, &local_ttl);
        if (local_ip && qtype == DNS_TYPE_A) {
            local_queries++;
            uint8_t resp[DNS_PACKET_MAX];
            size_t rlen = build_authoritative_response(req_buf, n, local_ip, local_ttl, resp);
            sendto(server_fd, resp, rlen, 0, (struct sockaddr *)&client_addr, clen);

            printf("  \033[0;90m[%s]\033[0m %s[🏠 LOCAL LAN]%s %-15s -> %-30s [%-5s] => %s%s%s (Local Host)\n",
                   time_str, COLOR_OK, COLOR_RESET, client_ip, query_domain, type_to_str(qtype), COLOR_VAL, local_ip, COLOR_RESET);
            continue;
        }

        // 3. Verificação de Cache em RAM
        DNSCacheEntry *cached = find_in_cache(query_domain, qtype);
        if (cached) {
            cached_queries++;
            DNSHeader *c_hdr = (DNSHeader *)cached->packet;
            c_hdr->id = hdr->id;
            sendto(server_fd, cached->packet, cached->packet_len, 0, (struct sockaddr *)&client_addr, clen);

            printf("  \033[0;90m[%s]\033[0m %s[⚡ RAM CACHE]%s %-15s -> %-30s [%-5s] => %s(Cache Hit <0.1ms)%s\n",
                   time_str, COLOR_VAL, COLOR_RESET, client_ip, query_domain, type_to_str(qtype), COLOR_OK, COLOR_RESET);
            continue;
        }

        // 4. Encaminhamento Upstream
        if (enable_forward) {
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
    print_server_summary(port, upstream_ip);

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "dnsserver: served %lu queries (%lu blocked, %lu cached)",
             total_queries, blocked_queries, cached_queries);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
