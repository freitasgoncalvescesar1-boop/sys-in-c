#ifndef IDT_H
#define IDT_H

#include <stdint.h>

typedef struct {
    uint16_t base_low;
    uint16_t selector;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
} registers_t;

void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

uint32_t pit_get_ticks(void);
uint32_t pit_get_seconds(void);

extern void isr0(void);   /* Divide Error */
extern void isr13(void);  /* GPF */
extern void isr14(void);  /* Page Fault */
extern void irq0(void);   /* PIT Timer (IRQ 0) */
extern void irq1(void);   /* PS/2 Keyboard (IRQ 1) */
extern void irq12(void);  /* PS/2 Mouse (IRQ 12) */

#endif
