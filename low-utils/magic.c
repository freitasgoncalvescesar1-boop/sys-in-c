#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdint.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_TAG     "\033[1;33m"
#define COLOR_VAL     "\033[1;36m"
#define COLOR_FILE    "\033[1;35m"
#define COLOR_MUTED   "\033[0;90m"

static void print_help(void) {
    low_print_banner("magic");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./magic <FILE...>\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Identify real file types and metadata using binary Magic Numbers (Header Signatures).\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-h, --help%s       Display this formatted help guide and exit\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --version%s    Display version and repository information\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./magic /bin/ls%s                (Identifica binario executavel ELF)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./magic foto_falsa.txt%s         (Desmascara imagem renomeada para .txt)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

static void identify_magic(const char *filepath) {
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "  %s[ERRO]%s %s: %s\n", COLOR_ERR, COLOR_RESET, filepath, strerror(errno));
        return;
    }

    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    ssize_t n = read(fd, buf, sizeof(buf));
    close(fd);

    if (n <= 0) {
        printf("  %s%s%s: %s[Arquivo Vazio / 0 bytes]%s\n", COLOR_FILE, filepath, COLOR_RESET, COLOR_MUTED, COLOR_RESET);
        return;
    }

    char type_desc[256] = "Dados Binarios Desconhecidos";
    char hex_magic[64] = "";
    snprintf(hex_magic, sizeof(hex_magic), "%02X %02X %02X %02X", buf[0], buf[1], buf[2], buf[3]);

    // 1. Executáveis ELF (Linux / Android)
    if (buf[0] == 0x7F && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F') {
        const char *bits = (buf[4] == 2) ? "64-bit" : "32-bit";
        uint16_t machine = buf[18] | (buf[19] << 8);
        const char *arch = "Unknown";
        if (machine == 183 || machine == 0xB7) arch = "ARM64 / AArch64";
        else if (machine == 62) arch = "x86_64";
        else if (machine == 40) arch = "ARM 32-bit";
        else if (machine == 3) arch = "Intel 80386";
        snprintf(type_desc, sizeof(type_desc), "ELF %s Executable / Shared Object (%s)", bits, arch);
    }
    // 2. Imagens PNG
    else if (buf[0] == 0x89 && buf[1] == 0x50 && buf[2] == 0x4E && buf[3] == 0x47) {
        uint32_t width = (buf[16] << 24) | (buf[17] << 16) | (buf[18] << 8) | buf[19];
        uint32_t height = (buf[20] << 24) | (buf[21] << 16) | (buf[22] << 8) | buf[23];
        snprintf(type_desc, sizeof(type_desc), "PNG Image Data (%u x %u pixels)", width, height);
    }
    // 3. Imagens JPEG
    else if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
        snprintf(type_desc, sizeof(type_desc), "JPEG / JFIF Image Data");
    }
    // 4. Arquivos ZIP / APK / JAR / DOCX
    else if (buf[0] == 0x50 && buf[1] == 0x4B && buf[2] == 0x03 && buf[3] == 0x04) {
        snprintf(type_desc, sizeof(type_desc), "ZIP Compressed Archive / Android APK Package");
    }
    // 5. Documentos PDF
    else if (buf[0] == 0x25 && buf[1] == 0x50 && buf[2] == 0x44 && buf[3] == 0x46) {
        snprintf(type_desc, sizeof(type_desc), "PDF Document (Version %c.%c)", buf[5], buf[7]);
    }
    // 6. GZIP
    else if (buf[0] == 0x1F && buf[1] == 0x8B) {
        snprintf(type_desc, sizeof(type_desc), "GZIP Compressed File");
    }
    // 7. SQLite 3 Database
    else if (memcmp(buf, "SQLite format 3", 15) == 0) {
        snprintf(type_desc, sizeof(type_desc), "SQLite 3 Database File");
    }
    // 8. Imagens BMP
    else if (buf[0] == 'B' && buf[1] == 'M') {
        int32_t w = *(int32_t *)(buf + 18);
        int32_t h = *(int32_t *)(buf + 22);
        snprintf(type_desc, sizeof(type_desc), "BMP Image Data (%d x %d pixels)", w, h);
    }
    // 9. Shebang Script (#!/bin/sh, etc.)
    else if (buf[0] == '#' && buf[1] == '!') {
        char shebang[64] = "";
        char *nl = strchr((char *)buf, '\n');
        size_t len = nl ? (size_t)(nl - (char *)buf) : sizeof(shebang) - 1;
        snprintf(shebang, sizeof(shebang), "%.*s", (int)len, (char *)buf);
        snprintf(type_desc, sizeof(type_desc), "Executable Script (%s)", shebang);
    }
    // 10. Krypt Vault (Nosso Cofre)
    else if (memcmp(buf, "KRYPT26\0", 8) == 0) {
        snprintf(type_desc, sizeof(type_desc), "sys-in-c Krypt Encrypted Vault (ChaCha20)");
    }
    // 11. Texto ASCII puro
    else {
        int is_ascii = 1;
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] < 9 || (buf[i] > 13 && buf[i] < 32)) { is_ascii = 0; break; }
        }
        if (is_ascii) snprintf(type_desc, sizeof(type_desc), "ASCII / UTF-8 Plain Text Document");
    }

    printf("  %s%s%s\n", COLOR_FILE, filepath, COLOR_RESET);
    printf("    • Tipo Real : %s%s%s\n", COLOR_OK, type_desc, COLOR_RESET);
    printf("    • Assinatura: %s[%s]%s\n\n", COLOR_MUTED, hex_magic, COLOR_RESET);
}

int main(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help();
        return (argc < 2) ? 1 : 0;
    }

    printf("\n");
    for (int i = 1; i < argc; i++) {
        identify_magic(argv[i]);
    }
    return 0;
}
