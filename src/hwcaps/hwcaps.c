#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <errno.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET "\033[0m"
#define COLOR_TITLE "\033[1;35m"
#define COLOR_OK    "\033[1;32m"
#define COLOR_ERR   "\033[1;31m"
#define COLOR_TAG   "\033[1;33m"
#define COLOR_VAL   "\033[1;36m"
#define COLOR_MUTED "\033[0;90m"

// Definições de flags ARM / ARM64 caso não estejam no header
#ifndef HWCAP_ASIMD
#define HWCAP_ASIMD (1 << 1)
#endif
#ifndef HWCAP_AES
#define HWCAP_AES (1 << 3)
#endif
#ifndef HWCAP_PMULL
#define HWCAP_PMULL (1 << 4)
#endif
#ifndef HWCAP_SHA1
#define HWCAP_SHA1 (1 << 5)
#endif
#ifndef HWCAP_SHA2
#define HWCAP_SHA2 (1 << 6)
#endif
#ifndef HWCAP_CRC32
#define HWCAP_CRC32 (1 << 7)
#endif
#ifndef HWCAP_ATOMICS
#define HWCAP_ATOMICS (1 << 8)
#endif
#ifndef HWCAP_FPHP
#define HWCAP_FPHP (1 << 9)
#endif
#ifndef HWCAP_ASIMDHP
#define HWCAP_ASIMDHP (1 << 10)
#endif
#ifndef HWCAP_SVE
#define HWCAP_SVE (1 << 22)
#endif

typedef struct {
    const char *name;
    const char *desc;
    unsigned long mask;
} CapInfo;

static const CapInfo arm64_caps[] = {
    {"ASIMD / NEON", "Vetorização SIMD Avançada de 128-bit", HWCAP_ASIMD},
    {"AES",          "Aceleração de Criptografia AES em Hardware", HWCAP_AES},
    {"PMULL",        "Multiplicação Polinomial (AES-GCM)", HWCAP_PMULL},
    {"SHA2 / SHA256","Aceleração de Hash SHA-256 em Hardware", HWCAP_SHA2},
    {"SHA1",         "Aceleração de Hash SHA-1 em Hardware", HWCAP_SHA1},
    {"CRC32",        "Instruções de Checksum CRC32 por Hardware", HWCAP_CRC32},
    {"ATOMICS (LSE)","Instruções Atômicas de Baixa Latência (SMP)", HWCAP_ATOMICS},
    {"FP16 / FPHP",  "Ponto Flutuante de Meia-Precisão (IA/ML)", HWCAP_FPHP},
    {"SVE",          "Scalable Vector Extension (Supercomputação)", HWCAP_SVE},
    {NULL, NULL, 0}
};

static void print_help(void) {
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ hwcaps - CPU Architecture & Hardware Instruction Audit ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  hwcaps                     (Audita capacidades e acelerações da CPU)\n");
    printf("  hwcaps --help              (Exibe esta ajuda)\n");
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_help();
        utilipc_close();
        return 0;
    }

    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ hwcaps - CPU Hardware Capabilities & Instruction Set Audit ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n\n", COLOR_TITLE, COLOR_RESET);

    // Identificação de Arquitetura
    const char *arch_name = "Desconhecida";
#if defined(__aarch64__)
    arch_name = "ARM64 / AArch64 (64-bit Little-Endian)";
#elif defined(__arm__)
    arch_name = "ARM 32-bit (ARMv7-A)";
#elif defined(__x86_64__)
    arch_name = "x86_64 / AMD64 (Intel/AMD 64-bit)";
#elif defined(__i386__)
    arch_name = "x86 / i386 (Intel 32-bit)";
#elif defined(__riscv)
    arch_name = "RISC-V";
#endif

    printf("  %s• Arquitetura da CPU :%s %s%s%s\n", COLOR_TAG, COLOR_RESET, COLOR_VAL, arch_name, COLOR_RESET);
    printf("  %s• Núcleos Ativos     :%s %s%ld núcleos%s\n", COLOR_TAG, COLOR_RESET, COLOR_VAL, sysconf(_SC_NPROCESSORS_ONLN), COLOR_RESET);
    printf("  %s• Tamanho da Página  :%s %s%ld bytes (%ld KB)%s\n\n", COLOR_TAG, COLOR_RESET, COLOR_VAL, sysconf(_SC_PAGESIZE), sysconf(_SC_PAGESIZE) / 1024, COLOR_RESET);

    unsigned long hwcaps = getauxval(AT_HWCAP);

    printf("  %s%-18s  %-52s  %s%s\n", COLOR_TAG, "INSTRUÇÃO / RECURSO", "DESCRIÇÃO TÉCNICA", "STATUS", COLOR_RESET);
    printf("  ---------------------------------------------------------------------------------\n");

#if defined(__aarch64__) || defined(__arm__)
    int supported_count = 0;
    for (int i = 0; arm64_caps[i].name != NULL; i++) {
        int is_supported = (hwcaps & arm64_caps[i].mask) != 0;
        if (is_supported) supported_count++;

        printf("  %-18s  %-52s  %s\n",
               arm64_caps[i].name,
               arm64_caps[i].desc,
               is_supported ? "\033[1;32m[ SUPORTADO ]\033[0m" : "\033[0;90m[ AUSENTE ]\033[0m");
    }
    printf("  ---------------------------------------------------------------------------------\n");
    printf("  %s• Vetor Auxiliar AT_HWCAP:%s 0x%016lx (%d recursos suportados)\n\n",
           COLOR_TAG, COLOR_RESET, hwcaps, supported_count);
#else
    printf("  • Diagnóstico detalhado de flags disponível para plataformas ARM/ARM64.\n\n");
#endif

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "hwcaps: audited CPU %s", arch_name);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
