#include "idt.h"
#include "../freestanding/kprintf.h"

static idt_entry_t idt[256];
static idt_ptr_t idt_ptr;
static volatile uint32_t timer_ticks = 0;

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

static void pit_init(uint32_t hz) {
    uint32_t divisor = 1193180 / hz;
    outb(0x43, 0x36); // Canal 0, modo square wave
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

uint32_t pit_get_ticks(void) {
    return timer_ticks;
}

uint32_t pit_get_seconds(void) {
    return timer_ticks / 100;
}

static void pic_remap(void) {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20); /* Master PIC offset 32 */
    outb(0xA1, 0x28); /* Slave PIC offset 40 */
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    /* Desmascara IRQ0 (Timer), IRQ1 (Teclado), IRQ2 (Cascade) */
    outb(0x21, 0xF8); 

    /* Desmascara IRQ12 (Mouse no Slave) */
    outb(0xA1, 0xEF); 
}

void idt_init(void) {
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint32_t)&idt;

    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    pic_remap();
    pit_init(100); // 100Hz = 1 tick a cada 10ms

    idt_set_gate(0,  (uint32_t)isr0,  0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(32, (uint32_t)irq0,  0x08, 0x8E); /* IRQ 0: Timer */
    idt_set_gate(33, (uint32_t)irq1,  0x08, 0x8E); /* IRQ 1: Keyboard */
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E); /* IRQ 12: Mouse */

    __asm__ __volatile__ ("lidt %0" : : "m"(idt_ptr));
    __asm__ __volatile__ ("sti");
}

extern void ps2_keyboard_handler(void);
extern void ps2_mouse_handler(void);

void isr_handler(registers_t *regs) {
    if (regs->int_no == 32) {
        timer_ticks++;
        outb(0x20, 0x20); // EOI Master
    } else if (regs->int_no == 33) {
        ps2_keyboard_handler();
        outb(0x20, 0x20);
    } else if (regs->int_no == 44) {
        ps2_mouse_handler();
        outb(0xA0, 0x20); // EOI Slave
        outb(0x20, 0x20); // EOI Master
    } else if (regs->int_no == 0) {
        kprintf("\n[Divide by zero exception]\n");
        __asm__ __volatile__("cli; hlt");
    }
}
