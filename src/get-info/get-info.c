#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_LABEL   "\033[1;36m"
#define COLOR_VAL     "\033[1;32m"
#define COLOR_WARN    "\033[1;33m"
#define COLOR_BAD     "\033[1;31m"

typedef struct {
    const char *name;
    const char *soc;
    unsigned long antutu;
    const char *ram;
    const char *battery;
    const char *verdict;
} PhoneInfo;

typedef struct {
    const char *name;
    int type; // 0 = CPU, 1 = GPU
    unsigned int score;
    unsigned int tdp_w;
    const char *verdict;
} PcPartInfo;

static const PhoneInfo phone_db[] = {
    {"Redmi 14C", "MediaTek Helio G81-Ultra", 270000, "4GB / 6GB / 8GB LPDDR4X", "5160 mAh (18W)", "COMPENSA PARA USO BÁSICO. Tela grande de 120Hz e excelente bateria pelo preço."},
    {"Redmi 13C", "MediaTek Helio G85", 260000, "4GB / 6GB / 8GB LPDDR4X", "5000 mAh (18W)", "COMPENSA BÁSICO. Tela 90Hz boa para redes sociais, mas fraco em jogos."},
    {"Redmi 12", "MediaTek Helio G88", 270000, "4GB / 8GB LPDDR4X", "5000 mAh (18W)", "COMPENSA BÁSICO. Construção em vidro bonita, porém desempenho modesto."},
    {"Poco X6 Pro", "Dimensity 8300 Ultra", 1400000, "8GB / 12GB LPDDR5X", "5000 mAh (67W)", "SUPER COMPENSA! Imbatível em desempenho e jogos na categoria."},
    {"Poco M6 Pro", "Helio G99 Ultra", 450000, "8GB / 12GB LPDDR4X", "5000 mAh (67W)", "COMPENSA BASTANTE! Tela AMOLED 120Hz e carregamento rápido."},
    {"Galaxy A55", "Exynos 1480", 720000, "8GB LPDDR5", "5000 mAh (25W)", "SUPER COMPENSA! Construção em alumínio, proteção IP67 e ótimas câmeras."},
    {"Galaxy A54", "Exynos 1380", 520000, "8GB LPDDR4X", "5000 mAh (25W)", "COMPENSA! Ótimas câmeras, proteção IP67 e boa construção."},
    {"Galaxy S23", "Snapdragon 8 Gen 2", 1520000, "8GB LPDDR5X", "3900 mAh (25W)", "SUPER COMPENSA! Top de linha compacto espetacular."},
    {"Galaxy S24", "Exynos 2400 / SD 8 Gen 3", 1750000, "8GB / 12GB LPDDR5X", "4000 mAh (25W)", "SUPER COMPENSA! Recursos Galaxy AI e tela espetacular."},
    {"iPhone 11", "Apple A13 Bionic", 650000, "4GB LPDDR4X", "3110 mAh (18W)", "COMPENSA SE ESTIVER BARATO. Tela HD mas ainda Roda tudo fluido."},
    {"iPhone 13", "Apple A15 Bionic", 1280000, "4GB LPDDR4X", "3240 mAh (20W)", "SUPER COMPENSA! Excelente bateria, câmeras e longevidade."},
    {"iPhone 14", "Apple A15 Bionic (5-core GPU)", 1320000, "6GB LPDDR4X", "3279 mAh (20W)", "COMPENSA se a diferença de preço para o iPhone 13 for pequena."},
    {"iPhone 15 Pro", "Apple A17 Pro", 1550000, "8GB LPDDR5", "3274 mAh (20W)", "SUPER COMPENSA PREMIUM! Estrutura de titânio e suporte a jogos de PC."},
    {NULL, NULL, 0, NULL, NULL, NULL}
};

static const PcPartInfo pc_db[] = {
    {"Ryzen 5 5600", 0, 21500, 65, "SUPER COMPENSA! Melhor CPU custo-benefício AM4."},
    {"Core i5 12400F", 0, 19800, 65, "SUPER COMPENSA! Processador frio e excelente para jogos."},
    {"Ryzen 7 5700X3D", 0, 27500, 105, "SUPER COMPENSA PARA JOGOS! Cache L3 gigante."},
    {"Core i3 12100F", 0, 14200, 58, "COMPENSA BASTANTE para setups de entrada baratos."},
    {"RTX 3060", 1, 17100, 170, "COMPENSA se encontrada em promoção (12GB VRAM é ótimo)."},
    {"RX 6600", 1, 15200, 132, "SUPER COMPENSA! Imbatível em custo-benefício em 1080p."},
    {"RTX 4060", 1, 20500, 115, "COMPENSA pelo DLSS 3 e baixíssimo consumo elétrico."},
    {"RTX 4070 Super", 1, 31800, 220, "SUPER COMPENSA para Quad HD (1440p) com Ray Tracing."},
    {NULL, 0, 0, 0, NULL}
};

static void normalize_str(const char *in, char *out, size_t max_len) {
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j < max_len - 1; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c)) {
            out[j++] = tolower(c);
        } else if (c == '-' || c == '_' || c == '.' || c == ' ') {
            if (j > 0 && out[j-1] != ' ') {
                out[j++] = ' ';
            }
        }
    }
    while (j > 0 && out[j-1] == ' ') j--;
    out[j] = '\0';
}

static int matches_name(const char *db_name, const char *query) {
    char norm_db[256];
    char norm_query[256];
    normalize_str(db_name, norm_db, sizeof(norm_db));
    normalize_str(query, norm_query, sizeof(norm_query));

    return (strstr(norm_db, norm_query) != NULL || strstr(norm_query, norm_db) != NULL);
}

static void fetch_online_wikipedia(const char *query) {
    printf("  • Conectando e buscando Ficha Técnica na Web (Wikipedia API) para: '\033[1;36m%s\033[0m'...\n", query);

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo("en.wikipedia.org", "80", &hints, &res) != 0) {
        printf("  • \033[1;31mErro: Não foi possível conectar ao servidor de busca online.\033[0m\n");
        return;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return;
    }

    struct timeval tv = { .tv_sec = 4, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        printf("  • \033[1;31mErro de conexão com o servidor de busca.\033[0m\n");
        close(fd);
        freeaddrinfo(res);
        return;
    }

    char sanitized[256] = "";
    for (size_t i = 0, j = 0; query[i] != '\0' && j < sizeof(sanitized) - 1; i++) {
        if (isalnum((unsigned char)query[i])) sanitized[j++] = query[i];
        else sanitized[j++] = '+';
    }

    char req[1024];
    snprintf(req, sizeof(req),
        "GET /w/api.php?action=query&list=search&srsearch=%s+smartphone&format=json HTTP/1.1\r\n"
        "Host: en.wikipedia.org\r\n"
        "User-Agent: utils-in-c-hardware-bot/1.0\r\n"
        "Connection: close\r\n\r\n", sanitized);

    send(fd, req, strlen(req), 0);

    size_t resp_cap = 65536;
    char *response = malloc(resp_cap);
    if (!response) {
        close(fd);
        freeaddrinfo(res);
        return;
    }

    ssize_t total_bytes = 0;
    ssize_t n;
    while ((n = recv(fd, response + total_bytes, resp_cap - total_bytes - 1, 0)) > 0) {
        total_bytes += n;
        if (total_bytes >= (ssize_t)(resp_cap - 1)) {
            size_t new_cap = resp_cap * 2;
            char *new_resp = realloc(response, new_cap);
            if (!new_resp) break;
            response = new_resp;
            resp_cap = new_cap;
        }
    }
    response[total_bytes] = '\0';

    close(fd);
    freeaddrinfo(res);

    char *snippet_ptr = strstr(response, "\"snippet\":\"");
    if (!snippet_ptr) snippet_ptr = strstr(response, "\"snippet\": \"");

    if (snippet_ptr) {
        snippet_ptr += (snippet_ptr[10] == '"') ? 11 : 12;
        char *end_snippet = strchr(snippet_ptr, '"');
        if (end_snippet) *end_snippet = '\0';

        char clean_snippet[2048];
        size_t c_idx = 0;
        int in_tag = 0;
        for (size_t i = 0; snippet_ptr[i] != '\0' && c_idx < sizeof(clean_snippet) - 1; i++) {
            if (snippet_ptr[i] == '<') in_tag = 1;
            else if (snippet_ptr[i] == '>') in_tag = 0;
            else if (!in_tag) {
                if (snippet_ptr[i] == '\\' && snippet_ptr[i+1] == '"') {
                    clean_snippet[c_idx++] = '"';
                    i++;
                } else {
                    clean_snippet[c_idx++] = snippet_ptr[i];
                }
            }
        }
        clean_snippet[c_idx] = '\0';

        printf("\n  \033[1;32m[Resultado Extraído em Tempo Real da Web]:\033[0m\n");
        printf("  • \033[0;36mResumo das Specs:\033[0m %s\n", clean_snippet);
        printf("  • \033[1;33mVeredito Web:\033[0m Dispositivo localizado no banco de dados global.\n");
    } else {
        printf("  • Dispositivo não localizado na busca online estendida.\n");
    }

    free(response);
}

static void lookup_phone(const char *model) {
    printf("\n%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ get-info - Smartphone Intelligence ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);

    int found = 0;
    for (int i = 0; phone_db[i].name != NULL; i++) {
        if (matches_name(phone_db[i].name, model)) {
            printf("  %s• Modelo:        %s%s\n", COLOR_LABEL, COLOR_VAL, phone_db[i].name);
            printf("  %s• Processador:   %s%s\n", COLOR_LABEL, COLOR_RESET, phone_db[i].soc);
            printf("  %s• Pontuação AnTuTu: %s%lu pts%s\n", COLOR_LABEL, COLOR_VAL, phone_db[i].antutu, COLOR_RESET);
            printf("  %s• Memória RAM:   %s%s\n", COLOR_LABEL, COLOR_RESET, phone_db[i].ram);
            printf("  %s• Bateria:       %s%s\n", COLOR_LABEL, COLOR_RESET, phone_db[i].battery);
            printf("  %s• Veredito:      %s%s%s\n", COLOR_LABEL, COLOR_WARN, phone_db[i].verdict, COLOR_RESET);
            printf("------------------------------------------\n");
            found = 1;
        }
    }

    if (!found) {
        fetch_online_wikipedia(model);
    }
    printf("%s==========================================%s\n\n", COLOR_TITLE, COLOR_RESET);
}

static void lookup_pc(const char *part) {
    printf("\n%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ get-info - PC Hardware Intelligence ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);

    int found = 0;
    for (int i = 0; pc_db[i].name != NULL; i++) {
        if (matches_name(pc_db[i].name, part)) {
            printf("  %s• Componente:    %s%s (%s)%s\n", COLOR_LABEL, COLOR_VAL, pc_db[i].name, pc_db[i].type == 0 ? "CPU" : "GPU", COLOR_RESET);
            printf("  %s• Benchmark Score: %s%u pts%s\n", COLOR_LABEL, COLOR_VAL, pc_db[i].score, COLOR_RESET);
            printf("  %s• Consumo (TDP): %s%u Watts%s\n", COLOR_LABEL, COLOR_RESET, pc_db[i].tdp_w);
            printf("  %s• Veredito:      %s%s%s\n", COLOR_LABEL, COLOR_WARN, pc_db[i].verdict, COLOR_RESET);
            printf("------------------------------------------\n");
            found = 1;
        }
    }

    if (!found) {
        fetch_online_wikipedia(part);
    }
    printf("%s==========================================%s\n\n", COLOR_TITLE, COLOR_RESET);
}

static void calculate_bottleneck(const char *cpu_name, const char *gpu_name) {
    unsigned int cpu_score = 18000;
    unsigned int gpu_score = 17000;

    for (int i = 0; pc_db[i].name != NULL; i++) {
        if (pc_db[i].type == 0 && matches_name(pc_db[i].name, cpu_name)) cpu_score = pc_db[i].score;
        if (pc_db[i].type == 1 && matches_name(pc_db[i].name, gpu_name)) gpu_score = pc_db[i].score;
    }

    double ratio = (double)gpu_score / (double)cpu_score;
    double bottleneck = 0.0;
    const char *status_msg = "";

    if (ratio > 1.35) {
        bottleneck = (ratio - 1.0) * 35.0;
        status_msg = "Gargalo de Processador (CPU Limitando a Placa de Vídeo)";
    } else if (ratio < 0.65) {
        bottleneck = (1.0 - ratio) * 35.0;
        status_msg = "Gargalo de Placa de Vídeo (GPU Limitando a CPU)";
    } else {
        bottleneck = (ratio > 1.0 ? ratio - 1.0 : 1.0 - ratio) * 15.0;
        status_msg = "Combinação Equilibrada! Excelente Custo-Benefício.";
    }

    if (bottleneck > 40.0) bottleneck = 40.0;

    printf("\n%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ Análise de Gargalo & Recomendações ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("  • CPU Escolhida : %s%s%s\n", COLOR_VAL, cpu_name, COLOR_RESET);
    printf("  • GPU Escolhida : %s%s%s\n", COLOR_VAL, gpu_name, COLOR_RESET);
    printf("  • Estímulo de Gargalo: %s%.1f%%%s\n", bottleneck > 18.0 ? COLOR_BAD : COLOR_VAL, bottleneck, COLOR_RESET);
    printf("  • Diagnóstico   : %s%s%s\n", COLOR_WARN, status_msg, COLOR_RESET);

    if (bottleneck > 18.0) {
        printf("\n  %s[Recomendações de Melhoria]:%s\n", COLOR_LABEL, COLOR_RESET);
        if (ratio > 1.35) {
            printf("    • Considere upgrade de CPU para: Ryzen 5 5600 / Core i5 12400F ou superior.\n");
        } else {
            printf("    • Sua CPU aguentaria uma GPU mais forte (ex: RTX 4060 ou RX 6700 XT).\n");
        }
    }
    printf("%s==========================================%s\n\n", COLOR_TITLE, COLOR_RESET);
}

static void run_shell(void) {
    printf("%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ get-info Interactive Shell ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Comandos disponíveis:\n");
    printf("  phone <modelo>     (Busca specs e AnTuTu de celular)\n");
    printf("  pc <peça>          (Busca specs de CPU/GPU e veredito)\n");
    printf("  bottleneck         (Modo interativo de cálculo de gargalo)\n");
    printf("  exit / quit        (Sair do shell)\n");
    printf("==========================================\n\n");

    char line[512];
    while (1) {
        printf("get-info> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' ')) line[--len] = '\0';
        if (len == 0) continue;

        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) break;

        if (strncmp(line, "phone ", 6) == 0) {
            lookup_phone(line + 6);
        } else if (strncmp(line, "pc ", 3) == 0) {
            lookup_pc(line + 3);
        } else if (strcmp(line, "bottleneck") == 0) {
            char cpu[128], gpu[128];
            printf("  Digite o modelo da CPU (ex: Ryzen 5 5600): ");
            fflush(stdout);
            if (!fgets(cpu, sizeof(cpu), stdin)) continue;
            cpu[strcspn(cpu, "\r\n")] = 0;

            printf("  Digite o modelo da GPU (ex: RTX 3060): ");
            fflush(stdout);
            if (!fgets(gpu, sizeof(gpu), stdin)) continue;
            gpu[strcspn(gpu, "\r\n")] = 0;

            calculate_bottleneck(cpu, gpu);
        } else {
            printf("Comando desconhecido. Use 'phone <modelo>', 'pc <peça>', 'bottleneck' ou 'exit'.\n");
        }
    }
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 2) {
        printf("Usage:\n");
        printf("  get-info -phone \"<modelo_celular>\"\n");
        printf("  get-info -pc \"<peça_pc>\"\n");
        printf("  get-info -shell\n");
        utilipc_close();
        return 1;
    }

    if (strcmp(argv[1], "-phone") == 0 && argc >= 3) {
        lookup_phone(argv[2]);
    } else if (strcmp(argv[1], "-pc") == 0 && argc >= 3) {
        lookup_pc(argv[2]);
    } else if (strcmp(argv[1], "-shell") == 0) {
        run_shell();
    } else {
        printf("Erro: Opção inválida. Use -phone, -pc ou -shell.\n");
    }

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "get-info: executed option %s", argv[1]);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
