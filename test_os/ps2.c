#include "ps2.h"
#include "../freestanding/kprintf.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__ ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__ ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void ps2_wait_write(void) {
    int timeout = 100000;
    while (timeout-- && (inb(0x64) & 2));
}

static void ps2_wait_read(void) {
    int timeout = 100000;
    while (timeout-- && !(inb(0x64) & 1));
}

static kgfx_mouse_t mouse_state = { .x = 400, .y = 300, .buttons = 0, .type = KGFX_CURSOR_NORMAL };
static uint8_t mouse_cycle = 0;
static int8_t mouse_packet[3];

static int ctrl_pressed = 0;
static int shift_pressed = 0;

extern void os_handle_keypress(char c);
extern void os_toggle_cli_mode(void);
extern void os_draw_mouse_cursor(void);

static const char scancode_ascii[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

static const char scancode_ascii_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

void ps2_init(void) {
    // 1. Desabilita portas durante configuração
    ps2_wait_write(); outb(0x64, 0xAD);
    ps2_wait_write(); outb(0x64, 0xA7);

    // 2. Limpa buffer de saída
    while (inb(0x64) & 1) inb(0x60);

    // 3. Lê e configura o Command Byte da Controladora PS/2
    ps2_wait_write(); outb(0x64, 0x20);
    ps2_wait_read();
    uint8_t status = inb(0x60);

    // Ativa IRQ1 (bit 0), IRQ12 (bit 1), Tradução ScanCode (bit 6) e limpa clocks desabilitados (bits 4, 5)
    status = (status | 0x47) & ~0x30;

    ps2_wait_write(); outb(0x64, 0x60);
    ps2_wait_write(); outb(0x60, status);

    // 4. Habilita portas do Teclado (0xAE) e do Mouse (0xA8)
    ps2_wait_write(); outb(0x64, 0xAE);
    ps2_wait_write(); outb(0x64, 0xA8);

    // 5. Ativa escaneamento do Teclado
    ps2_wait_write(); outb(0x60, 0xF4);
    ps2_wait_read(); inb(0x60); // ACK

    // 6. Configura e Habilita Mouse PS/2
    ps2_wait_write(); outb(0x64, 0xD4);
    ps2_wait_write(); outb(0x60, 0xF6); // Defaults
    ps2_wait_read(); inb(0x60);

    ps2_wait_write(); outb(0x64, 0xD4);
    ps2_wait_write(); outb(0x60, 0xF4); // Enable streaming
    ps2_wait_read(); inb(0x60);
}

void ps2_keyboard_handler(void) {
    if (!(inb(0x64) & 1)) return;
    uint8_t scancode = inb(0x60);

    if (scancode == 0x2A || scancode == 0x36) { shift_pressed = 1; return; }
    if (scancode == 0xAA || scancode == 0xB6) { shift_pressed = 0; return; }
    if (scancode == 0x1D) { ctrl_pressed = 1; return; }
    if (scancode == 0x9D) { ctrl_pressed = 0; return; }

    if (!(scancode & 0x80)) {
        // ESC (0x01), F1 (0x3B), TAB (0x0F) ou Ctrl+V (0x2F) alternam CLI/GUI
        if (scancode == 0x01 || scancode == 0x3B || scancode == 0x0F || (ctrl_pressed && scancode == 0x2F)) {
            os_toggle_cli_mode();
            return;
        }

        char c = shift_pressed ? scancode_ascii_shift[scancode] : scancode_ascii[scancode];
        if (c) {
            os_handle_keypress(c);
        }
    }
}

void ps2_mouse_handler(void) {
    if (!(inb(0x64) & 1)) return;
    uint8_t data = inb(0x60);

    switch (mouse_cycle) {
        case 0:
            if (data & 0x08) {
                mouse_packet[0] = (int8_t)data;
                mouse_cycle = 1;
            }
            break;
        case 1:
            mouse_packet[1] = (int8_t)data;
            mouse_cycle = 2;
            break;
        case 2: {
            mouse_packet[2] = (int8_t)data;
            mouse_cycle = 0;

            uint8_t flags = (uint8_t)mouse_packet[0];
            int rel_x = (int)mouse_packet[1];
            int rel_y = (int)mouse_packet[2];

            if (flags & 0x10) rel_x |= 0xFFFFFF00;
            if (flags & 0x20) rel_y |= 0xFFFFFF00;
            if (flags & 0xC0) break; // Descarta se estourou buffer

            mouse_state.buttons = flags & 0x07;
            mouse_state.x += rel_x;
            mouse_state.y -= rel_y;

            if (mouse_state.x < 0) mouse_state.x = 0;
            if (mouse_state.x >= 800) mouse_state.x = 799;
            if (mouse_state.y < 0) mouse_state.y = 0;
            if (mouse_state.y >= 600) mouse_state.y = 599;

            if (mouse_state.buttons & 1) {
                mouse_state.type = KGFX_CURSOR_CLICKABLE;
            } else {
                mouse_state.type = KGFX_CURSOR_NORMAL;
            }
            break;
        }
    }
}

kgfx_mouse_t *ps2_get_mouse_state(void) {
    return &mouse_state;
}
