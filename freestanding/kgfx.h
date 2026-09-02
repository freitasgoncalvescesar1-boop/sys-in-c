#ifndef KGFX_H
#define KGFX_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t *buffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
} kgfx_fb_t;

#define KGFX_BLACK     0xFF000000
#define KGFX_WHITE     0xFFFFFFFF
#define KGFX_RED       0xFFFF0000
#define KGFX_GREEN     0xFF00FF00
#define KGFX_BLUE      0xFF0000FF
#define KGFX_YELLOW    0xFFFFFF00
#define KGFX_CYAN      0xFF00FFFF
#define KGFX_MAGENTA   0xFFFF00FF
#define KGFX_DARKGRAY  0xFF313244
#define KGFX_NAVY      0xFF181825
#define KGFX_PURPLE    0xFFCBA6F7

static inline uint32_t kgfx_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

typedef struct {
    int x, y, w, h;
} kgfx_rect_t;

typedef struct {
    uint32_t *front_buffer;
    uint32_t *back_buffer;
    uint32_t width;
    uint32_t height;
} kgfx_double_buffer_t;

#define KGFX_MAX_DIRTY_RECTS 16

typedef struct {
    kgfx_rect_t rects[KGFX_MAX_DIRTY_RECTS];
    size_t count;
} kgfx_dirty_list_t;

typedef enum {
    KGFX_CURSOR_NORMAL = 0,
    KGFX_CURSOR_CLICKABLE,
    KGFX_CURSOR_TEXT
} kgfx_cursor_type_t;

typedef struct {
    int x, y;
    uint8_t buttons;
    kgfx_cursor_type_t type;
} kgfx_mouse_t;

extern const uint8_t kgfx_font8x8[95][8];

void kgfx_init(kgfx_fb_t *fb, uint32_t *buffer, uint32_t width, uint32_t height);
void kgfx_clear(kgfx_fb_t *fb, uint32_t color);

uint32_t kgfx_blend_colors(uint32_t bg, uint32_t fg);
void kgfx_draw_pixel_alpha(kgfx_fb_t *fb, int x, int y, uint32_t color);
void kgfx_draw_rect_alpha(kgfx_fb_t *fb, int x, int y, int w, int h, uint32_t color);

void kgfx_draw_pixel(kgfx_fb_t *fb, int x, int y, uint32_t color);
void kgfx_draw_line(kgfx_fb_t *fb, int x0, int y0, int x1, int y1, uint32_t color);
void kgfx_draw_rect(kgfx_fb_t *fb, int x, int y, int w, int h, uint32_t color, int fill);
void kgfx_draw_rounded_rect(kgfx_fb_t *fb, int x, int y, int w, int h, int r, uint32_t color, int fill);
void kgfx_draw_circle(kgfx_fb_t *fb, int cx, int cy, int r, uint32_t color);
void kgfx_draw_filled_circle(kgfx_fb_t *fb, int cx, int cy, int r, uint32_t color);
void kgfx_draw_triangle(kgfx_fb_t *fb, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color, int fill);

void kgfx_draw_char(kgfx_fb_t *fb, int x, int y, char c, uint32_t fg, uint32_t bg);
void kgfx_draw_string(kgfx_fb_t *fb, int x, int y, const char *str, uint32_t fg, uint32_t bg);
void kgfx_draw_char_scaled(kgfx_fb_t *fb, int x, int y, char c, uint32_t fg, uint32_t bg, int scale);
void kgfx_draw_string_scaled(kgfx_fb_t *fb, int x, int y, const char *str, uint32_t fg, uint32_t bg, int scale);

void kgfx_blit(kgfx_fb_t *dest, int dx, int dy, const uint32_t *src_buf, int sw, int sh, uint32_t chroma_key);

void kgfx_double_buffer_init(kgfx_double_buffer_t *db, uint32_t *front, uint32_t *back, uint32_t w, uint32_t h);
void kgfx_swap_buffers(kgfx_double_buffer_t *db);
void kgfx_dirty_clear(kgfx_dirty_list_t *list);
void kgfx_add_dirty_rect(kgfx_dirty_list_t *list, int x, int y, int w, int h);
void kgfx_flush_dirty(kgfx_double_buffer_t *db, kgfx_dirty_list_t *list);

void kgfx_draw_cursor(kgfx_fb_t *fb, const kgfx_mouse_t *mouse);
int kgfx_rect_contains(const kgfx_rect_t *rect, int x, int y);

typedef struct {
    uint8_t character;
    uint8_t attribute;
} __attribute__((packed)) kgfx_vga_cell_t;

void kgfx_vga3h_putc(int col, int row, char c, uint8_t fg, uint8_t bg);
void kgfx_vga3h_print(int col, int row, const char *str, uint8_t fg, uint8_t bg);
void kgfx_vga3h_clear(uint8_t bg);

#define COM1_PORT 0x3F8
void kgfx_serial_init(void);
void kgfx_serial_putc(char c);
void kgfx_serial_print(const char *str);

#endif
