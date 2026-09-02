#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <arpa/inet.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_LABEL   "\033[1;36m"
#define COLOR_VAL     "\033[1;32m"
#define COLOR_WARN    "\033[1;33m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_GRAY    "\033[0;90m"
#define COLOR_WHITE   "\033[1;37m"

static void u32_to_ip(uint32_t ip, char *buf, size_t sz) {
    snprintf(buf, sz, "%u.%u.%u.%u",
             (ip >> 24) & 0xFF,
             (ip >> 16) & 0xFF,
             (ip >> 8) & 0xFF,
             ip & 0xFF);
}

static void u32_to_bin(uint32_t val, char *buf, size_t sz) {
    snprintf(buf, sz,
        "%c%c%c%c%c%c%c%c.%c%c%c%c%c%c%c%c.%c%c%c%c%c%c%c%c.%c%c%c%c%c%c%c%c",
        (val & (1U << 31)) ? '1' : '0', (val & (1U << 30)) ? '1' : '0', (val & (1U << 29)) ? '1' : '0', (val & (1U << 28)) ? '1' : '0',
        (val & (1U << 27)) ? '1' : '0', (val & (1U << 26)) ? '1' : '0', (val & (1U << 25)) ? '1' : '0', (val & (1U << 24)) ? '1' : '0',
        (val & (1U << 23)) ? '1' : '0', (val & (1U << 22)) ? '1' : '0', (val & (1U << 21)) ? '1' : '0', (val & (1U << 20)) ? '1' : '0',
        (val & (1U << 19)) ? '1' : '0', (val & (1U << 18)) ? '1' : '0', (val & (1U << 17)) ? '1' : '0', (val & (1U << 16)) ? '1' : '0',
        (val & (1U << 15)) ? '1' : '0', (val & (1U << 14)) ? '1' : '0', (val & (1U << 13)) ? '1' : '0', (val & (1U << 12)) ? '1' : '0',
        (val & (1U << 11)) ? '1' : '0', (val & (1U << 10)) ? '1' : '0', (val & (1U << 9)) ? '1' : '0', (val & (1U << 8)) ? '1' : '0',
        (val & (1U << 7)) ? '1' : '0', (val & (1U << 6)) ? '1' : '0', (val & (1U << 5)) ? '1' : '0', (val & (1U << 4)) ? '1' : '0',
        (val & (1U << 3)) ? '1' : '0', (val & (1U << 2)) ? '1' : '0', (val & (1U << 1)) ? '1' : '0', (val & 1U) ? '1' : '0'
    );
}

static const char *get_ip_class(uint32_t ip) {
    uint8_t first = (ip >> 24) & 0xFF;
    if (first <= 127) return "Class A";
    if (first <= 191) return "Class B";
    if (first <= 223) return "Class C";
    if (first <= 239) return "Class D (Multicast)";
    return "Class E (Experimental)";
}

static const char *get_ip_scope(uint32_t ip) {
    uint8_t o1 = (ip >> 24) & 0xFF;
    uint8_t o2 = (ip >> 16) & 0xFF;

    if (o1 == 10) return "Privado (RFC 1918 / Rede Local)";
    if (o1 == 172 && (o2 >= 16 && o2 <= 31)) return "Privado (RFC 1918 / Rede Local)";
    if (o1 == 192 && o2 == 168) return "Privado (RFC 1918 / Rede Local)";
    if (o1 == 127) return "Loopback (Host Local)";
    if (o1 == 169 && o2 == 254) return "Link-Local / APIPA (Auto-IP)";
    if (o1 >= 224 && o1 <= 239) return "Multicast";
    if (o1 == 0) return "Endereço Default";
    return "Público (Internet Roteável)";
}

static void calculate_and_print(uint32_t ip_u32, int prefix) {
    if (prefix < 0) prefix = 0;
    if (prefix > 32) prefix = 32;

    uint32_t mask_u32 = (prefix == 0) ? 0 : (0xFFFFFFFFU << (32 - prefix));
    uint32_t wildcard_u32 = ~mask_u32;
    uint32_t net_u32 = ip_u32 & mask_u32;
    uint32_t bcast_u32 = ip_u32 | wildcard_u32;

    uint32_t first_host = 0, last_host = 0;
    unsigned long long usable_hosts = 0;
    unsigned long long total_hosts = 1ULL << (32 - prefix);

    if (prefix == 32) {
        first_host = last_host = ip_u32;
        usable_hosts = 1;
    } else if (prefix == 31) {
        first_host = net_u32;
        last_host = bcast_u32;
        usable_hosts = 2;
    } else {
        first_host = net_u32 + 1;
        last_host = bcast_u32 - 1;
        usable_hosts = total_hosts - 2;
    }

    char str_ip[16], str_net[16], str_bcast[16];
    char str_first[16], str_last[16], str_mask[16], str_wildcard[16];
    char str_bin_mask[40], str_bin_ip[40];

    u32_to_ip(ip_u32, str_ip, sizeof(str_ip));
    u32_to_ip(net_u32, str_net, sizeof(str_net));
    u32_to_ip(bcast_u32, str_bcast, sizeof(str_bcast));
    u32_to_ip(first_host, str_first, sizeof(str_first));
    u32_to_ip(last_host, str_last, sizeof(str_last));
    u32_to_ip(mask_u32, str_mask, sizeof(str_mask));
    u32_to_ip(wildcard_u32, str_wildcard, sizeof(str_wildcard));
    u32_to_bin(mask_u32, str_bin_mask, sizeof(str_bin_mask));
    u32_to_bin(ip_u32, str_bin_ip, sizeof(str_bin_ip));

    printf("\n%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ snc - Subnet Calculator / CIDR Analyzer ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);

    printf("  %s• IP Fornecido       :%s %s%s /%d%s\n", COLOR_LABEL, COLOR_RESET, COLOR_VAL, str_ip, prefix, COLOR_RESET);
    printf("  %s• IP em Binário      :%s %s%s%s\n", COLOR_LABEL, COLOR_RESET, COLOR_GRAY, str_bin_ip, COLOR_RESET);
    printf("  %s• IP da Rede (Network):%s %s%s%s\n", COLOR_LABEL, COLOR_RESET, COLOR_WARN, str_net, COLOR_RESET);
    printf("  %s• IP de Broadcast    :%s %s%s%s\n", COLOR_LABEL, COLOR_RESET, COLOR_WARN, str_bcast, COLOR_RESET);
    printf("  --------------------------------------------------------\n");

    if (prefix >= 31) {
        printf("  %s• Faixa de Hosts     :%s %s%s - %s%s\n", COLOR_LABEL, COLOR_RESET, COLOR_VAL, str_first, str_last, COLOR_RESET);
    } else {
        printf("  %s• Primeiro Host Útil :%s %s%s%s\n", COLOR_LABEL, COLOR_RESET, COLOR_VAL, str_first, COLOR_RESET);
        printf("  %s• Último Host Útil   :%s %s%s%s\n", COLOR_LABEL, COLOR_RESET, COLOR_VAL, str_last, COLOR_RESET);
    }

    printf("  %s• Hosts Utilizáveis  :%s %s%llu%s\n", COLOR_LABEL, COLOR_RESET, COLOR_VAL, usable_hosts, COLOR_RESET);
    printf("  %s• Total de Endereços :%s %llu\n", COLOR_LABEL, COLOR_RESET, total_hosts);
    printf("  --------------------------------------------------------\n");

    printf("  %s• Máscara Decimal    :%s %s%s%s\n", COLOR_LABEL, COLOR_RESET, COLOR_WHITE, str_mask, COLOR_RESET);
    printf("  %s• Máscara Hex        :%s 0x%08X\n", COLOR_LABEL, COLOR_RESET, mask_u32);
    printf("  %s• Máscara Binária    :%s %s%s%s\n", COLOR_LABEL, COLOR_RESET, COLOR_GRAY, str_bin_mask, COLOR_RESET);
    printf("  %s• Máscara Wildcard   :%s %s\n", COLOR_LABEL, COLOR_RESET, str_wildcard);
    printf("  --------------------------------------------------------\n");

    printf("  %s• Classe IPv4        :%s %s\n", COLOR_LABEL, COLOR_RESET, get_ip_class(ip_u32));
    printf("  %s• Escopo do Endereço :%s %s%s%s\n", COLOR_LABEL, COLOR_RESET, COLOR_WARN, get_ip_scope(ip_u32), COLOR_RESET);
    printf("%s========================================================%s\n\n", COLOR_TITLE, COLOR_RESET);

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "snc: analyzed %s/%d (net: %s, usable: %llu)", str_ip, prefix, str_net, usable_hosts);
    utilipc_write_status(-1, -1, -1, log_msg);
}

static int parse_mask_to_prefix(const char *mask_str) {
    if (mask_str[0] == '/' || isdigit((unsigned char)mask_str[0])) {
        const char *p = (mask_str[0] == '/') ? mask_str + 1 : mask_str;
        if (!strchr(p, '.')) {
            int prefix = atoi(p);
            return (prefix >= 0 && prefix <= 32) ? prefix : -1;
        }
    }
    struct in_addr addr;
    if (inet_pton(AF_INET, mask_str, &addr) == 1) {
        uint32_t m = ntohl(addr.s_addr);
        int prefix = 0;
        while (m & (1U << 31)) {
            prefix++;
            m <<= 1;
        }
        if (m == 0) return prefix;
    }
    return -1;
}

int main(int argc, char *argv[]) {
    utilipc_init();

    char ip_str[64] = "";
    int prefix = 24;

    if (argc >= 2) {
        char *slash = strchr(argv[1], '/');
        if (slash) {
            size_t ip_len = slash - argv[1];
            if (ip_len < sizeof(ip_str)) {
                strncpy(ip_str, argv[1], ip_len);
                ip_str[ip_len] = '\0';
                prefix = parse_mask_to_prefix(slash + 1);
            }
        } else {
            strncpy(ip_str, argv[1], sizeof(ip_str) - 1);
            if (argc >= 3) {
                prefix = parse_mask_to_prefix(argv[2]);
            }
        }
    } else {
        printf("\n\033[1;35m==========================================\n");
        printf("[ snc - Subnet Calculator Interativo ]\n");
        printf("==========================================\033[0m\n");
        printf("  \033[1;36m• Digite o IP ou CIDR\033[0m (ex: 192.168.1.100/24 ou 10.0.0.1): ");
        fflush(stdout);

        char input[128] = "";
        if (!fgets(input, sizeof(input), stdin)) return 0;
        input[strcspn(input, "\r\n")] = '\0';

        char *slash = strchr(input, '/');
        if (slash) {
            size_t ip_len = slash - input;
            if (ip_len < sizeof(ip_str)) {
                strncpy(ip_str, input, ip_len);
                ip_str[ip_len] = '\0';
                prefix = parse_mask_to_prefix(slash + 1);
            }
        } else {
            char mask_input[64] = "";
            char *space = strchr(input, ' ');
            if (space) {
                *space = '\0';
                strncpy(ip_str, input, sizeof(ip_str) - 1);
                prefix = parse_mask_to_prefix(space + 1);
            } else {
                strncpy(ip_str, input, sizeof(ip_str) - 1);
                printf("  \033[1;36m• Digite a Máscara ou CIDR\033[0m (ex: 24 ou 255.255.255.0) [Padrão: 24]: ");
                fflush(stdout);
                if (fgets(mask_input, sizeof(mask_input), stdin)) {
                    mask_input[strcspn(mask_input, "\r\n")] = '\0';
                    if (strlen(mask_input) > 0) {
                        prefix = parse_mask_to_prefix(mask_input);
                    }
                }
            }
        }
    }

    struct in_addr addr;
    if (inet_pton(AF_INET, ip_str, &addr) != 1 || prefix < 0) {
        printf("\n\033[1;31m[Erro: Endereço IP ou Máscara inválida!]\033[0m\n");
        printf("Exemplos válidos:\n");
        printf("  snc 192.168.1.0/24\n");
        printf("  snc 10.50.0.1 255.255.240.0\n");
        printf("  snc 172.16.0.0 16\n\n");
        utilipc_close();
        return 1;
    }

    calculate_and_print(ntohl(addr.s_addr), prefix);
    utilipc_close();
    return 0;
}
