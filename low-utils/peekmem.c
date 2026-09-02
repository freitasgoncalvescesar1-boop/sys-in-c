#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <ctype.h>
#include "low.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_WARN    "\033[1;33m"
#define COLOR_ADDR    "\033[1;35m"
#define COLOR_ASCII   "\033[1;32m"
#define COLOR_NULL    "\033[0;90m"
#define COLOR_BIN     "\033[1;36m"
#define COLOR_LABEL   "\033[1;37m"

typedef struct {
    uintptr_t start;
    uintptr_t end;
    char perms[5];
    char pathname[256];
} MemRegion;

static void print_help(void) {
    low_print_banner("peekmem");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./peekmem <PID> [OPTIONS]\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Advanced process virtual memory inspector, mapper, and scanner (/proc/[PID]/mem).\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-m, --maps%s                 List all virtual memory mappings (Heap, Stack, Code, Libs)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-d, --dump <ADDR> [SIZE]%s    Hexdump SIZE bytes from ADDR (Default size: 64)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-s, --find-str <STRING>%s     Search for a string pattern across writable memory\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-i, --find-int <INT32>%s      Search for a 32-bit integer across writable memory\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s                  Display this formatted help guide and exit\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --version%s               Display version and repository information\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./peekmem 1234 -m%s                           (Exibe mapa de memoria do processo)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./peekmem 1234 -d 0x7fff5bc00000 128%s        (Hexdump de 128 bytes no endereco)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./peekmem 1234 -s \"minha_senha\"%s            (Busca onde a string esta na RAM)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./peekmem 1234 -i 9999%s                      (Busca ocorrencias do inteiro 9999)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

static uintptr_t parse_addr(const char *str) {
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        return (uintptr_t)strtoull(str, NULL, 16);
    }
    return (uintptr_t)strtoull(str, NULL, 16); // assume hex padrão para ponteiros
}

static int get_regions(pid_t pid, MemRegion **out_regions, size_t *out_count) {
    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "  %s[ERRO]%s Nao foi possivel ler '%s': %s (O processo existe?)\n",
                COLOR_ERR, COLOR_RESET, path, strerror(errno));
        return -1;
    }

    size_t cap = 64;
    size_t count = 0;
    MemRegion *regions = malloc(cap * sizeof(MemRegion));

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        uintptr_t s = 0, e = 0;
        char perms[8] = "";
        char dev[16] = "";
        unsigned long offset = 0, inode = 0;
        char name[256] = "";

        int fields = sscanf(line, "%lx-%lx %4s %lx %s %lu %255[^\n]", &s, &e, perms, &offset, dev, &inode, name);
        if (fields >= 3) {
            if (count >= cap) {
                cap *= 2;
                regions = realloc(regions, cap * sizeof(MemRegion));
            }
            regions[count].start = s;
            regions[count].end = e;
            strncpy(regions[count].perms, perms, 4);
            regions[count].perms[4] = '\0';
            strncpy(regions[count].pathname, (fields >= 7) ? name : "[anon]", sizeof(regions[count].pathname) - 1);
            regions[count].pathname[sizeof(regions[count].pathname) - 1] = '\0';
            count++;
        }
    }
    fclose(fp);

    *out_regions = regions;
    *out_count = count;
    return 0;
}

static void show_maps(pid_t pid) {
    MemRegion *regions = NULL;
    size_t count = 0;
    if (get_regions(pid, &regions, &count) < 0) return;

    printf("  %s%-18s  %-18s  %-6s  %s%s\n",
           COLOR_LABEL, "START ADDR", "END ADDR", "PERMS", "MAPPING / PATH", COLOR_RESET);
    printf("  ----------------------------------------------------------------------\n");

    for (size_t i = 0; i < count; i++) {
        const char *col = COLOR_RESET;
        if (strstr(regions[i].pathname, "[heap]")) col = COLOR_OK;
        else if (strstr(regions[i].pathname, "[stack]")) col = COLOR_WARN;
        else if (regions[i].perms[2] == 'x') col = COLOR_BIN;

        printf("  %s0x%016lx%s  %s0x%016lx%s  %-6s  %s%s%s\n",
               COLOR_ADDR, regions[i].start, COLOR_RESET,
               COLOR_ADDR, regions[i].end, COLOR_RESET,
               regions[i].perms,
               col, regions[i].pathname, COLOR_RESET);
    }
    printf("\n  Total de regioes mapeadas: %zu\n\n", count);
    free(regions);
}

static void dump_memory(pid_t pid, uintptr_t addr, size_t size) {
    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/mem", pid);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "  %s[ERRO]%s Nao foi possivel abrir '%s': %s (Verifique as permissoes do processo)\n",
                COLOR_ERR, COLOR_RESET, path, strerror(errno));
        return;
    }

    unsigned char *buf = malloc(size);
    if (!buf) {
        close(fd);
        return;
    }

    ssize_t n = pread(fd, buf, size, (off_t)addr);
    close(fd);

    if (n < 0) {
        fprintf(stderr, "  %s[ERRO]%s Falha ao ler memoria em 0x%lx: %s (Endereco fora de mapeamento legivel)\n",
                COLOR_ERR, COLOR_RESET, addr, strerror(errno));
        free(buf);
        return;
    }

    printf("\n  %s[DUMP: PID %d | Endereco: 0x%016lx | %zd bytes lidos]:%s\n\n",
           COLOR_WARN, pid, addr, n, COLOR_RESET);

    for (size_t i = 0; i < (size_t)n; i += 16) {
        printf("  %s0x%016lx:%s ", COLOR_ADDR, addr + i, COLOR_RESET);

        for (size_t j = 0; j < 16; j++) {
            if (i + j < (size_t)n) {
                unsigned char c = buf[i + j];
                if (c == 0x00) printf("%s%02x%s ", COLOR_NULL, c, COLOR_RESET);
                else if (c >= 32 && c <= 126) printf("%s%02x%s ", COLOR_ASCII, c, COLOR_RESET);
                else printf("%s%02x%s ", COLOR_BIN, c, COLOR_RESET);
            } else {
                printf("   ");
            }
            if (j == 7) printf(" ");
        }

        printf(" |");
        for (size_t j = 0; j < 16; j++) {
            if (i + j < (size_t)n) {
                unsigned char c = buf[i + j];
                if (c >= 32 && c <= 126) printf("%s%c%s", COLOR_ASCII, c, COLOR_RESET);
                else printf("%s.%s", COLOR_NULL, COLOR_RESET);
            } else {
                printf(" ");
            }
        }
        printf("|\n");
    }
    printf("\n");
    free(buf);
}

static void scan_for_string(pid_t pid, const char *needle) {
    MemRegion *regions = NULL;
    size_t count = 0;
    if (get_regions(pid, &regions, &count) < 0) return;

    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/mem", pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "  %s[ERRO]%s Nao foi possivel abrir '%s': %s\n", COLOR_ERR, COLOR_RESET, path, strerror(errno));
        free(regions);
        return;
    }

    size_t needle_len = strlen(needle);
    int matches = 0;

    printf("  %sProcurando string \"%s\" na memoria do PID %d...%s\n\n", COLOR_WARN, needle, pid, COLOR_RESET);

    for (size_t i = 0; i < count; i++) {
        // Escaneia apenas regioes legiveis ('r')
        if (regions[i].perms[0] != 'r') continue;

        size_t sz = regions[i].end - regions[i].start;
        if (sz == 0 || sz > 64 * 1024 * 1024) continue; // Pula blocos maiores que 64MB para performance

        unsigned char *buf = malloc(sz);
        if (!buf) continue;

        ssize_t n = pread(fd, buf, sz, (off_t)regions[i].start);
        if (n > 0) {
            for (size_t b = 0; b + needle_len <= (size_t)n; b++) {
                if (memcmp(buf + b, needle, needle_len) == 0) {
                    uintptr_t match_addr = regions[i].start + b;
                    printf("  %s[MATCH #%d]%s Endereco: %s0x%016lx%s em %s%s%s (Perms: %s)\n",
                           COLOR_OK, ++matches, COLOR_RESET,
                           COLOR_ADDR, match_addr, COLOR_RESET,
                           COLOR_LABEL, regions[i].pathname, COLOR_RESET, regions[i].perms);
                }
            }
        }
        free(buf);
    }
    close(fd);
    free(regions);

    printf("\n  Busca finalizada. Ocorrencias encontradas: %d\n\n", matches);
}

static void scan_for_int(pid_t pid, int32_t target_val) {
    MemRegion *regions = NULL;
    size_t count = 0;
    if (get_regions(pid, &regions, &count) < 0) return;

    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/mem", pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "  %s[ERRO]%s Nao foi possivel abrir '%s': %s\n", COLOR_ERR, COLOR_RESET, path, strerror(errno));
        free(regions);
        return;
    }

    int matches = 0;
    printf("  %sProcurando inteiro 32-bit (%d / 0x%08X) na memoria do PID %d...%s\n\n",
           COLOR_WARN, target_val, (uint32_t)target_val, pid, COLOR_RESET);

    for (size_t i = 0; i < count; i++) {
        if (regions[i].perms[0] != 'r') continue;

        size_t sz = regions[i].end - regions[i].start;
        if (sz == 0 || sz > 64 * 1024 * 1024) continue;

        unsigned char *buf = malloc(sz);
        if (!buf) continue;

        ssize_t n = pread(fd, buf, sz, (off_t)regions[i].start);
        if (n > 0) {
            // Alinhamento de 4 bytes para inteiros
            for (size_t b = 0; b + 4 <= (size_t)n; b += 4) {
                int32_t val = *(int32_t *)(buf + b);
                if (val == target_val) {
                    uintptr_t match_addr = regions[i].start + b;
                    printf("  %s[MATCH #%d]%s Endereco: %s0x%016lx%s em %s%s%s (Perms: %s)\n",
                           COLOR_OK, ++matches, COLOR_RESET,
                           COLOR_ADDR, match_addr, COLOR_RESET,
                           COLOR_LABEL, regions[i].pathname, COLOR_RESET, regions[i].perms);
                }
            }
        }
        free(buf);
    }
    close(fd);
    free(regions);

    printf("\n  Busca finalizada. Ocorrencias encontradas: %d\n\n", matches);
}

int main(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        print_help();
        return (argc < 2) ? 1 : 0;
    }

    pid_t pid = atoi(argv[1]);
    if (pid <= 0) {
        fprintf(stderr, "peekmem: PID invalido '%s'\n", argv[1]);
        return 1;
    }

    if (argc == 2 || strcmp(argv[2], "-m") == 0 || strcmp(argv[2], "--maps") == 0) {
        show_maps(pid);
        return 0;
    }

    if (strcmp(argv[2], "-d") == 0 || strcmp(argv[2], "--dump") == 0) {
        if (argc < 4) {
            fprintf(stderr, "peekmem: informe o endereco para dump (ex: ./peekmem %d -d 0x7fff... [tamanho])\n", pid);
            return 1;
        }
        uintptr_t addr = parse_addr(argv[3]);
        size_t size = (argc >= 5) ? (size_t)strtoull(argv[4], NULL, 10) : 64;
        dump_memory(pid, addr, size);
        return 0;
    }

    if (strcmp(argv[2], "-s") == 0 || strcmp(argv[2], "--find-str") == 0) {
        if (argc < 4) {
            fprintf(stderr, "peekmem: informe a string para busca (ex: ./peekmem %d -s \"palavra\")\n", pid);
            return 1;
        }
        scan_for_string(pid, argv[3]);
        return 0;
    }

    if (strcmp(argv[2], "-i") == 0 || strcmp(argv[2], "--find-int") == 0) {
        if (argc < 4) {
            fprintf(stderr, "peekmem: informe o inteiro para busca (ex: ./peekmem %d -i 12345)\n", pid);
            return 1;
        }
        int32_t val = (int32_t)atoi(argv[3]);
        scan_for_int(pid, val);
        return 0;
    }

    print_help();
    return 1;
}
