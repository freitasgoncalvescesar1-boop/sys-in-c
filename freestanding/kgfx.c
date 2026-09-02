#include "kgfx.h"
#include "kmem.h"

#if defined(__x86_64__) || defined(__i386__)
static inline void kgfx_outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__ ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t kgfx_inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__ ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
#else
static inline void kgfx_outb(uint16_t port, uint8_t val) { (void)port; (void)val; }
static inline uint8_t kgfx_inb(uint16_t port) { (void)port; return 0; }
#endif

const uint8_t kgfx_font8x8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* Space */
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, /* ! */
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, /* # */
    {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00}, /* $ */
    {0x00,0x63,0x66,0x0C,0x18,0x33,0x63,0x00}, /* % */
    {0x1C,0x36,0x1C,0x3B,0x6E,0x66,0x3B,0x00}, /* & */
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, /* ( */
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, /* ) */
    {0x00,0x18,0x7E,0x3C,0x7E,0x18,0x00,0x00}, /* * */
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, /* , */
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, /* . */
    {0x00,0x03,0x06,0x0C,0x18,0x30,0x60,0x00}, /* / */
    {0x3C,0x66,0x6E,0x7E,0x76,0x66,0x3C,0x00}, /* 0 */
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, /* 1 */
    {0x3C,0x66,0x0C,0x18,0x30,0x60,0x7E,0x00}, /* 2 */
    {0x3C,0x66,0x0C,0x1C,0x0C,0x66,0x3C,0x00}, /* 3 */
    {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00}, /* 4 */
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}, /* 5 */
    {0x3C,0x66,0x60,0x7C,0x66,0x66,0x3C,0x00}, /* 6 */
    {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00}, /* 7 */
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, /* 8 */
    {0x3C,0x66,0x66,0x3E,0x06,0x66,0x3C,0x00}, /* 9 */
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}, /* : */
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30}, /* ; */
    {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00}, /* < */
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}, /* = */
    {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00}, /* > */
    {0x3C,0x66,0x0C,0x18,0x18,0x00,0x18,0x00}, /* ? */
    {0x3C,0x66,0x6E,0x6E,0x60,0x3E,0x00,0x00}, /* @ */
    {0x3C,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, /* A */
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}, /* B */
    {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00}, /* C */
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}, /* D */
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00}, /* E */
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00}, /* F */
    {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00}, /* G */
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, /* H */
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, /* I */
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00}, /* J */
    {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00}, /* K */
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00}, /* L */
    {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00}, /* M */
    {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00}, /* N */
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, /* O */
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}, /* P */
    {0x3C,0x66,0x66,0x66,0x6E,0x3C,0x0E,0x00}, /* Q */
    {0x7C,0x66,0x66,0x7C,0x70,0x68,0x66,0x00}, /* R */
    {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00}, /* S */
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, /* T */
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, /* U */
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00}, /* V */
    {0x63,0x63,0x63,0x6B,0x7F,0x3E,0x36,0x00}, /* W */
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00}, /* X */
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00}, /* Y */
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00}, /* Z */
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, /* [ */
    {0x00,0x60,0x30,0x18,0x0C,0x06,0x03,0x00}, /* \ */
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, /* ] */
    {0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, /* _ */
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00}, /* a */
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00}, /* b */
    {0x00,0x00,0x3C,0x60,0x60,0x66,0x3C,0x00}, /* c */
    {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00}, /* d */
    {0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00}, /* e */
    {0x1C,0x30,0x7C,0x30,0x30,0x30,0x30,0x00}, /* f */
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x7C}, /* g */
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00}, /* h */
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, /* i */
    {0x06,0x00,0x0E,0x06,0x06,0x66,0x3C,0x00}, /* j */
    {0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00}, /* k */
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, /* l */
    {0x00,0x00,0x66,0x7F,0x7F,0x6B,0x63,0x00}, /* m */
    {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00}, /* n */
    {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00}, /* o */
    {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60}, /* p */
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06}, /* q */
    {0x00,0x00,0x7C,0x66,0x60,0x60,0x60,0x00}, /* r */
    {0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00}, /* s */
    {0x18,0x18,0x7E,0x18,0x18,0x18,0x0E,0x00}, /* t */
    {0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00}, /* u */
    {0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00}, /* v */
    {0x00,0x00,0x63,0x6B,0x7F,0x3E,0x36,0x00}, /* w */
    {0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00}, /* x */
    {0x00,0x00,0x66,0x66,0x66,0x3E,0x0C,0x78}, /* y */
    {0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00}, /* z */
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00}, /* { */
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, /* | */
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00}, /* } */
    {0x3B,0x6E,0x00,0x00,0x00,0x00,0x00,0x00}  /* ~ */
};

static const uint16_t cursor_arrow[14] = {
    0b100000000000, 0b110000000000, 0b111000000000, 0b111100000000,
    0b111110000000, 0b111111000000, 0b111111100000, 0b111111110000,
    0b111110000000, 0b110111000000, 0b100011100000, 0b000001110000,
    0b000000110000, 0b000000000000
};

static const uint16_t cursor_hand[14] = {
    0b001100000000, 0b001100000000, 0b001100000000, 0b001101100000,
    0b001101101100, 0b011101101101, 0b011111111111, 0b011111111111,
    0b001111111111, 0b000111111110, 0b000011111100, 0b000001111100,
    0b000000000000, 0b000000000000
};

static const uint16_t cursor_beam[14] = {
    0b011111000000, 0b000100000000, 0b000100000000, 0b000100000000,
    0b000100000000, 0b000100000000, 0b000100000000, 0b000100000000,
    0b000100000000, 0b000100000000, 0b000100000000, 0b000100000000,
    0b011111000000, 0b000000000000
};

void kgfx_init(kgfx_fb_t *fb, uint32_t *buffer, uint32_t width, uint32_t height) {
    if (!fb || !buffer) return;
    fb->buffer = buffer;
    fb->width = width;
    fb->height = height;
    fb->pitch = width;
}

void kgfx_clear(kgfx_fb_t *fb, uint32_t color) {
    if (!fb || !fb->buffer) return;
    size_t total_pixels = (size_t)fb->width * fb->height;
    for (size_t i = 0; i < total_pixels; i++) {
        fb->buffer[i] = color;
    }
}

uint32_t kgfx_blend_colors(uint32_t bg, uint32_t fg) {
    uint8_t a = (fg >> 24) & 0xFF;
    if (a == 255) return fg;
    if (a == 0) return bg;

    uint8_t fg_r = (fg >> 16) & 0xFF, fg_g = (fg >> 8) & 0xFF, fg_b = fg & 0xFF;
    uint8_t bg_r = (bg >> 16) & 0xFF, bg_g = (bg >> 8) & 0xFF, bg_b = bg & 0xFF;

    uint8_t out_r = (uint8_t)((fg_r * a + bg_r * (255 - a)) / 255);
    uint8_t out_g = (uint8_t)((fg_g * a + bg_g * (255 - a)) / 255);
    uint8_t out_b = (uint8_t)((fg_b * a + bg_b * (255 - a)) / 255);

    return (0xFF000000) | ((uint32_t)out_r << 16) | ((uint32_t)out_g << 8) | out_b;
}

void kgfx_draw_pixel(kgfx_fb_t *fb, int x, int y, uint32_t color) {
    if (!fb || !fb->buffer) return;
    if (x < 0 || (uint32_t)x >= fb->width || y < 0 || (uint32_t)y >= fb->height) return;
    fb->buffer[y * fb->pitch + x] = color;
}

void kgfx_draw_pixel_alpha(kgfx_fb_t *fb, int x, int y, uint32_t color) {
    if (!fb || !fb->buffer) return;
    if (x < 0 || (uint32_t)x >= fb->width || y < 0 || (uint32_t)y >= fb->height) return;
    size_t idx = y * fb->pitch + x;
    fb->buffer[idx] = kgfx_blend_colors(fb->buffer[idx], color);
}

void kgfx_draw_line(kgfx_fb_t *fb, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = (y1 > y0) ? (y0 - y1) : (y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (1) {
        kgfx_draw_pixel(fb, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void kgfx_draw_rect(kgfx_fb_t *fb, int x, int y, int w, int h, uint32_t color, int fill) {
    if (w <= 0 || h <= 0) return;
    if (fill) {
        for (int py = y; py < y + h; py++) {
            if (py < 0 || (uint32_t)py >= fb->height) continue;
            uint32_t *row = &fb->buffer[py * fb->pitch + x];
            for (int px = 0; px < w; px++) {
                if (x + px >= 0 && (uint32_t)(x + px) < fb->width) {
                    row[px] = color;
                }
            }
        }
    } else {
        kgfx_draw_line(fb, x, y, x + w - 1, y, color);
        kgfx_draw_line(fb, x, y + h - 1, x + w - 1, y + h - 1, color);
        kgfx_draw_line(fb, x, y, x, y + h - 1, color);
        kgfx_draw_line(fb, x + w - 1, y, x + w - 1, y + h - 1, color);
    }
}

void kgfx_draw_rect_alpha(kgfx_fb_t *fb, int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            kgfx_draw_pixel_alpha(fb, px, py, color);
        }
    }
}

void kgfx_draw_circle(kgfx_fb_t *fb, int cx, int cy, int r, uint32_t color) {
    int x = 0, y = r;
    int d = 3 - 2 * r;
    while (y >= x) {
        kgfx_draw_pixel(fb, cx + x, cy + y, color);
        kgfx_draw_pixel(fb, cx - x, cy + y, color);
        kgfx_draw_pixel(fb, cx + x, cy - y, color);
        kgfx_draw_pixel(fb, cx - x, cy - y, color);
        kgfx_draw_pixel(fb, cx + y, cy + x, color);
        kgfx_draw_pixel(fb, cx - y, cy + x, color);
        kgfx_draw_pixel(fb, cx + y, cy - x, color);
        kgfx_draw_pixel(fb, cx - y, cy - x, color);
        x++;
        if (d > 0) { y--; d = d + 4 * (x - y) + 10; }
        else { d = d + 4 * x + 6; }
    }
}

void kgfx_draw_filled_circle(kgfx_fb_t *fb, int cx, int cy, int r, uint32_t color) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                kgfx_draw_pixel(fb, cx + x, cy + y, color);
            }
        }
    }
}

void kgfx_draw_rounded_rect(kgfx_fb_t *fb, int x, int y, int w, int h, int r, uint32_t color, int fill) {
    if (r <= 0) { kgfx_draw_rect(fb, x, y, w, h, color, fill); return; }
    if (fill) {
        kgfx_draw_rect(fb, x + r, y, w - 2 * r, h, color, 1);
        kgfx_draw_rect(fb, x, y + r, r, h - 2 * r, color, 1);
        kgfx_draw_rect(fb, x + w - r, y + r, r, h - 2 * r, color, 1);
        kgfx_draw_filled_circle(fb, x + r, y + r, r, color);
        kgfx_draw_filled_circle(fb, x + w - r - 1, y + r, r, color);
        kgfx_draw_filled_circle(fb, x + r, y + h - r - 1, r, color);
        kgfx_draw_filled_circle(fb, x + w - r - 1, y + h - r - 1, r, color);
    } else {
        kgfx_draw_line(fb, x + r, y, x + w - r, y, color);
        kgfx_draw_line(fb, x + r, y + h - 1, x + w - r, y + h - 1, color);
        kgfx_draw_line(fb, x, y + r, x, y + h - r, color);
        kgfx_draw_line(fb, x + w - 1, y + r, x + w - 1, y + h - r, color);
    }
}

void kgfx_draw_triangle(kgfx_fb_t *fb, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color, int fill) {
    if (!fill) {
        kgfx_draw_line(fb, x0, y0, x1, y1, color);
        kgfx_draw_line(fb, x1, y1, x2, y2, color);
        kgfx_draw_line(fb, x2, y2, x0, y0, color);
        return;
    }
    int min_x = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int max_x = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int min_y = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int max_y = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int w0 = (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0);
            int w1 = (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1);
            int w2 = (x0 - x2) * (y - y2) - (y0 - y2) * (x - x2);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                kgfx_draw_pixel(fb, x, y, color);
            }
        }
    }
}

void kgfx_draw_char(kgfx_fb_t *fb, int x, int y, char c, uint32_t fg, uint32_t bg) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = kgfx_font8x8[c - 32];
    size_t pitch = fb->pitch;

    for (int row = 0; row < 8; row++) {
        int py = y + row;
        if (py < 0 || (uint32_t)py >= fb->height) continue;
        uint32_t *line = &fb->buffer[py * pitch + x];
        uint8_t bits = glyph[row];

        for (int col = 0; col < 8; col++) {
            int px = x + col;
            if (px < 0 || (uint32_t)px >= fb->width) continue;
            if (bits & (1 << (7 - col))) {
                line[col] = fg;
            } else if (bg != 0) {
                line[col] = bg;
            }
        }
    }
}

void kgfx_draw_char_scaled(kgfx_fb_t *fb, int x, int y, char c, uint32_t fg, uint32_t bg, int scale) {
    if (scale <= 1) {
        kgfx_draw_char(fb, x, y, c, fg, bg);
        return;
    }
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = kgfx_font8x8[c - 32];

    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            uint32_t col_val = (bits & (1 << (7 - col))) ? fg : bg;
            if (col_val != 0) {
                kgfx_draw_rect(fb, x + col * scale, y + row * scale, scale, scale, col_val, 1);
            }
        }
    }
}

void kgfx_draw_string(kgfx_fb_t *fb, int x, int y, const char *str, uint32_t fg, uint32_t bg) {
    if (!str) return;
    int curr_x = x, curr_y = y;

    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            curr_x = x;
            curr_y += 14;
        } else {
            kgfx_draw_char(fb, curr_x, curr_y, str[i], fg, bg);
            curr_x += 8;
        }
    }
}

void kgfx_draw_string_scaled(kgfx_fb_t *fb, int x, int y, const char *str, uint32_t fg, uint32_t bg, int scale) {
    if (scale <= 1) {
        kgfx_draw_string(fb, x, y, str, fg, bg);
        return;
    }
    if (!str) return;
    int curr_x = x, curr_y = y;

    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            curr_x = x;
            curr_y += 14 * scale;
        } else {
            kgfx_draw_char_scaled(fb, curr_x, curr_y, str[i], fg, bg, scale);
            curr_x += 8 * scale;
        }
    }
}

void kgfx_blit(kgfx_fb_t *dest, int dx, int dy, const uint32_t *src_buf, int sw, int sh, uint32_t chroma_key) {
    if (!dest || !src_buf || sw <= 0 || sh <= 0) return;
    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            uint32_t pixel = src_buf[y * sw + x];
            if (pixel != chroma_key) {
                kgfx_draw_pixel(dest, dx + x, dy + y, pixel);
            }
        }
    }
}

void kgfx_double_buffer_init(kgfx_double_buffer_t *db, uint32_t *front, uint32_t *back, uint32_t w, uint32_t h) {
    if (!db) return;
    db->front_buffer = front;
    db->back_buffer = back;
    db->width = w;
    db->height = h;
}

void kgfx_swap_buffers(kgfx_double_buffer_t *db) {
    if (!db || !db->front_buffer || !db->back_buffer) return;
    size_t total_bytes = (size_t)db->width * db->height * sizeof(uint32_t);
    kmemcpy(db->front_buffer, db->back_buffer, total_bytes);
}

void kgfx_dirty_clear(kgfx_dirty_list_t *list) {
    if (list) list->count = 0;
}

void kgfx_add_dirty_rect(kgfx_dirty_list_t *list, int x, int y, int w, int h) {
    if (!list || list->count >= KGFX_MAX_DIRTY_RECTS) return;
    list->rects[list->count].x = x;
    list->rects[list->count].y = y;
    list->rects[list->count].w = w;
    list->rects[list->count].h = h;
    list->count++;
}

void kgfx_flush_dirty(kgfx_double_buffer_t *db, kgfx_dirty_list_t *list) {
    if (!db || !db->front_buffer || !db->back_buffer || !list) return;

    for (size_t i = 0; i < list->count; i++) {
        kgfx_rect_t r = list->rects[i];
        if (r.x < 0) r.x = 0;
        if (r.y < 0) r.y = 0;
        if ((uint32_t)(r.x + r.w) > db->width) r.w = db->width - r.x;
        if ((uint32_t)(r.y + r.h) > db->height) r.h = db->height - r.y;

        for (int py = r.y; py < r.y + r.h; py++) {
            size_t offset = (size_t)py * db->width + r.x;
            kmemcpy(&db->front_buffer[offset], &db->back_buffer[offset], r.w * sizeof(uint32_t));
        }
    }
    list->count = 0;
}

void kgfx_draw_cursor(kgfx_fb_t *fb, const kgfx_mouse_t *mouse) {
    if (!fb || !mouse) return;

    const uint16_t *sprite = cursor_arrow;
    if (mouse->type == KGFX_CURSOR_CLICKABLE) sprite = cursor_hand;
    else if (mouse->type == KGFX_CURSOR_TEXT) sprite = cursor_beam;

    for (int row = 0; row < 14; row++) {
        uint16_t bits = sprite[row];
        for (int col = 0; col < 12; col++) {
            if (bits & (1 << (11 - col))) {
                kgfx_draw_pixel(fb, mouse->x + col, mouse->y + row, KGFX_WHITE);
            }
        }
    }
}

int kgfx_rect_contains(const kgfx_rect_t *rect, int x, int y) {
    if (!rect) return 0;
    return (x >= rect->x && x < (rect->x + rect->w) && y >= rect->y && y < (rect->y + rect->h));
}

static volatile kgfx_vga_cell_t *vga3h_mem = (volatile kgfx_vga_cell_t *)0xB8000;

void kgfx_vga3h_putc(int col, int row, char c, uint8_t fg, uint8_t bg) {
    if (col < 0 || col >= 80 || row < 0 || row >= 25) return;
    uint8_t attr = (uint8_t)((bg << 4) | (fg & 0x0F));
    int idx = row * 80 + col;
    vga3h_mem[idx].character = (uint8_t)c;
    vga3h_mem[idx].attribute = attr;
}

void kgfx_vga3h_print(int col, int row, const char *str, uint8_t fg, uint8_t bg) {
    while (*str) {
        kgfx_vga3h_putc(col++, row, *str++, fg, bg);
        if (col >= 80) {
            col = 0;
            row++;
        }
    }
}

void kgfx_vga3h_clear(uint8_t bg) {
    for (int r = 0; r < 25; r++) {
        for (int c = 0; c < 80; c++) {
            kgfx_vga3h_putc(c, r, ' ', 0x0F, bg);
        }
    }
}

void kgfx_serial_init(void) {
    kgfx_outb(COM1_PORT + 1, 0x00);
    kgfx_outb(COM1_PORT + 3, 0x80);
    kgfx_outb(COM1_PORT + 0, 0x03);
    kgfx_outb(COM1_PORT + 1, 0x00);
    kgfx_outb(COM1_PORT + 3, 0x03);
    kgfx_outb(COM1_PORT + 2, 0xC7);
    kgfx_outb(COM1_PORT + 4, 0x0B);
}

void kgfx_serial_putc(char c) {
    while ((kgfx_inb(COM1_PORT + 5) & 0x20) == 0);
    kgfx_outb(COM1_PORT, (uint8_t)c);
}

void kgfx_serial_print(const char *str) {
    while (*str) {
        kgfx_serial_putc(*str++);
    }
}
