#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <elf.h>
#include <errno.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_ADDR    "\033[1;33m"
#define COLOR_HEX     "\033[0;90m"
#define COLOR_MNEM    "\033[1;32m"
#define COLOR_OPS     "\033[1;37m"
#define COLOR_LABEL   "\033[1;36m"

static void print_help(void) {
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ disasm - ELF Binary Machine Code Disassembler ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  disasm <BINARY_FILE> [OPTIONS]\n\n");
    printf("Options:\n");
    printf("  -s, --seek <OFFSET>    Start disassembling at byte offset\n");
    printf("  -l, --length <BYTES>   Disassemble up to BYTES of machine code [Default: 256]\n");
    printf("  --raw                  Disassemble raw binary without searching ELF .text\n");
    printf("  --help                 Display this formatted help guide\n\n");
    printf("Exemplos:\n");
    printf("  disasm ./whoami\n");
    printf("  disasm /bin/ls -l 64\n");
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

// Desmontador de Instruções ARM64 (AArch64 - 32-bit Opcodes)
static int disasm_arm64_instruction(uint32_t ins, uint64_t addr, char *mnem, char *ops) {
    // RET
    if (ins == 0xD65F03C0) { strcpy(mnem, "ret"); strcpy(ops, ""); return 4; }
    if (ins == 0xD503201F) { strcpy(mnem, "nop"); strcpy(ops, ""); return 4; }

    // STP (Store Pair): stp x29, x30, [sp, #-16]!
    if ((ins & 0xFFC00000) == 0xA9BF0000 || (ins & 0xFFC00000) == 0xA9800000 || (ins & 0x7FC00000) == 0x29800000) {
        uint8_t rt = ins & 0x1F, rt2 = (ins >> 10) & 0x1F, rn = (ins >> 5) & 0x1F;
        int32_t imm7 = (int32_t)((ins >> 15) & 0x7F);
        if (imm7 & 0x40) imm7 |= ~0x3F;
        strcpy(mnem, "stp");
        snprintf(ops, 64, "x%d, x%d, [%s, #%d]", rt, rt2, (rn == 31) ? "sp" : "x", imm7 * 8);
        return 4;
    }

    // LDP (Load Pair): ldp x29, x30, [sp], #16
    if ((ins & 0xFFC00000) == 0xA8C00000 || (ins & 0x7FC00000) == 0x28C00000) {
        uint8_t rt = ins & 0x1F, rt2 = (ins >> 10) & 0x1F, rn = (ins >> 5) & 0x1F;
        strcpy(mnem, "ldp");
        snprintf(ops, 64, "x%d, x%d, [%s]", rt, rt2, (rn == 31) ? "sp" : "x");
        return 4;
    }

    // MOVZ / MOVN / MOVK
    if ((ins & 0x7F800000) == 0x52800000) {
        uint8_t rd = ins & 0x1F;
        uint16_t imm16 = (ins >> 5) & 0xFFFF;
        strcpy(mnem, "mov");
        snprintf(ops, 64, "w%d, #%u", rd, imm16);
        return 4;
    }
    if ((ins & 0xFF800000) == 0xD2800000) {
        uint8_t rd = ins & 0x1F;
        uint16_t imm16 = (ins >> 5) & 0xFFFF;
        strcpy(mnem, "mov");
        snprintf(ops, 64, "x%d, #%u", rd, imm16);
        return 4;
    }

    // ADD / SUB (Immediate): add x29, sp, #0
    if ((ins & 0x7F000000) == 0x11000000 || (ins & 0x7F000000) == 0x51000000) {
        int is_sub = (ins & 0x40000000) != 0;
        int is_64 = (ins & 0x80000000) != 0;
        uint8_t rd = ins & 0x1F, rn = (ins >> 5) & 0x1F;
        uint16_t imm12 = (ins >> 10) & 0xFFF;
        strcpy(mnem, is_sub ? "sub" : "add");
        snprintf(ops, 64, "%c%d, %s, #%u", is_64 ? 'x' : 'w', rd, (rn == 31) ? "sp" : (is_64 ? "x" : "w"), imm12);
        return 4;
    }

    // ADD / SUB (Shifted Register): add x0, x1, x2
    if ((ins & 0x1F000000) == 0x0B000000 || (ins & 0x1F000000) == 0x4B000000) {
        int is_sub = (ins & 0x40000000) != 0;
        int is_64 = (ins & 0x80000000) != 0;
        uint8_t rd = ins & 0x1F, rn = (ins >> 5) & 0x1F, rm = (ins >> 16) & 0x1F;
        strcpy(mnem, is_sub ? "sub" : "add");
        snprintf(ops, 64, "%c%d, %c%d, %c%d", is_64 ? 'x' : 'w', rd, is_64 ? 'x' : 'w', rn, is_64 ? 'x' : 'w', rm);
        return 4;
    }

    // B (Unconditional Branch) & BL (Branch with Link / Call)
    if ((ins & 0x7C000000) == 0x14000000) {
        int is_bl = (ins & 0x80000000) != 0;
        int32_t imm26 = (int32_t)(ins & 0x03FFFFFF);
        if (imm26 & 0x02000000) imm26 |= ~0x01FFFFFF;
        uint64_t target = addr + (imm26 * 4);
        strcpy(mnem, is_bl ? "bl" : "b");
        snprintf(ops, 64, "0x%lx", target);
        return 4;
    }

    // BLR / BR (Register Branch)
    if ((ins & 0xFFFFFC1F) == 0xD63F0000 || (ins & 0xFFFFFC1F) == 0xD61F0000) {
        int is_blr = (ins & 0x00200000) != 0;
        uint8_t rn = (ins >> 5) & 0x1F;
        strcpy(mnem, is_blr ? "blr" : "br");
        snprintf(ops, 64, "x%d", rn);
        return 4;
    }

    // SVC (Syscall)
    if ((ins & 0xFFE0001F) == 0xD4000001) {
        uint16_t imm16 = (ins >> 5) & 0xFFFF;
        strcpy(mnem, "svc");
        snprintf(ops, 64, "#0x%x", imm16);
        return 4;
    }

    // Fallback genérico
    strcpy(mnem, ".word");
    snprintf(ops, 64, "0x%08x", ins);
    return 4;
}

// Desmontador de Instruções x86_64
static int disasm_x86_instruction(const uint8_t *p, uint64_t addr, char *mnem, char *ops) {
    (void)addr;
    if (p[0] == 0x90) { strcpy(mnem, "nop"); strcpy(ops, ""); return 1; }
    if (p[0] == 0xC3) { strcpy(mnem, "ret"); strcpy(ops, ""); return 1; }
    if (p[0] == 0x0F && p[1] == 0x05) { strcpy(mnem, "syscall"); strcpy(ops, ""); return 2; }
    if (p[0] == 0xCD && p[1] == 0x80) { strcpy(mnem, "int"); strcpy(ops, "0x80"); return 2; }

    // PUSH / POP
    if (p[0] >= 0x50 && p[0] <= 0x57) {
        static const char *regs[] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi"};
        strcpy(mnem, "push");
        snprintf(ops, 64, "%s", regs[p[0] - 0x50]);
        return 1;
    }
    if (p[0] >= 0x58 && p[0] <= 0x5F) {
        static const char *regs[] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi"};
        strcpy(mnem, "pop");
        snprintf(ops, 64, "%s", regs[p[0] - 0x58]);
        return 1;
    }

    // MOV reg, imm32 (0xB8 + reg)
    if (p[0] >= 0xB8 && p[0] <= 0xBF) {
        static const char *regs[] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
        uint32_t val = p[1] | (p[2] << 8) | (p[3] << 16) | (p[4] << 24);
        strcpy(mnem, "mov");
        snprintf(ops, 64, "%s, 0x%x", regs[p[0] - 0xB8], val);
        return 5;
    }

    // REX.W prefix (0x48)
    if (p[0] == 0x48) {
        if (p[1] == 0x89 && p[2] == 0xE5) { strcpy(mnem, "mov"); strcpy(ops, "rbp, rsp"); return 3; }
        if (p[1] == 0x83 && p[2] == 0xEC) { strcpy(mnem, "sub"); snprintf(ops, 64, "rsp, 0x%x", p[3]); return 4; }
        if (p[1] == 0x83 && p[2] == 0xC4) { strcpy(mnem, "add"); snprintf(ops, 64, "rsp, 0x%x", p[3]); return 4; }
        if (p[1] == 0x31 && p[2] == 0xC0) { strcpy(mnem, "xor"); strcpy(ops, "rax, rax"); return 3; }
    }

    // Fallback genérico
    strcpy(mnem, ".byte");
    snprintf(ops, 64, "0x%02x", p[0]);
    return 1;
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help();
        utilipc_close();
        return 0;
    }

    const char *filepath = argv[1];
    off_t seek_offset = 0;
    size_t max_bytes = 256;
    int raw_mode = 0;

    for (int i = 2; i < argc; i++) {
        if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--seek") == 0) && i + 1 < argc) {
            seek_offset = (off_t)strtoull(argv[++i], NULL, 0);
        } else if ((strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--length") == 0) && i + 1 < argc) {
            max_bytes = (size_t)strtoull(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--raw") == 0) {
            raw_mode = 1;
        }
    }

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        fprintf(stderr, "disasm: erro ao abrir '%s': %s\n", filepath, strerror(errno));
        utilipc_close();
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint8_t *file_buf = malloc(fsize);
    if (!file_buf) { fclose(fp); return 1; }
    fread(file_buf, 1, fsize, fp);
    fclose(fp);

    int is_arm64 = 1;
    uint64_t text_offset = 0;
    uint64_t text_vaddr = 0x1000;
    size_t text_size = fsize;

    // Parser ELF para localizar a seção .text
    if (!raw_mode && fsize > (long)sizeof(Elf64_Ehdr) && file_buf[0] == 0x7F && file_buf[1] == 'E') {
        Elf64_Ehdr *ehdr = (Elf64_Ehdr *)file_buf;
        if (ehdr->e_machine == EM_AARCH64) is_arm64 = 1;
        else if (ehdr->e_machine == EM_X86_64) is_arm64 = 0;

        if (ehdr->e_shoff > 0 && ehdr->e_shnum > 0) {
            Elf64_Shdr *shdrs = (Elf64_Shdr *)(file_buf + ehdr->e_shoff);
            const char *shstrtab = (const char *)(file_buf + shdrs[ehdr->e_shstrndx].sh_offset);

            for (int i = 0; i < ehdr->e_shnum; i++) {
                const char *sname = shstrtab + shdrs[i].sh_name;
                if (strcmp(sname, ".text") == 0) {
                    text_offset = shdrs[i].sh_offset;
                    text_vaddr = shdrs[i].sh_addr;
                    text_size = shdrs[i].sh_size;
                    break;
                }
            }
        }
    }

    if (seek_offset > 0) {
        text_offset += seek_offset;
        text_vaddr += seek_offset;
        if (text_size > (size_t)seek_offset) text_size -= seek_offset;
    }

    if (max_bytes > 0 && text_size > max_bytes) {
        text_size = max_bytes;
    }

    printf("\n%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ disasm - Desmontador de Código de Máquina Assembly (%s) ]%s\n",
           COLOR_TITLE, is_arm64 ? "ARM64 / AArch64" : "x86_64", COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("  • Arquivo    : \033[1;36m%s\033[0m\n", filepath);
    printf("  • Offset     : 0x%lx (VAddr: 0x%lx) | Desmontando: %zu bytes\n\n", text_offset, text_vaddr, text_size);
    printf("  %s%-18s  %-20s  %-8s  %s%s\n", COLOR_LABEL, "ENDEREÇO (VADDR)", "BYTES HEX", "INSTRUÇÃO", "OPERANDOS", COLOR_RESET);
    printf("  ---------------------------------------------------------------------------------\n");

    size_t cursor = text_offset;
    size_t end_cursor = text_offset + text_size;
    uint64_t cur_vaddr = text_vaddr;

    while (cursor < end_cursor && cursor < (size_t)fsize) {
        char mnem[32] = "";
        char ops[128] = "";
        int len = 0;

        char hex_str[32] = "";

        if (is_arm64) {
            uint32_t ins = *(uint32_t *)(file_buf + cursor);
            len = disasm_arm64_instruction(ins, cur_vaddr, mnem, ops);
            snprintf(hex_str, sizeof(hex_str), "%02x %02x %02x %02x",
                     file_buf[cursor], file_buf[cursor+1], file_buf[cursor+2], file_buf[cursor+3]);
        } else {
            len = disasm_x86_instruction(file_buf + cursor, cur_vaddr, mnem, ops);
            char tmp[8];
            for (int k = 0; k < len; k++) {
                snprintf(tmp, sizeof(tmp), "%02x ", file_buf[cursor + k]);
                strcat(hex_str, tmp);
            }
        }

        printf("  %s0x%016lx:%s  %s%-16.16s%s  %s%-8s%s  %s%s%s\n",
               COLOR_ADDR, cur_vaddr, COLOR_RESET,
               COLOR_HEX, hex_str, COLOR_RESET,
               COLOR_MNEM, mnem, COLOR_RESET,
               COLOR_OPS, ops, COLOR_RESET);

        cursor += len;
        cur_vaddr += len;
    }

    printf("  ---------------------------------------------------------------------------------\n\n");

    free(file_buf);
    utilipc_close();
    return 0;
}
