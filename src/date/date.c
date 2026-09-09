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
#include <ctype.h>
#include <errno.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_WARN    "\033[1;33m"
#define COLOR_TAG     "\033[1;33m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_LABEL   "\033[1;36m"
#define COLOR_VAL     "\033[1;37m"
#define COLOR_MUTED   "\033[0;90m"
#define COLOR_PROG    "\033[1;32m"

#define NTP_TIMESTAMP_DELTA 2208988800ULL
#define NTP_SERVER "a.st1.ntp.br"

#pragma pack(push, 1)
typedef struct {
    uint8_t  li_vn_mode;
    uint8_t  stratum;
    uint8_t  poll;
    uint8_t  precision;
    uint32_t root_delay;
    uint32_t root_dispersion;
    uint32_t ref_id;
    uint32_t ref_tm_s;
    uint32_t ref_tm_f;
    uint32_t orig_tm_s;
    uint32_t orig_tm_f;
    uint32_t rx_tm_s;
    uint32_t rx_tm_f;
    uint32_t tx_tm_s;
    uint32_t tx_tm_f;
} NTPPacket;
#pragma pack(pop)

typedef struct {
    char name[128];
    int day;
    int month;
    int year;
} ImportantDate;

static const char *get_events_file_path(void) {
    static char path[512];
    const char *home = getenv("HOME");
    if (!home || strlen(home) == 0) home = ".";
    snprintf(path, sizeof(path), "%s/.date_events.txt", home);
    return path;
}

static void print_help(void) {
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ date (src) - Atomic Internet Clock, Year Progress & Important Reminders ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  date                                         (Exibe painel atomico completo do ano)\n");
    printf("  date -c important \"<NOME>\" <DD/MM/AAAA>      (Salvar data importante / aniversario)\n");
    printf("  date -l, --list                              (Listar datas salvas com contagem regressiva)\n");
    printf("  date --clear                                 (Limpar todas as datas salvas)\n");
    printf("  date --help                                  (Exibir este guia formatado)\n\n");
    printf("Exemplos:\n");
    printf("  • %sdate%s\n", COLOR_TAG, COLOR_RESET);
    printf("  • %sdate -c important \"Aniversario da Samara\" 15/04/2005%s\n", COLOR_TAG, COLOR_RESET);
    printf("  • %sdate -c important \"Reveillon\" 31/12/2026%s\n", COLOR_TAG, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

static int is_leap_year(int y) {
    return ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0));
}

// Sincronização SNTP com o Observatório Nacional / NTP.br
static int fetch_atomic_time(time_t *out_atomic_time, double *out_offset_ms) {
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return -1;

    struct timeval tv = { .tv_sec = 1, .tv_usec = 500000 }; // Timeout rápido de 1.5s
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(NTP_SERVER, "123", &hints, &res) != 0) {
        close(fd);
        return -1;
    }

    NTPPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.li_vn_mode = 0x23; // NTPv4, Client

    struct timespec ts_t0;
    clock_gettime(CLOCK_REALTIME, &ts_t0);
    double t0 = (double)ts_t0.tv_sec + ((double)ts_t0.tv_nsec / 1e9);

    if (sendto(fd, &pkt, sizeof(pkt), 0, res->ai_addr, res->ai_addrlen) < 0) {
        close(fd);
        freeaddrinfo(res);
        return -1;
    }

    NTPPacket resp;
    struct sockaddr_in from;
    socklen_t flen = sizeof(from);
    ssize_t n = recvfrom(fd, &resp, sizeof(resp), 0, (struct sockaddr *)&from, &flen);

    struct timespec ts_t3;
    clock_gettime(CLOCK_REALTIME, &ts_t3);
    double t3 = (double)ts_t3.tv_sec + ((double)ts_t3.tv_nsec / 1e9);

    close(fd);
    freeaddrinfo(res);

    if (n < (ssize_t)sizeof(NTPPacket)) return -1;

    uint32_t tx_sec = ntohl(resp.tx_tm_s);
    uint32_t tx_frac = ntohl(resp.tx_tm_f);
    if (tx_sec == 0) return -1;

    double t2 = (double)(tx_sec - NTP_TIMESTAMP_DELTA) + ((double)tx_frac / 4294967296.0);
    uint32_t rx_sec = ntohl(resp.rx_tm_s);
    uint32_t rx_frac = ntohl(resp.rx_tm_f);
    double t1 = (double)(rx_sec - NTP_TIMESTAMP_DELTA) + ((double)rx_frac / 4294967296.0);

    double offset = ((t1 - t0) + (t2 - t3)) / 2.0;
    *out_offset_ms = offset * 1000.0;
    *out_atomic_time = (time_t)t2;
    return 0;
}

static void save_important_date(const char *name, const char *date_str) {
    int d = 0, m = 0, y = 0;
    if (sscanf(date_str, "%d/%d/%d", &d, &m, &y) != 3 &&
        sscanf(date_str, "%d-%d-%d", &d, &m, &y) != 3) {
        fprintf(stderr, "\n  %s[ERRO]%s Formato de data invalido! Use DD/MM/AAAA (ex: 15/04/2005)\n\n", COLOR_ERR, COLOR_RESET);
        return;
    }

    FILE *fp = fopen(get_events_file_path(), "a");
    if (!fp) {
        fprintf(stderr, "date: erro ao salvar em '%s': %s\n", get_events_file_path(), strerror(errno));
        return;
    }

    fprintf(fp, "%02d/%02d/%04d|%s\n", d, m, y, name);
    fclose(fp);

    printf("\n  %s✔ Data importante registrada com sucesso!%s\n", COLOR_OK, COLOR_RESET);
    printf("  • Evento : %s%s%s\n", COLOR_VAL, name, COLOR_RESET);
    printf("  • Data   : %s%02d/%02d/%04d%s\n\n", COLOR_TAG, d, m, y, COLOR_RESET);
}

static void list_important_dates(time_t current_time) {
    FILE *fp = fopen(get_events_file_path(), "r");
    if (!fp) return;

    struct tm *now_tm = localtime(&current_time);
    int cur_year = now_tm->tm_year + 1900;
    int cur_yday = now_tm->tm_yday;

    printf("  ---------------------------------------------------------------------------------\n");
    printf("  %s[ 📌 DATAS IMPORTANTES & LEMBRETES CADASTRADOS ]%s\n\n", COLOR_TITLE, COLOR_RESET);
    printf("  %-32.32s  %-12s  %s\n", "EVENTO / LEMBRETE", "DATA", "STATUS / CONTAGEM REGRESSIVA");
    printf("  ---------------------------------------------------------------------------------\n");

    char line[256];
    int count = 0;

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *sep = strchr(line, '|');
        if (!sep) continue;

        *sep = '\0';
        char *date_str = line;
        char *name = sep + 1;

        int d = 0, m = 0, y = 0;
        if (sscanf(date_str, "%d/%d/%d", &d, &m, &y) != 3) continue;

        // Calcula próximo aniversário / ocorrência
        struct tm target_tm;
        memset(&target_tm, 0, sizeof(target_tm));
        target_tm.tm_mday = d;
        target_tm.tm_mon  = m - 1;
        target_tm.tm_year = cur_year - 1900;
        mktime(&target_tm);

        int days_diff = target_tm.tm_yday - cur_yday;
        if (days_diff < 0) {
            // Já passou este ano, calcula para o próximo ano
            target_tm.tm_year = cur_year + 1 - 1900;
            mktime(&target_tm);
            int days_in_cur_year = is_leap_year(cur_year) ? 366 : 365;
            days_diff += days_in_cur_year;
        }

        printf("  %s%-32.32s%s  %s%02d/%02d/%04d%s  ",
               COLOR_VAL, name, COLOR_RESET, COLOR_TAG, d, m, y, COLOR_RESET);

        if (days_diff == 0) {
            printf("\033[1;30;42m 🎉 É HOJE! PARABÉNS! 🎂 \033[0m\n");
        } else if (days_diff == 1) {
            printf("%s[ É AMANHÃ! ]%s\n", COLOR_WARN, COLOR_RESET);
        } else {
            printf("%sFaltam %d dias%s\n", (days_diff <= 30) ? COLOR_WARN : COLOR_MUTED, days_diff, COLOR_RESET);
        }
        count++;
    }
    fclose(fp);

    if (count == 0) {
        printf("  %sNenhuma data cadastrada ainda.%s\n", COLOR_MUTED, COLOR_RESET);
    }
    printf("\n");
}

static void render_year_progress_bar(double pct, int bar_width) {
    int filled = (int)((pct / 100.0) * bar_width);
    if (filled < 0) filled = 0;
    if (filled > bar_width) filled = bar_width;

    printf("  %s[", COLOR_TITLE);
    for (int i = 0; i < bar_width; i++) {
        if (i < filled) printf("%s█", COLOR_PROG);
        else printf("%s░", COLOR_MUTED);
    }
    printf("%s] %s%10.6f%%%s\n", COLOR_TITLE, COLOR_OK, pct, COLOR_RESET);
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc >= 2) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print_help();
            utilipc_close();
            return 0;
        }
        if (strcmp(argv[1], "--clear") == 0) {
            unlink(get_events_file_path());
            printf("\n  %s✔ Lista de datas importantes limpa com sucesso!%s\n\n", COLOR_OK, COLOR_RESET);
            utilipc_close();
            return 0;
        }
        if (strcmp(argv[1], "-l") == 0 || strcmp(argv[1], "--list") == 0) {
            time_t now = time(NULL);
            list_important_dates(now);
            utilipc_close();
            return 0;
        }
        // date -c important "Nome" DD/MM/AAAA
        if (strcmp(argv[1], "-c") == 0 && argc >= 5 && strcmp(argv[2], "important") == 0) {
            save_important_date(argv[3], argv[4]);
            utilipc_close();
            return 0;
        }
    }

    // Modo Painel Principal
    time_t current_time = time(NULL);
    double ntp_offset = 0.0;
    int is_atomic = (fetch_atomic_time(&current_time, &ntp_offset) == 0);

    struct tm *tm_info = localtime(&current_time);
    int year = tm_info->tm_year + 1900;
    int total_days_in_year = is_leap_year(year) ? 366 : 365;
    int days_passed = tm_info->tm_yday + 1;
    int days_remaining = total_days_in_year - days_passed;

    // Cálculo exato de segundos no ano para porcentagem de precisão atômica
    struct tm year_start_tm;
    memset(&year_start_tm, 0, sizeof(year_start_tm));
    year_start_tm.tm_year = tm_info->tm_year;
    year_start_tm.tm_mday = 1;
    time_t year_start = timegm(&year_start_tm);

    struct tm year_end_tm;
    memset(&year_end_tm, 0, sizeof(year_end_tm));
    year_end_tm.tm_year = tm_info->tm_year + 1;
    year_end_tm.tm_mday = 1;
    time_t year_end = timegm(&year_end_tm);

    double total_year_seconds = (double)(year_end - year_start);
    double elapsed_year_seconds = (double)(current_time - year_start);
    double year_percentage = (elapsed_year_seconds / total_year_seconds) * 100.0;

    char date_str[64], time_str[64];
    strftime(date_str, sizeof(date_str), "%A, %d de %B de %Y", tm_info);
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

    printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s│%s  %s[ ⏰ DATE - Relógio Atômico & Painel de Conclusão do Ano ]%s                 %s│%s\n",
           COLOR_TITLE, COLOR_RESET, COLOR_OK, COLOR_RESET, COLOR_TITLE, COLOR_RESET);
    printf("%s├────────────────────────────────────────────────────────────────────────────┤%s\n", COLOR_TITLE, COLOR_RESET);
    printf("  %s• Data Atual       :%s %s%s%s\n", COLOR_LABEL, COLOR_RESET, COLOR_VAL, date_str, COLOR_RESET);
    printf("  %s• Horário Local    :%s %s%s%s\n", COLOR_LABEL, COLOR_RESET, COLOR_TAG, time_str, COLOR_RESET);
    printf("  %s• Fonte Temporal   :%s %s\n", COLOR_LABEL, COLOR_RESET,
           is_atomic ? "\033[1;32mSincronizado via Relógio Atômico (NTP.br / a.st1.ntp.br)\033[0m" : "\033[1;33mRelógio Local do Dispositivo (Offline)\033[0m");

    if (is_atomic) {
        printf("  %s• Desvio / Offset  :%s %s%+.2f ms%s (Precisão absoluta)\n",
               COLOR_LABEL, COLOR_RESET, (fabs(ntp_offset) < 20.0) ? COLOR_OK : COLOR_WARN, ntp_offset, COLOR_RESET);
    }

    printf("  ----------------------------------------------------------------------------\n");
    printf("  %s[ PROGRESSO & DIAS DO ANO (%d) ]%s\n\n", COLOR_TITLE, year, COLOR_RESET);
    printf("  %s• Dias Decorridos  :%s %s%d dias%s (desde 01/01/%d)\n", COLOR_LABEL, COLOR_RESET, COLOR_OK, days_passed, COLOR_RESET, year);
    printf("  %s• Dias Restantes   :%s %s%d dias%s (até 31/12/%d - Réveillon)\n", COLOR_LABEL, COLOR_RESET, COLOR_WARN, days_remaining, COLOR_RESET, year);
    printf("  %s• Progresso do Ano :%s\n", COLOR_LABEL, COLOR_RESET);
    render_year_progress_bar(year_percentage, 40);

    // Lista aniversários / eventos cadastrados
    list_important_dates(current_time);

    printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n", COLOR_TITLE, COLOR_RESET);

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "date: Year %d at %.4f%% (days left: %d)", year, year_percentage, days_remaining);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
