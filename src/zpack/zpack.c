#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_TAG     "\033[1;33m"
#define COLOR_VAL     "\033[1;36m"
#define COLOR_MUTED   "\033[0;90m"
#define COLOR_PERM    "\033[0;33m"

#define ZPACK_MAGIC_V1 "ZPACK01\0"
#define ZPACK_MAGIC_V2 "ZPACK02\0"

#define HASH_BITS      14
#define HASH_SIZE      (1 << HASH_BITS) // 16.384 slots
#define MAX_DISTANCE   65535            // Janela de 64 KB (estilo LZ4)

#pragma pack(push, 1)
typedef struct {
    char     magic[8];       // "ZPACK02\0" ou "ZPACK01\0"
    uint32_t num_entries;
    uint64_t total_orig_sz;
    uint64_t total_comp_sz;
    uint32_t archive_crc;
} ZpackArchiveHeader;

typedef struct {
    uint16_t path_len;
    uint8_t  is_dir;
    uint32_t mode;
    uint64_t orig_size;
    uint64_t comp_size;
    uint32_t file_crc;
} ZpackEntryHeader;
#pragma pack(pop)

// Checksum CRC32 (IEEE 802.3)
static uint32_t calc_crc32(const uint8_t *data, size_t len, uint32_t current_crc) {
    uint32_t crc = ~current_crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

// Leitura atômica de 32 bits portátil
static inline uint32_t read32(const void *p) {
    uint32_t val;
    memcpy(&val, p, 4);
    return val;
}

// Hash multiplicativo de Fibonacci para 4 bytes
static inline uint32_t hash4(uint32_t val) {
    return (val * 2654435761U) >> (32 - HASH_BITS);
}

// =========================================================================
// MOTOR ZPACK 2.0: COMPRESSOR ULTRARRÁPIDO BYTE-ALIGNED (64KB WINDOW)
// =========================================================================
static size_t zpack2_compress(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_max) {
    if (!src || src_len == 0 || !dst || dst_max < src_len + 64) return 0;

    int32_t table[HASH_SIZE];
    for (int i = 0; i < HASH_SIZE; i++) table[i] = -1;

    size_t in_pos = 0;
    size_t anchor = 0;
    size_t out_pos = 0;

    while (in_pos + 4 <= src_len && out_pos + 32 < dst_max) {
        uint32_t h = hash4(read32(src + in_pos));
        int32_t ref = table[h];
        table[h] = (int32_t)in_pos;

        // Se encontrou repetição válida dentro da janela de 64KB com os primeiros 4 bytes idênticos
        if (ref != -1 && (in_pos - ref) <= MAX_DISTANCE && read32(src + ref) == read32(src + in_pos)) {
            size_t match_len = 4;
            while (in_pos + match_len < src_len && src[ref + match_len] == src[in_pos + match_len]) {
                match_len++;
            }

            size_t lit_len = in_pos - anchor;
            uint16_t offset = (uint16_t)(in_pos - ref);

            // Monta o token byte: [4 bits literais | 4 bits repetição]
            uint8_t token = 0;
            if (lit_len < 15) token |= (uint8_t)(lit_len << 4);
            else token |= (15 << 4);

            size_t m_token = match_len - 4;
            if (m_token < 15) token |= (uint8_t)m_token;
            else token |= 15;

            dst[out_pos++] = token;

            // Bytes extras de tamanho de literais
            if (lit_len >= 15) {
                size_t rem = lit_len - 15;
                while (rem >= 255) {
                    dst[out_pos++] = 255;
                    rem -= 255;
                }
                dst[out_pos++] = (uint8_t)rem;
            }

            // Copia os literais pendentes
            if (lit_len > 0) {
                memcpy(dst + out_pos, src + anchor, lit_len);
                out_pos += lit_len;
            }

            // Emite a distância (offset 16-bit little-endian)
            dst[out_pos++] = (uint8_t)(offset & 0xFF);
            dst[out_pos++] = (uint8_t)(offset >> 8);

            // Bytes extras de tamanho de repetição (permite repetições de 500+ bytes)
            if (m_token >= 15) {
                size_t rem = m_token - 15;
                while (rem >= 255) {
                    dst[out_pos++] = 255;
                    rem -= 255;
                }
                dst[out_pos++] = (uint8_t)rem;
            }

            in_pos += match_len;
            anchor = in_pos;
        } else {
            in_pos++;
        }
    }

    // Emite literais finais restantes
    size_t lit_len = src_len - anchor;
    if (lit_len > 0) {
        uint8_t token = (lit_len < 15) ? (uint8_t)(lit_len << 4) : (15 << 4);
        dst[out_pos++] = token;
        if (lit_len >= 15) {
            size_t rem = lit_len - 15;
            while (rem >= 255) {
                dst[out_pos++] = 255;
                rem -= 255;
            }
            dst[out_pos++] = (uint8_t)rem;
        }
        memcpy(dst + out_pos, src + anchor, lit_len);
        out_pos += lit_len;
    }

    return out_pos;
}

// =========================================================================
// MOTOR ZPACK 2.0: DESCOMPRESSOR ULTRARRÁPIDO
// =========================================================================
static size_t zpack2_decompress(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_max) {
    if (!src || !dst) return 0;

    size_t in_pos = 0;
    size_t out_pos = 0;

    while (in_pos < src_len && out_pos < dst_max) {
        uint8_t token = src[in_pos++];
        size_t lit_len = token >> 4;

        if (lit_len == 15) {
            uint8_t s;
            do {
                if (in_pos >= src_len) return 0;
                s = src[in_pos++];
                lit_len += s;
            } while (s == 255);
        }

        if (out_pos + lit_len > dst_max || in_pos + lit_len > src_len) return 0;
        memcpy(dst + out_pos, src + in_pos, lit_len);
        out_pos += lit_len;
        in_pos += lit_len;

        if (in_pos >= src_len) break;

        if (in_pos + 2 > src_len) return 0;
        uint16_t offset = (uint16_t)(src[in_pos] | (src[in_pos + 1] << 8));
        in_pos += 2;

        if (offset == 0 || offset > out_pos) return 0;

        size_t match_len = (token & 0x0F);
        if (match_len == 15) {
            uint8_t s;
            do {
                if (in_pos >= src_len) return 0;
                s = src[in_pos++];
                match_len += s;
            } while (s == 255);
        }
        match_len += 4;

        if (out_pos + match_len > dst_max) return 0;

        // Cópia direta do bloco de repetição
        for (size_t i = 0; i < match_len; i++) {
            dst[out_pos] = dst[out_pos - offset];
            out_pos++;
        }
    }
    return out_pos;
}

// Fallback de descompressão LZSS 1.0 para manter retrocompatibilidade
static size_t lzss1_decompress(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_max) {
    size_t in_pos = 0, out_pos = 0;
    while (in_pos < src_len && out_pos < dst_max) {
        uint8_t flags = src[in_pos++];
        for (int bit = 0; bit < 8 && in_pos < src_len && out_pos < dst_max; bit++) {
            if (flags & (1 << bit)) {
                if (in_pos + 1 >= src_len) break;
                uint16_t token = (uint16_t)((src[in_pos] << 8) | src[in_pos + 1]);
                in_pos += 2;
                int dist = (token >> 4) & 0x0FFF;
                int len  = (token & 0x0F) + 3;
                if (dist > (int)out_pos || out_pos + len > dst_max) break;
                for (int i = 0; i < len; i++) {
                    dst[out_pos] = dst[out_pos - dist];
                    out_pos++;
                }
            } else {
                dst[out_pos++] = src[in_pos++];
            }
        }
    }
    return out_pos;
}

static void print_help(void) {
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ zpack 2.0 - Blazing-Fast Pure C Archiver (64KB Window & Long Matches) ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  zpack -c <FILE/DIR...> [-o <ARCHIVE.zp>]     (Compactar arquivo ou pasta)\n");
    printf("  zpack -x <ARCHIVE.zp> [-o <DESTINO_DIR>]     (Descompactar e restaurar tudo)\n");
    printf("  zpack -l <ARCHIVE.zp>                        (Listar arquivos e taxas de compressao)\n");
    printf("  zpack -t <ARCHIVE.zp>                        (Testar integridade CRC32 de cada arquivo)\n");
    printf("  zpack --help                                 (Exibir este guia formatado)\n\n");
    printf("Destaques da versao 2.0:\n");
    printf("  • %sJanela de 64 KB%s                        : Capta repeticoes 16x mais distantes\n", COLOR_OK, COLOR_RESET);
    printf("  • %sRepeticoes Ilimitadas%s                  : Comprime blocos gigantes de codigo de uma vez\n", COLOR_OK, COLOR_RESET);
    printf("  • %sDescompressao em GB/s%s                  : Byte-aligned zero bit-twiddling\n\n", COLOR_OK, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

static void make_parent_dirs(const char *filepath) {
    char temp[1024];
    strncpy(temp, filepath, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    for (char *p = temp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(temp, 0755);
            *p = '/';
        }
    }
}

static void mode_to_str(mode_t mode, char *str) {
    strcpy(str, "----------");
    if (S_ISDIR(mode)) str[0] = 'd';
    else if (S_ISLNK(mode)) str[0] = 'l';

    if (mode & S_IRUSR) str[1] = 'r';
    if (mode & S_IWUSR) str[2] = 'w';
    if (mode & S_IXUSR) str[3] = 'x';
    if (mode & S_IRGRP) str[4] = 'r';
    if (mode & S_IWGRP) str[5] = 'w';
    if (mode & S_IXGRP) str[6] = 'x';
    if (mode & S_IROTH) str[7] = 'r';
    if (mode & S_IWOTH) str[8] = 'w';
    if (mode & S_IXOTH) str[9] = 'x';
}

static int pack_recursive(int out_fd, const char *base_dir, const char *rel_prefix,
                          uint32_t *entry_counter, uint64_t *total_orig, uint64_t *total_comp) {
    DIR *dir = opendir(base_dir);
    if (!dir) return -1;

    struct dirent *de;
    char full_path[2048];
    char rel_path[2048];

    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", base_dir, de->d_name);
        if (strlen(rel_prefix) > 0) snprintf(rel_path, sizeof(rel_path), "%s/%s", rel_prefix, de->d_name);
        else snprintf(rel_path, sizeof(rel_path), "%s", de->d_name);

        struct stat st;
        if (lstat(full_path, &st) != 0) continue;

        uint8_t is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
        uint16_t plen = (uint16_t)strlen(rel_path);

        ZpackEntryHeader eh;
        memset(&eh, 0, sizeof(eh));
        eh.path_len = plen;
        eh.is_dir = is_dir;
        eh.mode = (uint32_t)st.st_mode;

        if (is_dir) {
            write(out_fd, &eh, sizeof(eh));
            write(out_fd, rel_path, plen);
            (*entry_counter)++;
            pack_recursive(out_fd, full_path, rel_path, entry_counter, total_orig, total_comp);
        } else if (S_ISREG(st.st_mode)) {
            eh.orig_size = st.st_size;
            (*total_orig) += st.st_size;

            uint8_t *orig_buf = malloc(st.st_size + 64);
            size_t comp_buf_sz = st.st_size + 2048;
            uint8_t *comp_buf = malloc(comp_buf_sz);

            if (orig_buf && comp_buf) {
                int ifd = open(full_path, O_RDONLY);
                if (ifd >= 0) {
                    read(ifd, orig_buf, st.st_size);
                    close(ifd);
                }

                eh.file_crc = calc_crc32(orig_buf, st.st_size, 0);

                size_t c_size = zpack2_compress(orig_buf, st.st_size, comp_buf, comp_buf_sz);
                if (c_size == 0 || c_size >= (size_t)st.st_size) {
                    eh.comp_size = st.st_size;
                    write(out_fd, &eh, sizeof(eh));
                    write(out_fd, rel_path, plen);
                    write(out_fd, orig_buf, st.st_size);
                    (*total_comp) += st.st_size;
                } else {
                    eh.comp_size = c_size;
                    write(out_fd, &eh, sizeof(eh));
                    write(out_fd, rel_path, plen);
                    write(out_fd, comp_buf, c_size);
                    (*total_comp) += c_size;
                }

                double ratio = (st.st_size > 0) ? (1.0 - ((double)eh.comp_size / (double)st.st_size)) * 100.0 : 0.0;
                printf("  %s• %-36.36s%s %6.1f KB -> %6.1f KB  %s[%5.1f%%]%s\n",
                       COLOR_VAL, rel_path, COLOR_RESET,
                       (double)st.st_size / 1024.0, (double)eh.comp_size / 1024.0,
                       (ratio > 0.0) ? COLOR_OK : COLOR_MUTED, ratio, COLOR_RESET);
            }

            if (orig_buf) free(orig_buf);
            if (comp_buf) free(comp_buf);
            (*entry_counter)++;
        }
    }
    closedir(dir);
    return 0;
}

static int create_archive(const char *target, const char *out_archive) {
    struct stat st;
    if (lstat(target, &st) != 0) {
        fprintf(stderr, "zpack: alvo '%s' nao encontrado: %s\n", target, strerror(errno));
        return -1;
    }

    int out_fd = open(out_archive, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        fprintf(stderr, "zpack: erro ao criar arquivo '%s': %s\n", out_archive, strerror(errno));
        return -1;
    }

    ZpackArchiveHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, ZPACK_MAGIC_V2, 8);
    write(out_fd, &hdr, sizeof(hdr));

    printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s│%s  %s[ 🗜️ ZPACK 2.0 - Compactando com Motor 64KB Window & Long Matches ]%s    %s│%s\n",
           COLOR_TITLE, COLOR_RESET, COLOR_OK, COLOR_RESET, COLOR_TITLE, COLOR_RESET);
    printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n", COLOR_TITLE, COLOR_RESET);

    uint32_t total_entries = 0;
    uint64_t total_orig = 0;
    uint64_t total_comp = 0;

    if (S_ISDIR(st.st_mode)) {
        pack_recursive(out_fd, target, "", &total_entries, &total_orig, &total_comp);
    } else {
        const char *base_name = strrchr(target, '/');
        base_name = base_name ? base_name + 1 : target;

        ZpackEntryHeader eh;
        memset(&eh, 0, sizeof(eh));
        eh.path_len = strlen(base_name);
        eh.is_dir = 0;
        eh.mode = (uint32_t)st.st_mode;
        eh.orig_size = st.st_size;
        total_orig = st.st_size;

        uint8_t *orig_buf = malloc(st.st_size + 64);
        size_t comp_buf_sz = st.st_size + 2048;
        uint8_t *comp_buf = malloc(comp_buf_sz);

        if (orig_buf && comp_buf) {
            int ifd = open(target, O_RDONLY);
            if (ifd >= 0) { read(ifd, orig_buf, st.st_size); close(ifd); }

            eh.file_crc = calc_crc32(orig_buf, st.st_size, 0);
            size_t c_sz = zpack2_compress(orig_buf, st.st_size, comp_buf, comp_buf_sz);

            if (c_sz == 0 || c_sz >= (size_t)st.st_size) {
                eh.comp_size = st.st_size;
                write(out_fd, &eh, sizeof(eh));
                write(out_fd, base_name, eh.path_len);
                write(out_fd, orig_buf, st.st_size);
                total_comp = st.st_size;
            } else {
                eh.comp_size = c_sz;
                write(out_fd, &eh, sizeof(eh));
                write(out_fd, base_name, eh.path_len);
                write(out_fd, comp_buf, c_sz);
                total_comp = c_sz;
            }

            double ratio = (st.st_size > 0) ? (1.0 - ((double)eh.comp_size / (double)st.st_size)) * 100.0 : 0.0;
            printf("  %s• %-36.36s%s %6.1f KB -> %6.1f KB  %s[%5.1f%%]%s\n",
                   COLOR_VAL, base_name, COLOR_RESET,
                   (double)st.st_size / 1024.0, (double)eh.comp_size / 1024.0,
                   (ratio > 0.0) ? COLOR_OK : COLOR_MUTED, ratio, COLOR_RESET);
        }
        if (orig_buf) free(orig_buf);
        if (comp_buf) free(comp_buf);
        total_entries = 1;
    }

    hdr.num_entries = total_entries;
    hdr.total_orig_sz = total_orig;
    hdr.total_comp_sz = total_comp;
    hdr.archive_crc = calc_crc32((const uint8_t *)&hdr, sizeof(hdr) - 4, 0);

    lseek(out_fd, 0, SEEK_SET);
    write(out_fd, &hdr, sizeof(hdr));
    close(out_fd);

    double total_ratio = (total_orig > 0) ? (1.0 - ((double)total_comp / (double)total_orig)) * 100.0 : 0.0;
    printf("\n  ----------------------------------------------------------------------------\n");
    printf("  %s✔ Pacote criado com sucesso:%s %s%s%s\n", COLOR_OK, COLOR_RESET, COLOR_TAG, out_archive, COLOR_RESET);
    printf("  • Entradas: %u | Original: %.1f KB | Compactado: %.1f KB (%s%.1f%% economizado%s)\n\n",
           total_entries, (double)total_orig / 1024.0, (double)total_comp / 1024.0,
           COLOR_OK, total_ratio, COLOR_RESET);

    return 0;
}

static int extract_archive(const char *in_archive, const char *dest_dir) {
    int in_fd = open(in_archive, O_RDONLY);
    if (in_fd < 0) {
        fprintf(stderr, "zpack: erro ao abrir arquivo '%s': %s\n", in_archive, strerror(errno));
        return -1;
    }

    ZpackArchiveHeader hdr;
    if (read(in_fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        close(in_fd);
        return -1;
    }

    int is_v2 = (memcmp(hdr.magic, ZPACK_MAGIC_V2, 8) == 0);
    int is_v1 = (memcmp(hdr.magic, ZPACK_MAGIC_V1, 8) == 0);

    if (!is_v1 && !is_v2) {
        fprintf(stderr, "zpack: '%s' nao e um arquivo valido do zpack!\n", in_archive);
        close(in_fd);
        return -1;
    }

    mkdir(dest_dir, 0755);

    printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s│%s  %s[ 📦 ZPACK %s - Descompactando & Restaurando Arquivos ]%s              %s│%s\n",
           COLOR_TITLE, COLOR_RESET, COLOR_OK, is_v2 ? "2.0" : "1.0", COLOR_RESET, COLOR_TITLE, COLOR_RESET);
    printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n", COLOR_TITLE, COLOR_RESET);

    uint32_t extracted_count = 0;
    ZpackEntryHeader eh;

    while (read(in_fd, &eh, sizeof(eh)) == sizeof(eh)) {
        char rel_path[1024];
        read(in_fd, rel_path, eh.path_len);
        rel_path[eh.path_len] = '\0';

        char full_dest[2048];
        if (strcmp(dest_dir, ".") == 0) snprintf(full_dest, sizeof(full_dest), "%s", rel_path);
        else snprintf(full_dest, sizeof(full_dest), "%s/%s", dest_dir, rel_path);

        if (eh.is_dir) {
            mkdir(full_dest, (mode_t)eh.mode);
            extracted_count++;
            continue;
        }

        make_parent_dirs(full_dest);

        uint8_t *comp_buf = malloc(eh.comp_size + 64);
        uint8_t *orig_buf = malloc(eh.orig_size + 64);

        if (comp_buf && orig_buf) {
            read(in_fd, comp_buf, eh.comp_size);

            if (eh.comp_size == eh.orig_size) {
                memcpy(orig_buf, comp_buf, eh.orig_size);
            } else if (is_v2) {
                zpack2_decompress(comp_buf, eh.comp_size, orig_buf, eh.orig_size);
            } else {
                lzss1_decompress(comp_buf, eh.comp_size, orig_buf, eh.orig_size);
            }

            uint32_t check = calc_crc32(orig_buf, eh.orig_size, 0);
            int crc_ok = (check == eh.file_crc);

            int ofd = open(full_dest, O_WRONLY | O_CREAT | O_TRUNC, (mode_t)eh.mode);
            if (ofd >= 0) {
                write(ofd, orig_buf, eh.orig_size);
                close(ofd);
                chmod(full_dest, (mode_t)eh.mode);
            }

            printf("  %s• %-42.42s%s %6.1f KB %s[%s]%s\n",
                   COLOR_VAL, rel_path, COLOR_RESET,
                   (double)eh.orig_size / 1024.0,
                   crc_ok ? COLOR_OK : COLOR_ERR,
                   crc_ok ? "CRC-OK" : "CRC-FALHA", COLOR_RESET);
            extracted_count++;
        }

        if (comp_buf) free(comp_buf);
        if (orig_buf) free(orig_buf);
    }
    close(in_fd);

    printf("\n  ----------------------------------------------------------------------------\n");
    printf("  %s✔ Descompactação finalizada! Total de itens restaurados: %u%s\n\n", COLOR_OK, extracted_count, COLOR_RESET);
    return 0;
}

static int list_archive(const char *in_archive, int test_only) {
    int in_fd = open(in_archive, O_RDONLY);
    if (in_fd < 0) {
        fprintf(stderr, "zpack: erro ao abrir '%s': %s\n", in_archive, strerror(errno));
        return -1;
    }

    ZpackArchiveHeader hdr;
    if (read(in_fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        close(in_fd);
        return -1;
    }

    int is_v2 = (memcmp(hdr.magic, ZPACK_MAGIC_V2, 8) == 0);
    int is_v1 = (memcmp(hdr.magic, ZPACK_MAGIC_V1, 8) == 0);

    if (!is_v1 && !is_v2) {
        fprintf(stderr, "zpack: '%s' nao e um arquivo valido do zpack!\n", in_archive);
        close(in_fd);
        return -1;
    }

    printf("\n%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ zpack %s - %s: %s ]%s\n",
           COLOR_TITLE, is_v2 ? "2.0" : "1.0", test_only ? "Teste de Integridade CRC32" : "Conteúdo do Arquivo", in_archive, COLOR_RESET);
    printf("%s=================================================================================%s\n\n", COLOR_TITLE, COLOR_RESET);
    printf("  %s%-10s %-12s %-12s %-8s %s%s\n",
           COLOR_TAG, "PERMISSÕES", "ORIGINAL", "COMPACTADO", "REDUÇÃO", "CAMINHO", COLOR_RESET);
    printf("  ---------------------------------------------------------------------------------\n");

    ZpackEntryHeader eh;
    int errors = 0;

    while (read(in_fd, &eh, sizeof(eh)) == sizeof(eh)) {
        char rel_path[1024];
        read(in_fd, rel_path, eh.path_len);
        rel_path[eh.path_len] = '\0';

        char perm_str[12];
        mode_to_str((mode_t)eh.mode, perm_str);

        if (eh.is_dir) {
            printf("  %s%s%s %-12s %-12s %-8s %s%s/%s\n",
                   COLOR_PERM, perm_str, COLOR_RESET, "-", "-", "-", COLOR_VAL, rel_path, COLOR_RESET);
            continue;
        }

        double ratio = (eh.orig_size > 0) ? (1.0 - ((double)eh.comp_size / (double)eh.orig_size)) * 100.0 : 0.0;

        if (test_only) {
            uint8_t *comp_buf = malloc(eh.comp_size + 64);
            uint8_t *orig_buf = malloc(eh.orig_size + 64);
            if (comp_buf && orig_buf) {
                read(in_fd, comp_buf, eh.comp_size);
                if (eh.comp_size == eh.orig_size) memcpy(orig_buf, comp_buf, eh.orig_size);
                else if (is_v2) zpack2_decompress(comp_buf, eh.comp_size, orig_buf, eh.orig_size);
                else lzss1_decompress(comp_buf, eh.comp_size, orig_buf, eh.orig_size);

                uint32_t c = calc_crc32(orig_buf, eh.orig_size, 0);
                if (c != eh.file_crc) errors++;
            }
            if (comp_buf) free(comp_buf);
            if (orig_buf) free(orig_buf);
        } else {
            lseek(in_fd, eh.comp_size, SEEK_CUR);
        }

        char orig_sz_str[32], comp_sz_str[32];
        snprintf(orig_sz_str, sizeof(orig_sz_str), "%.1f KB", (double)eh.orig_size / 1024.0);
        snprintf(comp_sz_str, sizeof(comp_sz_str), "%.1f KB", (double)eh.comp_size / 1024.0);

        printf("  %s%s%s %-12s %-12s %s%5.1f%%%s  %s%s%s\n",
               COLOR_PERM, perm_str, COLOR_RESET,
               orig_sz_str, comp_sz_str,
               (ratio > 0.0) ? COLOR_OK : COLOR_MUTED, ratio, COLOR_RESET,
               COLOR_VAL, rel_path, COLOR_RESET);
    }
    close(in_fd);

    double total_ratio = (hdr.total_orig_sz > 0) ? (1.0 - ((double)hdr.total_comp_sz / (double)hdr.total_orig_sz)) * 100.0 : 0.0;
    printf("  ---------------------------------------------------------------------------------\n");
    printf("  Total: %u entradas | Original: %.1f KB | Compactado: %.1f KB (%s%.1f%% economizado%s)\n",
           hdr.num_entries, (double)hdr.total_orig_sz / 1024.0, (double)hdr.total_comp_sz / 1024.0,
           COLOR_OK, total_ratio, COLOR_RESET);

    if (test_only) {
        printf("  Integridade CRC32: %s\n", errors == 0 ? "\033[1;32m[ 100% OK - Sem Erros ]\033[0m" : "\033[1;31m[ CORROMPIDO ]\033[0m");
    }
    printf("%s=================================================================================%s\n\n", COLOR_TITLE, COLOR_RESET);
    return errors ? 1 : 0;
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help();
        utilipc_close();
        return 0;
    }

    int mode_compress = 0, mode_extract = 0, mode_list = 0, mode_test = 0;
    const char *target = NULL;
    char out_path[512] = "";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--compress") == 0) {
            mode_compress = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') target = argv[++i];
        } else if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--extract") == 0) {
            mode_extract = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') target = argv[++i];
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            mode_list = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') target = argv[++i];
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--test") == 0) {
            mode_test = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') target = argv[++i];
        } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            strncpy(out_path, argv[++i], sizeof(out_path) - 1);
        } else if (!target && argv[i][0] != '-') {
            target = argv[i];
        }
    }

    if (!target) {
        print_help();
        utilipc_close();
        return 1;
    }

    int res = 0;
    if (mode_compress) {
        if (strlen(out_path) == 0) {
            char clean[512];
            strncpy(clean, target, sizeof(clean) - 1);
            size_t l = strlen(clean);
            while (l > 1 && clean[l - 1] == '/') clean[--l] = '\0';
            snprintf(out_path, sizeof(out_path), "%s.zp", clean);
        }
        res = create_archive(target, out_path);
    } else if (mode_extract) {
        if (strlen(out_path) == 0) strcpy(out_path, ".");
        res = extract_archive(target, out_path);
    } else if (mode_list) {
        res = list_archive(target, 0);
    } else if (mode_test) {
        res = list_archive(target, 1);
    } else {
        print_help();
        res = 1;
    }

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "zpack: processed '%s'", target);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return res;
}
