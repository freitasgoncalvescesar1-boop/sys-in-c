#ifndef KGFX_H
#define KGFX_H

#include <stddef.h>
#include <stdint.h>

/* Framebuffer descriptor */
typedef struct {
    uint32_t *buffer;   /* Pixel memory array (ARGB32) */
    uint32_t width;     /* Screen width in pixels */
    uint32_t height;    /* Screen height in pixels */
    uint32_t pitch;     /* Pixels per scanline */
} kgfx_fb_t;

/* Standard 32-bit ARGB color definitions */
#define KGFX_BLACK     0xFF000000
#define KGFX_WHITE     0xFFFFFFFF
#define KGFX_RED       0xFFFF0000
#define KGFX_GREEN     0xFF00FF00
#define KGFX_BLUE      0xFF0000FF
#define KGFX_YELLOW    0xFFFFFF00
#define KGFX_CYAN      0xFF00FFFF
#define KGFX_MAGENTA   0xFFFF00FF
#define KGFX_DARKGRAY  0xFF313244

/* Bounding box rectangle */
typedef struct {
    int x, y, w, h;
} kgfx_rect_t;

/* Double buffering descriptor */
typedef struct {
    uint32_t *front_buffer; /* Hardware video memory */
    uint32_t *back_buffer;  /* RAM render target */
    uint32_t width;         /* Screen width */
    uint32_t height;        /* Screen height */
} kgfx_double_buffer_t;

/* Dirty rectangle tracker */
#define KGFX_MAX_DIRTY_RECTS 16

typedef struct {
    kgfx_rect_t rects[KGFX_MAX_DIRTY_RECTS];
    size_t count;
} kgfx_dirty_list_t;

/* Cursor appearance type */
typedef enum {
    KGFX_CURSOR_NORMAL = 0,    /* Standard arrow pointer */
    KGFX_CURSOR_CLICKABLE,     /* Hand/pointer for interactive UI */
    KGFX_CURSOR_TEXT           /* Beam cursor for text input */
} kgfx_cursor_type_t;

/* Mouse input state */
typedef struct {
    int x, y;                  /* Screen coordinates */
    uint8_t buttons;           /* Bit 0: Left, Bit 1: Right, Bit 2: Middle */
    kgfx_cursor_type_t type;   /* Active cursor appearance */
} kgfx_mouse_t;

/* Framebuffer initialization */
void kgfx_init(kgfx_fb_t *fb, uint32_t *buffer, uint32_t width, uint32_t height);

/* Screen clearing */
void kgfx_clear(kgfx_fb_t *fb, uint32_t color);

/* Graphics primitives */
void kgfx_draw_pixel(kgfx_fb_t *fb, int x, int y, uint32_t color);
void kgfx_draw_line(kgfx_fb_t *fb, int x0, int y0, int x1, int y1, uint32_t color);
void kgfx_draw_rect(kgfx_fb_t *fb, int x, int y, int w, int h, uint32_t color, int fill);
void kgfx_draw_circle(kgfx_fb_t *fb, int cx, int cy, int r, uint32_t color);

/* Text rendering */
void kgfx_draw_char(kgfx_fb_t *fb, int x, int y, char c, uint32_t fg, uint32_t bg);
void kgfx_draw_string(kgfx_fb_t *fb, int x, int y, const char *str, uint32_t fg, uint32_t bg);

/* Double buffering API */
void kgfx_double_buffer_init(kgfx_double_buffer_t *db, uint32_t *front, uint32_t *back, uint32_t w, uint32_t h);
void kgfx_swap_buffers(kgfx_double_buffer_t *db);

/* Dirty rectangle API */
void kgfx_dirty_clear(kgfx_dirty_list_t *list);
void kgfx_add_dirty_rect(kgfx_dirty_list_t *list, int x, int y, int w, int h);
void kgfx_flush_dirty(kgfx_double_buffer_t *db, kgfx_dirty_list_t *list);

/* Mouse cursor & hit-test API */
void kgfx_draw_cursor(kgfx_fb_t *fb, const kgfx_mouse_t *mouse);
int kgfx_rect_contains(const kgfx_rect_t *rect, int x, int y);

/* --- VGA Mode 03h Driver (Text Mode 80x25 at 0xB8000) --- */
/*
void kgfx_vga3h_putc(int col, int row, char c, uint8_t fg, uint8_t bg);
void kgfx_vga3h_print(int col, int row, const char *str, uint8_t fg, uint8_t bg);
void kgfx_vga3h_clear(uint8_t bg);
*/

/* --- UART Serial Port Driver (COM1 0x3F8) --- */
/*
void kgfx_serial_init(void);
void kgfx_serial_putc(char c);
void kgfx_serial_print(const char *str);
*/

#endif
