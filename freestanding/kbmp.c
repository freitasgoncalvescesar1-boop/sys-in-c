#ifndef KBMP_C_INCLUDED
#define KBMP_C_INCLUDED

#include "kbmp.h"

int kbmp_render(kgfx_fb_t *fb, int dest_x, int dest_y, const uint8_t *bmp_data, size_t data_len) {
    if (!fb || !bmp_data || data_len < sizeof(kbmp_header_t)) return -1;

    const kbmp_header_t *hdr = (const kbmp_header_t *)bmp_data;
    if (hdr->type != 0x4D42) return -1; // "BM"

    int w = hdr->width;
    int h = hdr->height;
    int bpp = hdr->bpp;
    if (w <= 0 || h == 0 || (bpp != 24 && bpp != 32)) return -1;

    int is_bottom_up = 1;
    if (h < 0) {
        h = -h;
        is_bottom_up = 0;
    }

    const uint8_t *pixels = bmp_data + hdr->offset;
    int row_stride = (w * (bpp / 8) + 3) & ~3; // Alinhamento a 4 bytes

    for (int y = 0; y < h; y++) {
        int src_y = is_bottom_up ? (h - 1 - y) : y;
        const uint8_t *row = pixels + (src_y * row_stride);

        for (int x = 0; x < w; x++) {
            uint32_t color = 0;
            if (bpp == 24) {
                uint8_t b = row[x * 3 + 0];
                uint8_t g = row[x * 3 + 1];
                uint8_t r = row[x * 3 + 2];
                color = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            } else if (bpp == 32) {
                uint8_t b = row[x * 4 + 0];
                uint8_t g = row[x * 4 + 1];
                uint8_t r = row[x * 4 + 2];
                uint8_t a = row[x * 4 + 3];
                color = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
            kgfx_draw_pixel(fb, dest_x + x, dest_y + y, color);
        }
    }
    return 0;
}

#endif
