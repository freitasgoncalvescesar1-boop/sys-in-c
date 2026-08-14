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

static char input_buf[128] = "";
static size_t input_pos = 0;
static int ctrl_pressed = 0;

extern void os_handle_keypress(char c);
extern void os_toggle_cli_mode(void);
extern void os_draw_mouse_cursor(void);

static const char scancode_ascii[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

void ps2_init(void) {
    /* Enable Auxiliary PS/2 Mouse Device */
    ps2_wait_write();
    outb(0x64, 0xA8);

    /* Read PS/2 Controller Command Byte */
    ps2_wait_write();
    outb(0x64, 0x20);
    ps2_wait_read();
    uint8_t status = inb(0x60) | 0x02; /* Enable IRQ12 */

    /* Write PS/2 Controller Command Byte */
    ps2_wait_write();
    outb(0x64, 0x60);
    ps2_wait_write();
    outb(0x60, status);

    /* Send default settings command to mouse */
    ps2_wait_write();
    outb(0x64, 0xD4);
    ps2_wait_write();
    outb(0x60, 0xF6);
    ps2_wait_read();
    inb(0x60); /* ACK */

    /* Enable Mouse Streaming */
    ps2_wait_write();
    outb(0x64, 0xD4);
    ps2_wait_write();
    outb(0x60, 0xF4);
    ps2_wait_read();
    inb(0x60); /* ACK */
}

void ps2_keyboard_handler(void) {
    uint8_t scancode = inb(0x60);

    if (scancode == 0x1D) {
        ctrl_pressed = 1;
        return;
    } else if (scancode == 0x9D) {
        ctrl_pressed = 0;
        return;
    }

    if (!(scancode & 0x80)) { /* Key Press */
        /* F1 (0x3B), TAB (0x0F) or Ctrl+V (0x2F) toggles CLI mode */
        if (scancode == 0x3B || scancode == 0x0F || (ctrl_pressed && scancode == 0x2F)) {
            os_toggle_cli_mode();
            return;
        }

        char c = scancode_ascii[scancode];
        if (c) {
            os_handle_keypress(c);
        }
    }
}

void ps2_mouse_handler(void) {
    uint8_t data = inb(0x60);

    switch (mouse_cycle) {
        case 0:
            if (data & 0x08) { /* Byte 1 alignment check */
                mouse_packet[0] = (int8_t)data;
                mouse_cycle = 1;
            }
            break;
        case 1:
            mouse_packet[1] = (int8_t)data;
            mouse_cycle = 2;
            break;
        case 2:
            mouse_packet[2] = (int8_t)data;
            mouse_cycle = 0;

            mouse_state.buttons = mouse_packet[0] & 0x07;
            int rel_x = mouse_packet[1];
            int rel_y = mouse_packet[2];

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

            os_draw_mouse_cursor();
            break;
    }
}

kgfx_mouse_t *ps2_get_mouse_state(void) {
    return &mouse_state;
}
