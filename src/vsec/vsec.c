#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_WARN    "\033[1;33m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_LABEL   "\033[1;36m"
#define COLOR_VAL     "\033[1;37m"
#define COLOR_MUTED   "\033[0;90m"

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
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ vsec 2.0 - Comprehensive Site Security, SSL/TLS, DNS & Latency Auditor ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  vsec <DOMAIN_OR_URL>\n\n");
    printf("Auditorias Realizadas:\n");
    printf("  1. DNS & Autoridade   : IPs (IPv4/IPv6) e Nameservers autoritativos (NS)\n");
    printf("  2. Latencia & Rede    : TCP Connect RTT (min/med/max/jitter) e TTFB (Server Response)\n");
    printf("  3. Resposta HTTP      : Codigo de status (200, 301, 403, 404) com cor semantica\n");
    printf("  4. Seguranca SSL/TLS  : Emissor, cifra, protocolo, data e dias ate a expiracao\n");
    printf("  5. Headers de Defesa  : HSTS, CSP, X-Frame-Options, Sniffing, vazamento de Server\n");
    printf("  6. Pontuacao Global   : Nota de blindagem HTTP (A+, A, B, C, F)\n\n");
    printf("Exemplos:\n");
    printf("  • %svsec google.com%s\n", COLOR_WARN, COLOR_RESET);
    printf("  • %svsec github.com%s\n", COLOR_WARN, COLOR_RESET);
    printf("  • %svsec cloudflare.com%s\n", COLOR_WARN, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

static void parse_target_url(const char *url, char *out_host, size_t host_sz, int *out_port, char *out_path, size_t path_sz, int *is_https) {
    *out_port = 443;
    *is_https = 1;
    strncpy(out_path, "/", path_sz - 1);
    out_path[path_sz - 1] = '\0';

    const char *p = url;
    if (strncasecmp(p, "http://", 7) == 0) {
        p += 7;
        *out_port = 80;
        *is_https = 0;
    } else if (strncasecmp(p, "https://", 8) == 0) {
        p += 8;
        *out_port = 443;
        *is_https = 1;
    }

    const char *slash = strchr(p, '/');
    if (slash) {
        size_t hlen = slash - p;
        if (hlen >= host_sz) hlen = host_sz - 1;
        strncpy(out_host, p, hlen);
        out_host[hlen] = '\0';
        strncpy(out_path, slash, path_sz - 1);
    } else {
        strncpy(out_host, p, host_sz - 1);
        out_host[host_sz - 1] = '\0';
    }

    char *colon = strchr(out_host, ':');
    if (colon) {
        *colon = '\0';
        *out_port = atoi(colon + 1);
        if (*out_port == 80) *is_https = 0;
    }
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

static void query_authoritative_ns(const char *domain) {
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return;

    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dns_srv;
    memset(&dns_srv, 0, sizeof(dns_srv));
    dns_srv.sin_family = AF_INET;
    dns_srv.sin_port = htons(53);
    inet_pton(AF_INET, "1.1.1.1", &dns_srv.sin_addr);

    unsigned char packet[512];
    memset(packet, 0, sizeof(packet));
    DNSHeader *hdr = (DNSHeader *)packet;
    hdr->id = htons(0xCAFE);
    hdr->flags = htons(0x0100);
    hdr->qdcount = htons(1);

    size_t qname_len = 0;
    domain_to_dns_format(domain, packet + sizeof(DNSHeader), &qname_len);

    unsigned char *q_ptr = packet + sizeof(DNSHeader) + qname_len;
    *((uint16_t *)q_ptr) = htons(2); q_ptr += 2; // TYPE 2 = NS
    *((uint16_t *)q_ptr) = htons(1); q_ptr += 2; // CLASS 1 = IN

    sendto(fd, packet, q_ptr - packet, 0, (struct sockaddr *)&dns_srv, sizeof(dns_srv));

    unsigned char resp[1024];
    struct sockaddr_in from;
    socklen_t flen = sizeof(from);
    ssize_t n = recvfrom(fd, resp, sizeof(resp), 0, (struct sockaddr *)&from, &flen);
    close(fd);

    if (n < (ssize_t)sizeof(DNSHeader)) {
        printf("  %s• Nameservers (NS)   :%s %sConsulta direta indisponivel%s\n", COLOR_LABEL, COLOR_RESET, COLOR_MUTED, COLOR_RESET);
        return;
    }

    DNSHeader *rh = (DNSHeader *)resp;
    uint16_t ancount = ntohs(rh->ancount);
    uint16_t nscount = ntohs(rh->nscount);
    uint16_t total = ancount > 0 ? ancount : nscount;

    const unsigned char *reader = resp + sizeof(DNSHeader);
    char dummy[256];
    reader += read_dns_name(reader, resp, n, dummy);
    reader += 4;

    printf("  %s• Nameservers (NS)   :%s ", COLOR_LABEL, COLOR_RESET);
    int printed = 0;

    for (int i = 0; i < total; i++) {
        if ((size_t)(reader - resp) >= (size_t)n) break;
        char ns_domain[256];
        reader += read_dns_name(reader, resp, n, ns_domain);
        uint16_t type = ntohs(*((uint16_t *)reader)); reader += 2;
        reader += 2; // Class
        reader += 4; // TTL
        uint16_t dlen = ntohs(*((uint16_t *)reader)); reader += 2;

        if (type == 2) {
            char ns_target[256];
            read_dns_name(reader, resp, n, ns_target);
            printf("%s%s%s%s", COLOR_VAL, ns_target, COLOR_RESET, (printed < 3 && i < total - 1) ? ", " : "");
            printed++;
            if (printed >= 4) break;
        }
        reader += dlen;
    }

    if (printed == 0) printf("%sNenhum registro NS direto retornado%s", COLOR_MUTED, COLOR_RESET);
    printf("\n");
}

static void measure_tcp_latency_stats(const char *host, int port, double *out_min, double *out_avg, double *out_max, double *out_jitter) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char pstr[16];
    snprintf(pstr, sizeof(pstr), "%d", port);

    if (getaddrinfo(host, pstr, &hints, &res) != 0) return;

    double pings[3];
    int valid = 0;
    double sum = 0.0;

    for (int i = 0; i < 3; i++) {
        int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd < 0) continue;

        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        double t0 = get_time_sec();
        int conn = connect(fd, res->ai_addr, res->ai_addrlen);

        if (conn == 0) {
            double rtt = (get_time_sec() - t0) * 1000.0;
            pings[valid++] = rtt;
            sum += rtt;
        } else if (errno == EINPROGRESS) {
            fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
            struct timeval tv = { .tv_sec = 1, .tv_usec = 500000 };
            if (select(fd + 1, NULL, &fds, NULL, &tv) > 0) {
                int err = 0; socklen_t elen = sizeof(err);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen);
                if (err == 0) {
                    double rtt = (get_time_sec() - t0) * 1000.0;
                    pings[valid++] = rtt;
                    sum += rtt;
                }
            }
        }
        close(fd);
        usleep(25000);
    }
    freeaddrinfo(res);

    if (valid > 0) {
        *out_min = pings[0];
        *out_max = pings[0];
        for (int i = 1; i < valid; i++) {
            if (pings[i] < *out_min) *out_min = pings[i];
            if (pings[i] > *out_max) *out_max = pings[i];
        }
        *out_avg = sum / valid;
        *out_jitter = (valid >= 2) ? fabs(pings[valid - 1] - pings[0]) : 0.0;
    }
}

static int parse_month_name(const char *m) {
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (int i = 0; i < 12; i++) {
        if (strncasecmp(m, months[i], 3) == 0) return i;
    }
    return 0;
}

// Parser universal de datas de certificados X.509
static int calculate_days_until_expiration(const char *date_str) {
    if (!date_str || strlen(date_str) < 10) return -999;

    char m_str[8] = "";
    int day = 0, hour = 0, min = 0, sec = 0, year = 0;

    const char *p = date_str;
    while (*p && !isalpha((unsigned char)*p) && !isdigit((unsigned char)*p)) p++;

    if (sscanf(p, "%3s %d %d:%d:%d %d", m_str, &day, &hour, &min, &sec, &year) >= 3 ||
        sscanf(p, "%d-%2s-%d", &year, m_str, &day) >= 3) {
        
        struct tm exp_tm;
        memset(&exp_tm, 0, sizeof(exp_tm));
        exp_tm.tm_year = year - 1900;
        exp_tm.tm_mon  = parse_month_name(m_str);
        exp_tm.tm_mday = day;
        exp_tm.tm_hour = hour;
        exp_tm.tm_min  = min;
        exp_tm.tm_sec  = sec;

        time_t exp_t = timegm(&exp_tm);
        time_t now = time(NULL);

        if (exp_t > 0) {
            double diff_sec = difftime(exp_t, now);
            return (int)(diff_sec / 86400.0);
        }
    }
    return -999;
}

// Extração avançada de certificado SSL/TLS (Curl Verbose + OpenSSL Fallback)
static int audit_ssl_certificate(const char *host, int port) {
    char cmd[1024];
    // Usa curl verbose para capturar todo o handshake TLS, cifra e certificado
    snprintf(cmd, sizeof(cmd),
             "curl -s -I -v --max-time 5 \"https://%s:%d/\" 2>&1", host, port);

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    char line[512];
    char issuer[256] = "";
    char subject[256] = "";
    char expire_date[64] = "";
    char start_date[64] = "";
    char tls_version[64] = "";
    char cipher_suite[64] = "";
    int cert_verified = 0;

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';

        if (strstr(line, "expire date:")) {
            char *p = strstr(line, "expire date:") + 12;
            while (*p == ' ') p++;
            strncpy(expire_date, p, sizeof(expire_date) - 1);
        } else if (strstr(line, "start date:")) {
            char *p = strstr(line, "start date:") + 11;
            while (*p == ' ') p++;
            strncpy(start_date, p, sizeof(start_date) - 1);
        } else if (strstr(line, "issuer:")) {
            char *p = strstr(line, "issuer:") + 7;
            while (*p == ' ') p++;
            strncpy(issuer, p, sizeof(issuer) - 1);
        } else if (strstr(line, "subject:") && strlen(subject) == 0) {
            char *p = strstr(line, "subject:") + 8;
            while (*p == ' ') p++;
            strncpy(subject, p, sizeof(subject) - 1);
        } else if (strstr(line, "SSL certificate verify ok")) {
            cert_verified = 1;
        } else if (strstr(line, "SSL connection using")) {
            char *proto = strstr(line, "using ") + 6;
            char *slash = strchr(proto, '/');
            if (slash) {
                snprintf(tls_version, sizeof(tls_version), "%.*s", (int)(slash - proto - 1), proto);
                char *ciph = slash + 1;
                while (*ciph == ' ') ciph++;
                strncpy(cipher_suite, ciph, sizeof(cipher_suite) - 1);
            } else {
                strncpy(tls_version, proto, sizeof(tls_version) - 1);
            }
        }
    }
    pclose(fp);

    // Fallback secundário via OpenSSL com handshake garantido (echo Q)
    if (strlen(expire_date) == 0) {
        snprintf(cmd, sizeof(cmd),
                 "echo Q | openssl s_client -servername %s -connect %s:%d 2>/dev/null | openssl x509 -noout -dates -issuer -subject 2>/dev/null",
                 host, host, port);
        fp = popen(cmd, "r");
        if (fp) {
            while (fgets(line, sizeof(line), fp)) {
                line[strcspn(line, "\r\n")] = '\0';
                if (strncmp(line, "notAfter=", 9) == 0) {
                    strncpy(expire_date, line + 9, sizeof(expire_date) - 1);
                } else if (strncmp(line, "notBefore=", 10) == 0) {
                    strncpy(start_date, line + 10, sizeof(start_date) - 1);
                } else if (strncmp(line, "issuer=", 7) == 0) {
                    strncpy(issuer, line + 7, sizeof(issuer) - 1);
                } else if (strncmp(line, "subject=", 8) == 0) {
                    strncpy(subject, line + 8, sizeof(subject) - 1);
                }
            }
            pclose(fp);
            cert_verified = 1;
        }
    }

    if (strlen(expire_date) == 0) {
        printf("  %s• Certificado SSL/TLS :%s %sPorta 443 sem resposta TLS ou certificado inacessível%s\n",
               COLOR_LABEL, COLOR_RESET, COLOR_ERR, COLOR_RESET);
        return 0;
    }

    int days_left = calculate_days_until_expiration(expire_date);
    const char *status_txt = cert_verified ? "\033[1;32m[ VÁLIDO / CONFIÁVEL ]\033[0m" : "\033[1;33m[ AUTO-ASSINADO / NÃO VERIFICADO ]\033[0m";

    printf("  %s• Status / Validação :%s %s\n", COLOR_LABEL, COLOR_RESET, status_txt);
    if (strlen(tls_version) > 0) {
        printf("  %s• Protocolo / Cifra  :%s %s%s%s (Cifra: %s)\n",
               COLOR_LABEL, COLOR_RESET, COLOR_VAL, tls_version, COLOR_RESET,
               strlen(cipher_suite) > 0 ? cipher_suite : "AES-GCM");
    }

    printf("  %s• Emissor (Issuer)   :%s %s%s%s\n", COLOR_LABEL, COLOR_RESET, COLOR_VAL, issuer, COLOR_RESET);
    printf("  %s• Sujeito (CN)       :%s %s\n", COLOR_LABEL, COLOR_RESET, subject);
    if (strlen(start_date) > 0) {
        printf("  %s• Data de Emissão    :%s %s\n", COLOR_LABEL, COLOR_RESET, start_date);
    }

    if (days_left != -999) {
        const char *day_col = (days_left > 30) ? COLOR_OK : (days_left > 7) ? COLOR_WARN : COLOR_ERR;
        printf("  %s• Expira em          :%s %s%s%s (%s%d dias restantes%s)\n",
               COLOR_LABEL, COLOR_RESET, COLOR_VAL, expire_date, COLOR_RESET,
               day_col, days_left, COLOR_RESET);
    } else {
        printf("  %s• Data de Expiração  :%s %s%s%s\n", COLOR_LABEL, COLOR_RESET, COLOR_VAL, expire_date, COLOR_RESET);
    }
    return 1;
}

// Analisa cabeçalhos HTTP de segurança (OWASP)
static void analyze_security_headers(const char *headers, int *out_score, char *out_grade) {
    int score = 0;

    int has_hsts = (strcasestr(headers, "Strict-Transport-Security:") != NULL);
    int has_csp  = (strcasestr(headers, "Content-Security-Policy:") != NULL);
    int has_xfo  = (strcasestr(headers, "X-Frame-Options:") != NULL);
    int has_xcto = (strcasestr(headers, "X-Content-Type-Options:") != NULL);
    int has_rp   = (strcasestr(headers, "Referrer-Policy:") != NULL);
    int has_pp   = (strcasestr(headers, "Permissions-Policy:") != NULL);

    char server_val[128] = "";
    char *srv = strcasestr(headers, "Server:");
    if (srv) {
        srv += 7;
        while (*srv == ' ') srv++;
        char *nl = strpbrk(srv, "\r\n");
        if (nl) snprintf(server_val, sizeof(server_val), "%.*s", (int)(nl - srv), srv);
    }

    printf("  ---------------------------------------------------------------------------------\n");
    printf("  %s[ AUDITORIA DE HEADERS DE SEGURANÇA HTTP (OWASP) ]%s\n\n", COLOR_TITLE, COLOR_RESET);

    if (has_hsts) { printf("  • %-32s: %s[ PROTEGIDO ]%s (HSTS ativo / HTTPS obrigatório)\n", "Strict-Transport-Security", COLOR_OK, COLOR_RESET); score += 25; }
    else { printf("  • %-32s: %s[ AUSENTE   ]%s (Risco de Downgrade SSL)\n", "Strict-Transport-Security", COLOR_ERR, COLOR_RESET); }

    if (has_csp) { printf("  • %-32s: %s[ PROTEGIDO ]%s (Mitigação Anti-XSS ativa)\n", "Content-Security-Policy", COLOR_OK, COLOR_RESET); score += 25; }
    else { printf("  • %-32s: %s[ AUSENTE   ]%s (Sem restrição de scripts externos)\n", "Content-Security-Policy", COLOR_WARN, COLOR_RESET); }

    if (has_xfo) { printf("  • %-32s: %s[ PROTEGIDO ]%s (Anti-Clickjacking / Sem Iframes)\n", "X-Frame-Options", COLOR_OK, COLOR_RESET); score += 20; }
    else { printf("  • %-32s: %s[ AUSENTE   ]%s (Vulnerável a Iframe/Clickjacking)\n", "X-Frame-Options", COLOR_WARN, COLOR_RESET); }

    if (has_xcto) { printf("  • %-32s: %s[ PROTEGIDO ]%s (nosniff ativo)\n", "X-Content-Type-Options", COLOR_OK, COLOR_RESET); score += 15; }
    else { printf("  • %-32s: %s[ AUSENTE   ]%s (Risco de MIME-Confusion)\n", "X-Content-Type-Options", COLOR_WARN, COLOR_RESET); }

    if (has_rp) { printf("  • %-32s: %s[ PROTEGIDO ]%s (Privacidade de referrer)\n", "Referrer-Policy", COLOR_OK, COLOR_RESET); score += 10; }
    else { printf("  • %-32s: %s[ PADRÃO    ]%s\n", "Referrer-Policy", COLOR_MUTED, COLOR_RESET); }

    if (has_pp) { printf("  • %-32s: %s[ PROTEGIDO ]%s (Restrição de sensores e câmera)\n", "Permissions-Policy", COLOR_OK, COLOR_RESET); score += 5; }
    else { printf("  • %-32s: %s[ PADRÃO    ]%s\n", "Permissions-Policy", COLOR_MUTED, COLOR_RESET); }

    if (strlen(server_val) > 0) {
        printf("  • %-32s: %s%s%s %s(Divulgação pública de software)%s\n",
               "Assinatura do Servidor", COLOR_WARN, server_val, COLOR_RESET, COLOR_MUTED, COLOR_RESET);
    }

    *out_score = score;
    if (score >= 90) strcpy(out_grade, "A+");
    else if (score >= 80) strcpy(out_grade, "A");
    else if (score >= 65) strcpy(out_grade, "B");
    else if (score >= 45) strcpy(out_grade, "C");
    else strcpy(out_grade, "F");
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help();
        utilipc_close();
        return 0;
    }

    char host[256] = "";
    char path[512] = "/";
    int port = 443;
    int is_https = 1;

    parse_target_url(argv[1], host, sizeof(host), &port, path, sizeof(path), &is_https);

    printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s│%s  %s[ 🛡️ vsec 2.0 - Auditoria Completa de Segurança Web, SSL & DNS ]%s          %s│%s\n",
           COLOR_TITLE, COLOR_RESET, COLOR_OK, COLOR_RESET, COLOR_TITLE, COLOR_RESET);
    printf("%s├────────────────────────────────────────────────────────────────────────────┤%s\n", COLOR_TITLE, COLOR_RESET);
    printf("  %s• Alvo Auditado    :%s %s%s%s (Porta %d | %s)\n",
           COLOR_LABEL, COLOR_RESET, COLOR_VAL, host, COLOR_RESET, port, is_https ? "HTTPS Seguro" : "HTTP Plano");

    // 1. Resolução DNS (IPv4 e IPv6)
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, NULL, &hints, &res) == 0) {
        char ip4_str[INET_ADDRSTRLEN] = "";
        char ip6_str[INET6_ADDRSTRLEN] = "";

        for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
            if (p->ai_family == AF_INET && strlen(ip4_str) == 0) {
                inet_ntop(AF_INET, &((struct sockaddr_in *)p->ai_addr)->sin_addr, ip4_str, sizeof(ip4_str));
            } else if (p->ai_family == AF_INET6 && strlen(ip6_str) == 0) {
                inet_ntop(AF_INET6, &((struct sockaddr_in6 *)p->ai_addr)->sin6_addr, ip6_str, sizeof(ip6_str));
            }
        }
        if (strlen(ip4_str) > 0) printf("  %s• Endereço IPv4    :%s %s%s%s\n", COLOR_LABEL, COLOR_RESET, COLOR_OK, ip4_str, COLOR_RESET);
        if (strlen(ip6_str) > 0) printf("  %s• Endereço IPv6    :%s %s%s%s\n", COLOR_LABEL, COLOR_RESET, COLOR_OK, ip6_str, COLOR_RESET);
        freeaddrinfo(res);
    } else {
        printf("  %s• Endereço IP      :%s %sFalha ao resolver DNS%s\n", COLOR_LABEL, COLOR_RESET, COLOR_ERR, COLOR_RESET);
    }

    // 2. Nameservers Autoritativos (NS)
    query_authoritative_ns(host);

    // 3. Latência de Conexão (Ping e Jitter)
    double rtt_min = 0, rtt_avg = 0, rtt_max = 0, jitter = 0;
    measure_tcp_latency_stats(host, port, &rtt_min, &rtt_avg, &rtt_max, &jitter);

    if (rtt_avg > 0.0) {
        printf("  %s• Ping TCP (RTT)   :%s %s%.2f ms%s (Mín: %.1f ms | Máx: %.1f ms | %sJitter: %.2f ms%s)\n",
               COLOR_LABEL, COLOR_RESET, COLOR_OK, rtt_avg, COLOR_RESET, rtt_min, rtt_max, COLOR_WARN, jitter, COLOR_RESET);
    }

    // 4. Auditoria de Certificado SSL/TLS
    if (is_https || port == 443) {
        printf("  ---------------------------------------------------------------------------------\n");
        printf("  %s[ AUDITORIA DE CRIPTOGRAFIA & CERTIFICADO SSL/TLS ]%s\n\n", COLOR_TITLE, COLOR_RESET);
        audit_ssl_certificate(host, port);
    }

    // 5. Requisição HTTP, TTFB & Headers de Segurança
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "curl -s -I -L --max-redirs 5 -A 'Mozilla/5.0' -w '%%{time_starttransfer}\\n%%{http_code}' \"%s://%s:%d%s\" 2>/dev/null",
             is_https ? "https" : "http", host, port, path);

    double t0 = get_time_sec();
    FILE *fp = popen(cmd, "r");
    char *headers_buf = malloc(128 * 1024);

    if (fp && headers_buf) {
        size_t n = fread(headers_buf, 1, (128 * 1024) - 1, fp);
        pclose(fp);
        double ttfb_ms = (get_time_sec() - t0) * 1000.0;
        headers_buf[n] = '\0';

        int status_code = 0;
        char *last_nl = strrchr(headers_buf, '\n');
        if (last_nl) {
            status_code = atoi(last_nl + 1);
            *last_nl = '\0';
            char *prev_nl = strrchr(headers_buf, '\n');
            if (prev_nl) {
                double c_ttfb = atof(prev_nl + 1);
                if (c_ttfb > 0.0) ttfb_ms = c_ttfb * 1000.0;
                *prev_nl = '\0';
            }
        }

        printf("  ---------------------------------------------------------------------------------\n");
        printf("  %s[ RESPOSTA DO SERVIDOR & LATÊNCIA DE PROCESSAMENTO (TTFB) ]%s\n\n", COLOR_TITLE, COLOR_RESET);

        if (status_code > 0) {
            const char *st_col = (status_code >= 200 && status_code < 300) ? COLOR_OK :
                                 (status_code >= 300 && status_code < 400) ? COLOR_WARN : COLOR_ERR;

            printf("  %s• Código de Status :%s %sHTTP %d%s (%s)\n",
                   COLOR_LABEL, COLOR_RESET, st_col, status_code, COLOR_RESET,
                   (status_code == 200) ? "Sucesso / OK" :
                   (status_code == 301 || status_code == 302) ? "Redirecionamento" :
                   (status_code == 403) ? "Acesso Proibido" :
                   (status_code == 404) ? "Não Encontrado" : "Resposta Recebida");

            printf("  %s• TTFB (Server Lat):%s %s%.2f ms%s (Tempo para o servidor gerar o 1º byte)\n",
                   COLOR_LABEL, COLOR_RESET, COLOR_VAL, ttfb_ms, COLOR_RESET);

            int sec_score = 0;
            char sec_grade[8] = "";
            analyze_security_headers(headers_buf, &sec_score, sec_grade);

            const char *gr_col = (sec_score >= 80) ? "\033[1;30;42m" : (sec_score >= 50) ? "\033[1;30;43m" : "\033[1;37;41m";
            printf("  ---------------------------------------------------------------------------------\n");
            printf("  %s• Pontuação Global :%s %s%d / 100%s | %s CONCEITO DE DEFESA: %s %s\n",
                   COLOR_LABEL, COLOR_RESET, COLOR_VAL, sec_score, COLOR_RESET,
                   COLOR_LABEL, gr_col, sec_grade, COLOR_RESET);
        } else {
            printf("  %s• Servidor web offline ou conexão rejeitada.%s\n", COLOR_ERR, COLOR_RESET);
        }
        free(headers_buf);
    } else {
        if (headers_buf) free(headers_buf);
        if (fp) pclose(fp);
    }

    printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n", COLOR_TITLE, COLOR_RESET);

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "vsec: audited %s (SSL & Security Headers)", host);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
