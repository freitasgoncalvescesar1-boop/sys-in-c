#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>
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

// Época NTP (1900-01-01) vs Época Unix (1970-01-01): 70 anos em segundos
#define NTP_TIMESTAMP_DELTA 2208988800ULL

#pragma pack(push, 1)
typedef struct {
    uint8_t  li_vn_mode;      // Leap Indicator (2b), Version (3b), Mode (3b)
    uint8_t  stratum;         // Nível de estrato (1 = Relógio Atômico Primário)
    uint8_t  poll;            // Intervalo de poll
    uint8_t  precision;       // Precisão do relógio do servidor
    uint32_t root_delay;      // Atraso de raiz
    uint32_t root_dispersion; // Dispersão de raiz
    uint32_t ref_id;          // Reference Identifier (ex: GPS, ATOM)
    uint32_t ref_tm_s;        // Reference Timestamp Seconds
    uint32_t ref_tm_f;        // Reference Timestamp Fraction
    uint32_t orig_tm_s;       // Origin Timestamp (t0)
    uint32_t orig_tm_f;
    uint32_t rx_tm_s;         // Receive Timestamp (t1)
    uint32_t rx_tm_f;
    uint32_t tx_tm_s;         // Transmit Timestamp (t2)
    uint32_t tx_tm_f;
} NTPPacket;
#pragma pack(pop)

static double get_realtime_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static double ntp_to_unix_time(uint32_t ntp_sec, uint32_t ntp_frac) {
    uint32_t sec = ntohl(ntp_sec);
    uint32_t frac = ntohl(ntp_frac);
    if (sec == 0) return 0.0;
    double sec_d = (double)(sec - NTP_TIMESTAMP_DELTA);
    double frac_d = (double)frac / 4294967296.0;
    return sec_d + frac_d;
}

static void print_help(void) {
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ sntp - RFC 5905 Atomic Time Synchronizer & Clock Drift ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  sntp [SERVER_HOST]         (Consulta hora atomica e calcula desvio)\n");
    printf("  sntp --help                (Exibe esta ajuda)\n\n");
    printf("Servidores Populares:\n");
    printf("  • a.st1.ntp.br             (Observatorio Nacional / NTP.br - Padrao BR)\n");
    printf("  • pool.ntp.org             (Pool Global de Relogios Atomicos)\n");
    printf("  • time.google.com          (Servidores de Tempo do Google)\n");
    printf("  • time.cloudflare.com      (Servidores de Tempo da Cloudflare)\n");
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

int main(int argc, char *argv[]) {
    utilipc_init();

    const char *server_host = "a.st1.ntp.br";

    if (argc >= 2) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print_help();
            utilipc_close();
            return 0;
        }
        server_host = argv[1];
    }

    printf("\n%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ sntp - Sincronizador de Relógio Atômico via UDP / RFC 5905 ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n\n", COLOR_TITLE, COLOR_RESET);
    printf("  • Conectando ao servidor NTP: %s%s:123%s (UDP)...\n", COLOR_LABEL, server_host, COLOR_RESET);

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(server_host, "123", &hints, &res) != 0) {
        fprintf(stderr, "  %s[ERRO]%s Nao foi possivel resolver o servidor NTP '%s'\n\n", COLOR_ERR, COLOR_RESET, server_host);
        utilipc_close();
        return 1;
    }

    char server_ip[INET_ADDRSTRLEN];
    struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
    inet_ntop(AF_INET, &sa->sin_addr, server_ip, sizeof(server_ip));

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        perror("socket");
        freeaddrinfo(res);
        utilipc_close();
        return 1;
    }

    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Montagem do Pacote NTP v4 (Client Mode 3)
    NTPPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.li_vn_mode = 0x23; // LI=0 (sem aviso), VN=4 (NTPv4), Mode=3 (Client)

    double t0 = get_realtime_sec(); // Timestamp de Origem (Local)

    if (sendto(fd, &pkt, sizeof(pkt), 0, res->ai_addr, res->ai_addrlen) < 0) {
        fprintf(stderr, "  %s[ERRO]%s Falha ao enviar pacote UDP: %s\n\n", COLOR_ERR, COLOR_RESET, strerror(errno));
        close(fd);
        freeaddrinfo(res);
        utilipc_close();
        return 1;
    }

    NTPPacket resp;
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    ssize_t n = recvfrom(fd, &resp, sizeof(resp), 0, (struct sockaddr *)&from_addr, &from_len);
    double t3 = get_realtime_sec(); // Timestamp de Chegada (Local)

    close(fd);
    freeaddrinfo(res);

    if (n < (ssize_t)sizeof(NTPPacket)) {
        fprintf(stderr, "  %s[ERRO]%s Timeout na resposta do servidor NTP %s (Sem resposta em 3s)\n\n", COLOR_ERR, COLOR_RESET, server_ip);
        utilipc_close();
        return 1;
    }

    double t1 = ntp_to_unix_time(resp.rx_tm_s, resp.rx_tm_f); // Recebido pelo servidor
    double t2 = ntp_to_unix_time(resp.tx_tm_s, resp.tx_tm_f); // Transmitido pelo servidor

    // Cálculo Oficial RFC 5905
    double round_trip_delay = (t3 - t0) - (t2 - t1);
    double clock_offset = ((t1 - t0) + (t2 - t3)) / 2.0;

    time_t atomic_sec = (time_t)t2;
    struct tm *tm_atomic = localtime(&atomic_sec);
    char atomic_time_str[64];
    strftime(atomic_time_str, sizeof(atomic_time_str), "%Y-%m-%d %H:%M:%S", tm_atomic);

    time_t local_sec = (time_t)t3;
    struct tm *tm_local = localtime(&local_sec);
    char local_time_str[64];
    strftime(local_time_str, sizeof(local_time_str), "%Y-%m-%d %H:%M:%S", tm_local);

    const char *stratum_desc = "Desconhecido";
    if (resp.stratum == 1) stratum_desc = "Estrato 1 (Relógio Atômico Primário / GPS)";
    else if (resp.stratum == 2) stratum_desc = "Estrato 2 (Servidor Sincronizado com Estrato 1)";
    else if (resp.stratum > 2) stratum_desc = "Estrato Secundário de Rede";

    printf("  %s• IP do Servidor NTP :%s %s (%s)\n", COLOR_LABEL, COLOR_RESET, server_ip, server_host);
    printf("  %s• Nível de Estrato   :%s %u - %s%s%s\n", COLOR_LABEL, COLOR_RESET, resp.stratum, COLOR_OK, stratum_desc, COLOR_RESET);
    printf("  ---------------------------------------------------------------------------------\n");
    printf("  %s• Hora Atômica Real  :%s %s%s%s (UTC Local)\n", COLOR_LABEL, COLOR_RESET, COLOR_OK, atomic_time_str, COLOR_RESET);
    printf("  %s• Hora do seu Aparelho:%s %s\n", COLOR_LABEL, COLOR_RESET, local_time_str);
    printf("  ---------------------------------------------------------------------------------\n");

    const char *drift_col = (fabs(clock_offset) < 0.05) ? COLOR_OK : COLOR_WARN;
    printf("  %s• Desvio do seu Relógio:%s %s%+.3f ms%s (%+.6f segundos)\n",
           COLOR_LABEL, COLOR_RESET, drift_col, clock_offset * 1000.0, COLOR_RESET, clock_offset);
    printf("  %s• Latência de Rede   :%s %s%.2f ms%s (Ida e Volta)\n",
           COLOR_LABEL, COLOR_RESET, COLOR_VAL, round_trip_delay * 1000.0, COLOR_RESET);
    printf("%s=================================================================================%s\n\n", COLOR_TITLE, COLOR_RESET);

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "sntp: offset %+.2fms via %s", clock_offset * 1000.0, server_host);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
