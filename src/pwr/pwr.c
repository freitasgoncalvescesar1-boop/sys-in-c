#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
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

static volatile int keep_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    keep_running = 0;
    printf("\033[?25h\033[0m\n");
    fflush(stdout);
}

static void print_help(void) {
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ pwr - Kernel Hardware Power, Battery & Thermal Monitor ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  pwr                        (Exibe snapshot de energia e temperaturas)\n");
    printf("  pwr -w, --watch            (Monitoramento continuo em tempo real)\n");
    printf("  pwr --help                 (Exibe esta ajuda)\n\n");
    printf("Sensores Monitorados:\n");
    printf("  • Bateria: Carga %%, Voltagem (V), Corrente (mA), Consumo (Watts)\n");
    printf("  • Saude & Tecnologia: Li-ion/Li-poly, Status de carga\n");
    printf("  • Zonas Termicas: Temperatura da bateria e nucleos da CPU (°C)\n");
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

static int read_sys_str(const char *path, char *out, size_t max_len) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    if (!fgets(out, max_len, fp)) { fclose(fp); return -1; }
    fclose(fp);
    size_t l = strlen(out);
    while (l > 0 && (out[l-1] == '\n' || out[l-1] == '\r' || out[l-1] == ' ')) out[--l] = '\0';
    return 0;
}

static int read_sys_long(const char *path, long *val) {
    char buf[64];
    if (read_sys_str(path, buf, sizeof(buf)) < 0) return -1;
    *val = strtol(buf, NULL, 10);
    return 0;
}

static void render_pwr_dashboard(int live_mode) {
    char bat_dir[256] = "";
    DIR *dir = opendir("/sys/class/power_supply");
    if (dir) {
        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {
            if (de->d_name[0] == '.') continue;
            if (strcasestr(de->d_name, "bat") || strcasecmp(de->d_name, "battery") == 0) {
                snprintf(bat_dir, sizeof(bat_dir), "/sys/class/power_supply/%s", de->d_name);
                break;
            }
        }
        closedir(dir);
    }

    long capacity = -1, voltage_uV = 0, current_uA = 0, temp_raw = 0;
    char status[64] = "Desconhecido", health[64] = "Normal", tech[64] = "Li-ion";

    if (strlen(bat_dir) > 0) {
        char path[512];
        snprintf(path, sizeof(path), "%s/capacity", bat_dir); read_sys_long(path, &capacity);
        snprintf(path, sizeof(path), "%s/voltage_now", bat_dir);
        if (read_sys_long(path, &voltage_uV) < 0) {
            snprintf(path, sizeof(path), "%s/voltage_avg", bat_dir); read_sys_long(path, &voltage_uV);
        }
        snprintf(path, sizeof(path), "%s/current_now", bat_dir);
        if (read_sys_long(path, &current_uA) < 0) {
            snprintf(path, sizeof(path), "%s/current_avg", bat_dir); read_sys_long(path, &current_uA);
        }
        snprintf(path, sizeof(path), "%s/temp", bat_dir); read_sys_long(path, &temp_raw);
        snprintf(path, sizeof(path), "%s/status", bat_dir); read_sys_str(path, status, sizeof(status));
        snprintf(path, sizeof(path), "%s/health", bat_dir); read_sys_str(path, health, sizeof(health));
        snprintf(path, sizeof(path), "%s/technology", bat_dir); read_sys_str(path, tech, sizeof(tech));
    }

    double voltage_v = (double)voltage_uV / 1000000.0;
    double current_ma = (double)current_uA / 1000.0;
    double power_watts = (voltage_v * current_ma) / 1000.0;
    if (power_watts < 0) power_watts = -power_watts;

    double temp_c = (temp_raw > 1000) ? ((double)temp_raw / 1000.0) : ((double)temp_raw / 10.0);

    if (live_mode) printf("\033[H");

    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ pwr - Telemetria de Bateria, Energia & Sensores Térmicos ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n\n", COLOR_TITLE, COLOR_RESET);

    // 1. Bloco de Bateria & Carga
    const char *bat_col = (capacity > 50) ? COLOR_OK : (capacity > 20) ? COLOR_WARN : COLOR_ERR;
    printf("  %s[1] STATUS DA BATERIA & FLUXO ELÉTRICO%s\n", COLOR_LABEL, COLOR_RESET);
    printf("  ---------------------------------------------------------------------------------\n");
    if (capacity >= 0) {
        printf("  • %sNível de Carga    :%s %s%ld%%%s  [", COLOR_LABEL, COLOR_RESET, bat_col, capacity, COLOR_RESET);
        int filled = capacity / 10;
        for (int i = 0; i < 10; i++) {
            if (i < filled) printf("%s■%s", bat_col, COLOR_RESET);
            else printf("%s░%s", COLOR_MUTED, COLOR_RESET);
        }
        printf("] (%s)\n", status);
    } else {
        printf("  • %sNível de Carga    :%s N/A (Alimentação AC ou Sem Bateria)\n", COLOR_LABEL, COLOR_RESET);
    }

    printf("  • %sVoltagem Atual   :%s %s%.3f V%s\n", COLOR_LABEL, COLOR_RESET, COLOR_VAL, voltage_v, COLOR_RESET);

    if (current_ma != 0.0) {
        const char *cur_col = (current_ma > 0) ? COLOR_OK : COLOR_WARN;
        printf("  • %sCorrente / Fluxo :%s %s%+.1f mA%s (%s)\n",
               COLOR_LABEL, COLOR_RESET, cur_col, current_ma, COLOR_RESET, (current_ma > 0) ? "Carregando" : "Consumo Ativo");
        printf("  • %sPotência Instant. :%s %s%.2f Watts%s\n", COLOR_LABEL, COLOR_RESET, COLOR_VAL, power_watts, COLOR_RESET);
    }

    printf("  • %sTemperatura Bat. :%s %s%.1f °C%s (Saúde: %s%s%s | Tipo: %s)\n\n",
           COLOR_LABEL, COLOR_RESET, (temp_c > 42.0) ? COLOR_ERR : COLOR_OK, temp_c, COLOR_RESET,
           COLOR_OK, health, COLOR_RESET, tech);

    // 2. Sensores Térmicos da CPU/SoC
    printf("  %s[2] ZONAS TÉRMICAS DA CPU & SENSORES (/sys/class/thermal)%s\n", COLOR_LABEL, COLOR_RESET);
    printf("  ---------------------------------------------------------------------------------\n");

    DIR *tdir = opendir("/sys/class/thermal");
    int sensors_count = 0;
    if (tdir) {
        struct dirent *de;
        while ((de = readdir(tdir)) != NULL && sensors_count < 8) {
            if (strncmp(de->d_name, "thermal_zone", 12) == 0) {
                char path[512], type_str[64] = "zone";
                long t_val = 0;

                snprintf(path, sizeof(path), "/sys/class/thermal/%s/type", de->d_name);
                read_sys_str(path, type_str, sizeof(type_str));

                snprintf(path, sizeof(path), "/sys/class/thermal/%s/temp", de->d_name);
                if (read_sys_long(path, &t_val) == 0 && t_val > 0) {
                    double t_deg = (t_val > 1000) ? ((double)t_val / 1000.0) : ((double)t_val);
                    if (t_deg > 0.0 && t_deg < 120.0) {
                        const char *tc = (t_deg > 75.0) ? COLOR_ERR : (t_deg > 55.0) ? COLOR_WARN : COLOR_OK;
                        printf("  • %-28.28s : %s%5.1f °C%s\n", type_str, tc, t_deg, COLOR_RESET);
                        sensors_count++;
                    }
                }
            }
        }
        closedir(tdir);
    }
    if (sensors_count == 0) {
        printf("  • Sensores térmicos integrados operando em modo protegido.\n");
    }

    printf("  ---------------------------------------------------------------------------------\n");
    if (live_mode) {
        printf("  %s[Modo Ao Vivo - Atualizando a cada 1s | Pressione Ctrl+C para sair]%s\n\n", COLOR_MUTED, COLOR_RESET);
    }
}

int main(int argc, char *argv[]) {
    utilipc_init();

    int live_mode = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            utilipc_close();
            return 0;
        }
        if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--watch") == 0) {
            live_mode = 1;
        }
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    if (live_mode) {
        printf("\033[?25l\033[H\033[J");
        while (keep_running) {
            render_pwr_dashboard(1);
            sleep(1);
        }
        printf("\033[?25h\033[0m\n");
    } else {
        render_pwr_dashboard(0);
    }

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "pwr: hardware power & thermal audited");
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
