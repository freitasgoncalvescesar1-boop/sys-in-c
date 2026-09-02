#ifndef KBMP_H
#define KBMP_H

#include <stdint.h>
#include <stddef.h>
#include "kgfx.h"

#pragma pack(push, 1)
typedef struct {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
    uint32_t header_size;
    int32_t  width;
    int32_t  height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t image_size;
    int32_t  x_ppm;
    int32_t  y_ppm;
    uint32_t colors_used;
    uint32_t colors_important;
} kbmp_header_t;
#pragma pack(pop)

int kbmp_render(kgfx_fb_t *fb, int dest_x, int dest_y, const uint8_t *bmp_data, size_t data_len);

#endif
