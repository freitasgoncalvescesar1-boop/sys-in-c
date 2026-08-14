#include "idt.h"
#include "../freestanding/kprintf.h"

static idt_entry_t idt[256];
static idt_ptr_t idt_ptr;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__ ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__ ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].selector = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

static void pic_remap(void) {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20); /* Master PIC vector offset 32 (0x20) */
    outb(0xA1, 0x28); /* Slave PIC vector offset 40 (0x28) */
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    /* Unmask IRQ1 (Keyboard) and IRQ2 (Slave PIC Cascade) on Master PIC */
    outb(0x21, 0xF9); 

    /* Unmask IRQ12 (Mouse) on Slave PIC */
    outb(0xA1, 0xEF); 
}

void idt_init(void) {
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint32_t)&idt;

    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    pic_remap();

    idt_set_gate(0,  (uint32_t)isr0,  0x08, 0x8E); /* Divide Error */
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E); /* GPF */
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E); /* Page Fault */
    idt_set_gate(33, (uint32_t)irq1,  0x08, 0x8E); /* IRQ 1: Keyboard */
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E); /* IRQ 12: Mouse */

    __asm__ __volatile__ ("lidt %0" : : "m"(idt_ptr));
    __asm__ __volatile__ ("sti");
}

extern void ps2_keyboard_handler(void);
extern void ps2_mouse_handler(void);

void isr_handler(registers_t *regs) {
    if (regs->int_no == 0) {
        static int div_caught = 0;
        if (!div_caught) {
            kprintf("\n  [IDT EXCEPTION 0]: Divide-by-Zero Exception Caught!\n");
            kprintf("  [System Halted to prevent cascading faults]\n");
            div_caught = 1;
        }
        __asm__ __volatile__("cli; hlt");
    } else if (regs->int_no == 13) {
        kprintf("\n  [IDT EXCEPTION 13]: General Protection Fault (GPF) Caught!\n");
        __asm__ __volatile__("cli; hlt");
    } else if (regs->int_no == 14) {
        kprintf("\n  [IDT EXCEPTION 14]: Page Fault Caught!\n");
        __asm__ __volatile__("cli; hlt");
    } else if (regs->int_no == 33) {
        ps2_keyboard_handler();
        outb(0x20, 0x20); /* Send PIC EOI */
    } else if (regs->int_no == 44) {
        ps2_mouse_handler();
        outb(0xA0, 0x20); /* EOI Slave */
        outb(0x20, 0x20); /* EOI Master */
    }
}
