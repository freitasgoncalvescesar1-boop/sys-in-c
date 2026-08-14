#include "gdt.h"

static gdt_entry_t gdt[5];
static gdt_ptr_t gdt_ptr;

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;

    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;

    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access = access;
}

void gdt_init(void) {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 5) - 1;
    gdt_ptr.base = (uint32_t)&gdt;

    gdt_set_gate(0, 0, 0, 0, 0);                 /* Null segment */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);  /* Kernel Code segment (0x08) */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);  /* Kernel Data segment (0x10) */
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);  /* User Code segment */
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);  /* User Data segment */

    /* Carrega GDT e recarrega registradores de segmento */
    __asm__ __volatile__ (
        "lgdt %0\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        : : "m"(gdt_ptr) : "eax"
    );
}
