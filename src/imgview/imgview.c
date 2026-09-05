#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_TAG     "\033[1;33m"
#define COLOR_VAL     "\033[1;36m"
#define COLOR_LABEL   "\033[1;37m"
#define COLOR_MUTED   "\033[0;90m"

#pragma pack(push, 1)
typedef struct {
    uint16_t type;             // "BM" (0x4D42)
    uint32_t file_size;
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
} BMPHeader;
#pragma pack(pop)

typedef struct {
    uint8_t r, g, b, a;
} RGBColor;

typedef struct {
    int width;
    int height;
    int bpp;
    const char *format;
    RGBColor *pixels;
} DecodedImage;

static void print_help(void) {
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ imgview 2.0 - Terminal TrueColor Image Viewer (BMP & JPG/JPEG Engine) ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  imgview <ARQUIVO.bmp | ARQUIVO.jpg> [OPTIONS]\n");
    printf("  imgview --demo                               (Renderiza paleta teste em memoria)\n");
    printf("  imgview --save-demo <arquivo.bmp>            (Cria imagem BMP 24-bit no disco)\n\n");
    printf("Options:\n");
    printf("  %s-i, --info%s            Exibir apenas metadados tecnicos do cabecalho\n", COLOR_OK, COLOR_RESET);
    printf("  %s-w, --width <N>%s       Forcar largura maxima em colunas do terminal\n", COLOR_OK, COLOR_RESET);
    printf("  %s-g, --gray%s            Renderizar imagem filtrada em escala de cinza\n", COLOR_OK, COLOR_RESET);
    printf("  %s-a, --ascii%s           Renderizar em arte ASCII monocromatica classica\n", COLOR_OK, COLOR_RESET);
    printf("  %s-z, --zoom%s            Permitir ampliacao (upscale) acima de 100%%\n", COLOR_OK, COLOR_RESET);
    printf("  %s-h, --help%s            Exibir este guia formatado\n\n", COLOR_OK, COLOR_RESET);
    printf("Exemplos:\n");
    printf("  • %simgview foto.jpg%s\n", COLOR_TAG, COLOR_RESET);
    printf("  • %simgview imagem.bmp -w 60%s\n", COLOR_TAG, COLOR_RESET);
    printf("  • %simgview --demo%s\n", COLOR_TAG, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

static void get_terminal_dimensions(int *cols, int *rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    } else {
        *cols = 80;
        *rows = 24;
    }
}

// =========================================================================
// MOTOR DECODIFICADOR BMP (24-bit, 32-bit e 8-bit com Paleta)
// =========================================================================
static DecodedImage *load_bmp_memory(const uint8_t *raw, size_t fsize) {
    if (fsize < sizeof(BMPHeader)) return NULL;

    // Assinatura 'B' (0x42) e 'M' (0x4D)
    if (raw[0] != 0x42 || raw[1] != 0x4D) return NULL;

    const BMPHeader *hdr = (const BMPHeader *)raw;
    int w = hdr->width;
    int h = hdr->height;
    int bpp = hdr->bpp;

    if (w <= 0 || h == 0 || (bpp != 8 && bpp != 24 && bpp != 32)) return NULL;

    int is_bottom_up = 1;
    if (h < 0) {
        h = -h;
        is_bottom_up = 0;
    }

    DecodedImage *img = malloc(sizeof(DecodedImage));
    if (!img) return NULL;
    img->width = w;
    img->height = h;
    img->bpp = bpp;
    img->format = "BMP";
    img->pixels = malloc((size_t)w * h * sizeof(RGBColor));
    if (!img->pixels) { free(img); return NULL; }

    const uint8_t *palette = NULL;
    if (bpp == 8) {
        palette = raw + 14 + hdr->header_size;
    }

    const uint8_t *pixel_data = raw + hdr->offset;
    int row_stride = (w * (bpp / 8) + 3) & ~3;

    for (int y = 0; y < h; y++) {
        int src_y = is_bottom_up ? (h - 1 - y) : y;
        const uint8_t *row = pixel_data + (src_y * row_stride);

        for (int x = 0; x < w; x++) {
            RGBColor col = {0, 0, 0, 255};

            if (bpp == 24) {
                col.b = row[x * 3 + 0];
                col.g = row[x * 3 + 1];
                col.r = row[x * 3 + 2];
            } else if (bpp == 32) {
                col.b = row[x * 4 + 0];
                col.g = row[x * 4 + 1];
                col.r = row[x * 4 + 2];
                col.a = row[x * 4 + 3];
            } else if (bpp == 8 && palette) {
                uint8_t idx = row[x];
                col.b = palette[idx * 4 + 0];
                col.g = palette[idx * 4 + 1];
                col.r = palette[idx * 4 + 2];
            }
            img->pixels[y * w + x] = col;
        }
    }
    return img;
}

// =========================================================================
// MOTOR DECODIFICADOR JPEG COMPLETO (NanoJPEG Subsampling 4:2:0, 4:2:2, 4:4:4)
// =========================================================================
typedef struct _nj_code {
    uint8_t bits;
    uint8_t code;
} nj_vlc_t;

typedef struct _nj_cmp {
    int cid;
    int ssx, ssy;
    int width, height;
    int stride;
    int qtsel;
    int actabsel, dctabsel;
    int dcpred;
    uint8_t *pixels;
} nj_component_t;

typedef struct _nj_ctx {
    const uint8_t *pos;
    int size;
    int length;
    int width, height;
    int mbwidth, mbheight;
    int mbsizex, mbsizey;
    int ncomp;
    nj_component_t comp[3];
    int qtused, qtavail;
    uint8_t qtab[4][64];
    nj_vlc_t vlctab[4][65536];
    int buf, bufbits;
    int block[64];
    int rstinterval;
    uint8_t *rgb;
} nj_context_t;

static const char njZZ[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

static inline uint8_t njClip(int x) {
    return (x < 0) ? 0 : ((x > 255) ? 255 : (uint8_t)x);
}

static inline int njShowBits(nj_context_t* ctx, int bits) {
    if (!bits) return 0;
    while (ctx->bufbits < bits) {
        if (ctx->size <= 0) {
            ctx->buf = (ctx->buf << 8) | 0xFF;
            ctx->bufbits += 8;
            continue;
        }
        uint8_t newbyte = *ctx->pos++;
        ctx->size--;
        ctx->bufbits += 8;
        ctx->buf = (ctx->buf << 8) | newbyte;
        if (newbyte == 0xFF) {
            if (ctx->size > 0) {
                uint8_t marker = *ctx->pos++;
                ctx->size--;
                if (marker != 0) {
                    // Marker
                }
            }
        }
    }
    return (ctx->buf >> (ctx->bufbits - bits)) & ((1 << bits) - 1);
}

static inline void njSkipBits(nj_context_t* ctx, int bits) {
    if (ctx->bufbits < bits) (void)njShowBits(ctx, bits);
    ctx->bufbits -= bits;
}

static inline int njGetBits(nj_context_t* ctx, int bits) {
    int res = njShowBits(ctx, bits);
    njSkipBits(ctx, bits);
    return res;
}

static inline int njGetVLC(nj_context_t* ctx, nj_vlc_t* tab, uint8_t* code) {
    int value = njShowBits(ctx, 16);
    int bits = tab[value].bits;
    if (!bits) return 0;
    njSkipBits(ctx, bits);
    value = tab[value].code;
    if (code) *code = (uint8_t)value;
    bits = value & 15;
    if (!bits) return 0;
    value = njGetBits(ctx, bits);
    if (value < (1 << (bits - 1)))
        value += ((-1) << bits) + 1;
    return value;
}

static void njRowIDCT(int* blk) {
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = blk[4] << 11) | (x2 = blk[6]) | (x3 = blk[2]) |
          (x4 = blk[1]) | (x5 = blk[7]) | (x6 = blk[5]) | (x7 = blk[3]))) {
        blk[0] = blk[1] = blk[2] = blk[3] = blk[4] = blk[5] = blk[6] = blk[7] = blk[0] << 3;
        return;
    }
    x0 = (blk[0] << 11) + 128;
    x8 = 565 * (x4 + x5);
    x4 = x8 + 2276 * x4;
    x5 = x8 - 3406 * x5;
    x8 = 2408 * (x6 + x7);
    x6 = x8 - 799 * x6;
    x7 = x8 - 4017 * x7;
    x8 = x0 + x1;
    x0 -= x1;
    x1 = 1108 * (x3 + x2);
    x2 = x1 - 3784 * x2;
    x3 = x1 + 1567 * x3;
    x1 = x4 + x6;
    x4 -= x6;
    x6 = x5 + x7;
    x5 -= x7;
    x7 = x8 + x3;
    x8 -= x3;
    x3 = x0 + x2;
    x0 -= x2;
    x2 = (181 * (x4 + x5) + 128) >> 8;
    x4 = (181 * (x4 - x5) + 128) >> 8;
    blk[0] = (x7 + x1) >> 8;
    blk[1] = (x3 + x2) >> 8;
    blk[2] = (x0 + x4) >> 8;
    blk[3] = (x8 + x6) >> 8;
    blk[4] = (x8 - x6) >> 8;
    blk[5] = (x0 - x4) >> 8;
    blk[6] = (x3 - x2) >> 8;
    blk[7] = (x7 - x1) >> 8;
}

static void njColIDCT(const int* blk, uint8_t *out, int stride) {
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = blk[8*4] << 8) | (x2 = blk[8*6]) | (x3 = blk[8*2]) |
          (x4 = blk[8*1]) | (x5 = blk[8*7]) | (x6 = blk[8*5]) | (x7 = blk[8*3]))) {
        x1 = njClip(((blk[0] + 32) >> 6) + 128);
        for (x0 = 8; x0; --x0) {
            *out = (uint8_t)x1;
            out += stride;
        }
        return;
    }
    x0 = (blk[0] << 8) + 8192;
    x8 = 565 * (x4 + x5);
    x4 = x8 + 2276 * x4;
    x5 = x8 - 3406 * x5;
    x8 = 2408 * (x6 + x7);
    x6 = x8 - 799 * x6;
    x7 = x8 - 4017 * x7;
    x8 = x0 + x1;
    x0 -= x1;
    x1 = 1108 * (x3 + x2);
    x2 = x1 - 3784 * x2;
    x3 = x1 + 1567 * x3;
    x1 = x4 + x6;
    x4 -= x6;
    x6 = x5 + x7;
    x5 -= x7;
    x7 = x8 + x3;
    x8 -= x3;
    x3 = x0 + x2;
    x0 -= x2;
    x2 = (181 * (x4 + x5) + 128) >> 8;
    x4 = (181 * (x4 - x5) + 128) >> 8;
    *out = njClip(((x7 + x1) >> 14) + 128); out += stride;
    *out = njClip(((x3 + x2) >> 14) + 128); out += stride;
    *out = njClip(((x0 + x4) >> 14) + 128); out += stride;
    *out = njClip(((x8 + x6) >> 14) + 128); out += stride;
    *out = njClip(((x8 - x6) >> 14) + 128); out += stride;
    *out = njClip(((x0 - x4) >> 14) + 128); out += stride;
    *out = njClip(((x3 - x2) >> 14) + 128); out += stride;
    *out = njClip(((x7 - x1) >> 14) + 128);
}

static void njDecodeBlock(nj_context_t* ctx, nj_component_t* c, uint8_t* out) {
    uint8_t code = 0;
    int coef = 0;
    memset(ctx->block, 0, sizeof(ctx->block));
    c->dcpred += njGetVLC(ctx, &ctx->vlctab[c->dctabsel][0], NULL);
    ctx->block[0] = c->dcpred * ctx->qtab[c->qtsel][0];
    do {
        int value = njGetVLC(ctx, &ctx->vlctab[c->actabsel][0], &code);
        if (!code) break;
        if (!(code & 0x0F) && (code != 0xF0)) break;
        coef += (code >> 4) + 1;
        if (coef > 63) break;
        ctx->block[(int)njZZ[coef]] = value * ctx->qtab[c->qtsel][coef];
    } while (coef < 63);

    for (coef = 0; coef < 64; coef += 8)
        njRowIDCT(&ctx->block[coef]);
    for (coef = 0; coef < 8; ++coef)
        njColIDCT(&ctx->block[coef], &out[coef], c->stride);
}

static int njBuildHuff(nj_context_t* ctx, int idx, const uint8_t* counts, const uint8_t* values) {
    int i, j, k = 0, code = 0;
    nj_vlc_t* tab = &ctx->vlctab[idx][0];
    memset(tab, 0, 65536 * sizeof(nj_vlc_t));
    for (i = 1; i <= 16; ++i) {
        for (j = 0; j < counts[i - 1]; ++j) {
            int n = 16 - i;
            int c = code << n;
            int max = c + (1 << n);
            while (c < max) {
                tab[c].bits = (uint8_t)i;
                tab[c].code = values[k];
                c++;
            }
            k++;
            code++;
        }
        code <<= 1;
    }
    return 0;
}

static DecodedImage *load_jpeg_memory(const uint8_t *raw, size_t fsize) {
    if (fsize < 4 || raw[0] != 0xFF || raw[1] != 0xD8) return NULL;

    nj_context_t *ctx = calloc(1, sizeof(nj_context_t));
    if (!ctx) return NULL;

    ctx->pos = raw;
    ctx->size = fsize;

    // Parser dos Marcadores JPEG
    while (ctx->size > 0) {
        if (*ctx->pos++ != 0xFF) { ctx->size--; continue; }
        ctx->size--;
        uint8_t marker = *ctx->pos++;
        ctx->size--;
        while (marker == 0xFF && ctx->size > 0) { marker = *ctx->pos++; ctx->size--; }

        if (marker == 0xD9) break; // EOI

        if (marker == 0xC0 || marker == 0xC2) { // SOF0 / SOF2
            if (ctx->size < 6) break;
            int len = (ctx->pos[0] << 8) | ctx->pos[1];
            ctx->height = (ctx->pos[3] << 8) | ctx->pos[4];
            ctx->width = (ctx->pos[5] << 8) | ctx->pos[6];
            ctx->ncomp = ctx->pos[7];
            ctx->pos += 8; ctx->size -= 8;

            int max_ssx = 0, max_ssy = 0;
            for (int i = 0; i < ctx->ncomp; ++i) {
                ctx->comp[i].cid = ctx->pos[0];
                ctx->comp[i].ssx = ctx->pos[1] >> 4;
                ctx->comp[i].ssy = ctx->pos[1] & 15;
                ctx->comp[i].qtsel = ctx->pos[2];
                if (ctx->comp[i].ssx > max_ssx) max_ssx = ctx->comp[i].ssx;
                if (ctx->comp[i].ssy > max_ssy) max_ssy = ctx->comp[i].ssy;
                ctx->pos += 3; ctx->size -= 3;
            }
            ctx->mbsizex = max_ssx << 3;
            ctx->mbsizey = max_ssy << 3;
            ctx->mbwidth = (ctx->width + ctx->mbsizex - 1) / ctx->mbsizex;
            ctx->mbheight = (ctx->height + ctx->mbsizey - 1) / ctx->mbsizey;

            for (int i = 0; i < ctx->ncomp; ++i) {
                ctx->comp[i].width = (ctx->width * ctx->comp[i].ssx + max_ssx - 1) / max_ssx;
                ctx->comp[i].height = (ctx->height * ctx->comp[i].ssy + max_ssy - 1) / max_ssy;
                ctx->comp[i].stride = ctx->mbwidth * ctx->comp[i].ssx << 3;
                ctx->comp[i].pixels = malloc(ctx->comp[i].stride * (ctx->mbheight * ctx->comp[i].ssy << 3));
            }
            ctx->pos += (len - 8 - ctx->ncomp * 3);
            ctx->size -= (len - 8 - ctx->ncomp * 3);
            continue;
        }

        if (marker == 0xDB) { // DQT
            int len = (ctx->pos[0] << 8) | ctx->pos[1];
            ctx->pos += 2; ctx->size -= 2;
            len -= 2;
            while (len > 0) {
                int qid = *ctx->pos++; ctx->size--; len--;
                int idx = qid & 15;
                for (int i = 0; i < 64; ++i) {
                    ctx->qtab[idx][i] = *ctx->pos++; ctx->size--; len--;
                }
            }
            continue;
        }

        if (marker == 0xC4) { // DHT
            int len = (ctx->pos[0] << 8) | ctx->pos[1];
            ctx->pos += 2; ctx->size -= 2;
            len -= 2;
            while (len > 0) {
                int hid = *ctx->pos++; ctx->size--; len--;
                int is_ac = (hid >> 4) & 1;
                int idx = (hid & 15) | (is_ac ? 2 : 0);
                uint8_t counts[16];
                int total = 0;
                for (int i = 0; i < 16; ++i) {
                    counts[i] = *ctx->pos++; ctx->size--; len--;
                    total += counts[i];
                }
                uint8_t values[256];
                for (int i = 0; i < total; ++i) {
                    values[i] = *ctx->pos++; ctx->size--; len--;
                }
                njBuildHuff(ctx, idx, counts, values);
            }
            continue;
        }

        if (marker == 0xDA) { // SOS (Start of Scan)
            int len = (ctx->pos[0] << 8) | ctx->pos[1];
            ctx->pos += 2; ctx->size -= 2;
            int ncomp = *ctx->pos++; ctx->size--;
            for (int i = 0; i < ncomp; ++i) {
                int cid = *ctx->pos++; ctx->size--;
                int sel = *ctx->pos++; ctx->size--;
                for (int j = 0; j < ctx->ncomp; ++j) {
                    if (ctx->comp[j].cid == cid) {
                        ctx->comp[j].dctabsel = (sel >> 4) & 15;
                        ctx->comp[j].actabsel = (sel & 15) | 2;
                    }
                }
            }
            ctx->pos += 3; ctx->size -= 3; // Ss, Se, Ah/Al
            break; // O restante são os dados brutos de Huffman
        }

        if (marker >= 0xE0 && marker <= 0xFE) {
            int len = (ctx->pos[0] << 8) | ctx->pos[1];
            ctx->pos += len; ctx->size -= len;
        }
    }

    if (ctx->width <= 0 || ctx->height <= 0 || !ctx->comp[0].pixels) {
        free(ctx);
        return NULL;
    }

    // Decodifica todos os MCUs
    for (int mby = 0; mby < ctx->mbheight; ++mby) {
        for (int mbx = 0; mbx < ctx->mbwidth; ++mbx) {
            for (int c = 0; c < ctx->ncomp; ++c) {
                for (int ssy = 0; ssy < ctx->comp[c].ssy; ++ssy) {
                    for (int ssx = 0; ssx < ctx->comp[c].ssx; ++ssx) {
                        uint8_t *ptr = &ctx->comp[c].pixels[((mby * ctx->comp[c].ssy + ssy) * ctx->comp[c].stride + (mbx * ctx->comp[c].ssx + ssx)) << 3];
                        njDecodeBlock(ctx, &ctx->comp[c], ptr);
                    }
                }
            }
        }
    }

    DecodedImage *img = malloc(sizeof(DecodedImage));
    if (!img) { free(ctx); return NULL; }
    img->width = ctx->width;
    img->height = ctx->height;
    img->bpp = 24;
    img->format = "JPG/JPEG";
    img->pixels = malloc((size_t)ctx->width * ctx->height * sizeof(RGBColor));

    // Conversão de YCbCr -> RGB (com suporte a Chroma Subsampling 4:2:0 / 4:2:2 / 4:4:4)
    for (int y = 0; y < ctx->height; ++y) {
        for (int x = 0; x < ctx->width; ++x) {
            int y_val = ctx->comp[0].pixels[y * ctx->comp[0].stride + x];
            int cb = 128, cr = 128;

            if (ctx->ncomp >= 3) {
                int cx = (x * ctx->comp[1].width) / ctx->width;
                int cy = (y * ctx->comp[1].height) / ctx->height;
                cb = ctx->comp[1].pixels[cy * ctx->comp[1].stride + cx];

                cx = (x * ctx->comp[2].width) / ctx->width;
                cy = (y * ctx->comp[2].height) / ctx->height;
                cr = ctx->comp[2].pixels[cy * ctx->comp[2].stride + cx];
            }

            int r = y_val + (int)(1.402 * (cr - 128));
            int g = y_val - (int)(0.344136 * (cb - 128) + 0.714136 * (cr - 128));
            int b = y_val + (int)(1.772 * (cb - 128));

            RGBColor col;
            col.r = njClip(r);
            col.g = njClip(g);
            col.b = njClip(b);
            col.a = 255;

            img->pixels[y * ctx->width + x] = col;
        }
    }

    for (int i = 0; i < ctx->ncomp; ++i) {
        if (ctx->comp[i].pixels) free(ctx->comp[i].pixels);
    }
    free(ctx);

    return img;
}

// =========================================================================
// CARREGADOR UNIVERSAL (DETECÇÃO POR ASSINATURA MÁGICA)
// =========================================================================
static DecodedImage *load_image_file(const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        fprintf(stderr, "imgview: erro ao abrir '%s': %s\n", filepath, strerror(errno));
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize < 4) {
        fprintf(stderr, "imgview: '%s' e muito pequeno ou vazio.\n", filepath);
        fclose(fp);
        return NULL;
    }

    uint8_t *raw = malloc(fsize);
    if (!raw) { fclose(fp); return NULL; }
    fread(raw, 1, fsize, fp);
    fclose(fp);

    DecodedImage *img = NULL;

    // 1. Tenta carregar como JPEG (0xFFD8)
    if (raw[0] == 0xFF && raw[1] == 0xD8) {
        img = load_jpeg_memory(raw, fsize);
    }
    // 2. Tenta carregar como BMP ('B' / 0x42 e 'M' / 0x4D)
    else if (raw[0] == 0x42 && raw[1] == 0x4D) {
        img = load_bmp_memory(raw, fsize);
    } else {
        fprintf(stderr, "imgview: formato de arquivo nao suportado (assinatura 0x%02X%02X). Use arquivos .BMP ou .JPG.\n", raw[0], raw[1]);
    }

    free(raw);
    return img;
}

static void free_decoded_image(DecodedImage *img) {
    if (img) {
        if (img->pixels) free(img->pixels);
        free(img);
    }
}

static inline RGBColor sample_image(const DecodedImage *img, int target_x, int target_y, int render_w, int render_h) {
    int src_x = (target_x * img->width) / render_w;
    int src_y = (target_y * img->height) / render_h;

    if (src_x < 0) src_x = 0;
    if (src_x >= img->width) src_x = img->width - 1;
    if (src_y < 0) src_y = 0;
    if (src_y >= img->height) src_y = img->height - 1;

    return img->pixels[src_y * img->width + src_x];
}

static void print_image_info(const char *filepath, const DecodedImage *img) {
    printf("\n%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ imgview - Metadados Tecnicos da Imagem ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n\n", COLOR_TITLE, COLOR_RESET);
    printf("  %s• Arquivo          :%s %s\n", COLOR_LABEL, COLOR_RESET, filepath);
    printf("  %s• Formato Detectado:%s %s%s%s\n", COLOR_LABEL, COLOR_RESET, COLOR_VAL, img->format, COLOR_RESET);
    printf("  %s• Dimensoes        :%s %s%d x %d pixels%s\n", COLOR_LABEL, COLOR_RESET, COLOR_VAL, img->width, img->height, COLOR_RESET);
    printf("  %s• Profundidade (BPP):%s %s%d bits por pixel%s\n", COLOR_LABEL, COLOR_RESET, COLOR_VAL, img->bpp, COLOR_RESET);
    printf("  %s• Area em Pixels   :%s %s%u pixels%s\n", COLOR_LABEL, COLOR_RESET, COLOR_VAL, (unsigned int)(img->width * img->height), COLOR_RESET);
    printf("%s=================================================================================%s\n\n", COLOR_TITLE, COLOR_RESET);
}

// Cria imagem BMP de demonstracao com degradê 24-bit
static int create_demo_bmp(const char *out_path) {
    int w = 64, h = 64;
    int row_stride = (w * 3 + 3) & ~3;
    uint32_t image_sz = row_stride * h;
    uint32_t file_sz = sizeof(BMPHeader) + image_sz;

    BMPHeader hdr;
    memset(&hdr, 0, sizeof(BMPHeader));
    hdr.type = 0x4D42; // "BM"
    hdr.file_size = file_sz;
    hdr.offset = sizeof(BMPHeader);
    hdr.header_size = 40;
    hdr.width = w;
    hdr.height = h;
    hdr.planes = 1;
    hdr.bpp = 24;
    hdr.image_size = image_sz;

    uint8_t *buffer = calloc(1, image_sz);
    if (!buffer) return -1;

    for (int y = 0; y < h; y++) {
        uint8_t *row = buffer + (y * row_stride);
        for (int x = 0; x < w; x++) {
            row[x * 3 + 0] = (uint8_t)(x * 4);       // Blue
            row[x * 3 + 1] = (uint8_t)(y * 4);       // Green
            row[x * 3 + 2] = (uint8_t)((x + y) * 2); // Red
        }
    }

    FILE *fp = fopen(out_path, "wb");
    if (!fp) {
        free(buffer);
        return -1;
    }
    fwrite(&hdr, 1, sizeof(BMPHeader), fp);
    fwrite(buffer, 1, image_sz, fp);
    fclose(fp);
    free(buffer);
    return 0;
}

static void render_image_terminal(const DecodedImage *img, const char *title, int custom_width, int opt_gray, int opt_ascii, int opt_zoom) {
    int term_cols, term_rows;
    get_terminal_dimensions(&term_cols, &term_rows);

    int max_w = term_cols - 4;
    int max_h = (term_rows - 4) * 2; // 2 pixels verticais por linha do terminal com '▀'

    if (custom_width > 0) {
        max_w = custom_width;
    }

    if (max_w < 10) max_w = 10;
    if (max_h < 10) max_h = 10;

    double scale_w = (double)max_w / (double)img->width;
    double scale_h = (double)max_h / (double)img->height;
    double scale = (scale_w < scale_h) ? scale_w : scale_h;

    if (scale > 1.0 && !opt_zoom && custom_width <= 0) {
        scale = 1.0;
    }

    int render_w = (int)(img->width * scale);
    int render_h = (int)(img->height * scale);
    if (render_w < 2) render_w = 2;
    if (render_h < 2) render_h = 2;
    if (render_h % 2 != 0) render_h++;

    printf("\n%s╭── [ 🖼️ imgview | %s (%s %dx%d px) ]%s\n",
           COLOR_TITLE, title, img->format, img->width, img->height, COLOR_RESET);

    if (opt_ascii) {
        static const char charset[] = " .:-=+*#%@";
        int num_chars = sizeof(charset) - 1;

        for (int y = 0; y < render_h; y += 2) {
            printf("  ");
            for (int x = 0; x < render_w; x++) {
                RGBColor c = sample_image(img, x, y, render_w, render_h);
                uint8_t lum = (uint8_t)(0.299 * c.r + 0.587 * c.g + 0.114 * c.b);
                int idx = (lum * num_chars) / 256;
                putchar(charset[idx]);
            }
            putchar('\n');
        }
    } else {
        // Renderizador 24-bit TrueColor ANSI Half-Block (▀)
        for (int y = 0; y < render_h; y += 2) {
            printf("  ");
            for (int x = 0; x < render_w; x++) {
                RGBColor top = sample_image(img, x, y, render_w, render_h);
                RGBColor bot = (y + 1 < render_h) ? sample_image(img, x, y + 1, render_w, render_h) : (RGBColor){0,0,0,255};

                if (opt_gray) {
                    uint8_t l_top = (uint8_t)(0.299 * top.r + 0.587 * top.g + 0.114 * top.b);
                    uint8_t l_bot = (uint8_t)(0.299 * bot.r + 0.587 * bot.g + 0.114 * bot.b);
                    top.r = top.g = top.b = l_top;
                    bot.r = bot.g = bot.b = l_bot;
                }

                // Cor superior = Texto (Foreground) | Cor inferior = Fundo (Background)
                printf("\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm▀",
                       top.r, top.g, top.b,
                       bot.r, bot.g, bot.b);
            }
            printf("%s\n", COLOR_RESET);
        }
    }

    printf("%s╰── [ Render: %dx%d células | Escala: %.1f%% ]%s\n\n",
           COLOR_TITLE, render_w, render_h / 2, scale * 100.0, COLOR_RESET);
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 2) {
        print_help();
        utilipc_close();
        return 0;
    }

    const char *filepath = NULL;
    int opt_info = 0;
    int opt_gray = 0;
    int opt_ascii = 0;
    int opt_zoom = 0;
    int custom_width = 0;
    int opt_demo = 0;
    const char *save_demo_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            utilipc_close();
            return 0;
        }
        if (strcmp(argv[i], "--demo") == 0) {
            opt_demo = 1;
            continue;
        }
        if (strcmp(argv[i], "--save-demo") == 0 && i + 1 < argc) {
            save_demo_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--info") == 0) {
            opt_info = 1;
            continue;
        }
        if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--gray") == 0) {
            opt_gray = 1;
            continue;
        }
        if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--ascii") == 0) {
            opt_ascii = 1;
            continue;
        }
        if (strcmp(argv[i], "-z") == 0 || strcmp(argv[i], "--zoom") == 0) {
            opt_zoom = 1;
            continue;
        }
        if ((strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--width") == 0) && i + 1 < argc) {
            custom_width = atoi(argv[++i]);
            continue;
        }
        if (!filepath && argv[i][0] != '-') {
            filepath = argv[i];
        }
    }

    if (save_demo_path) {
        if (create_demo_bmp(save_demo_path) == 0) {
            printf("\n  %s[✔ DEMO GERADO]%s Imagem salva em: %s%s%s (64x64 24-bit)\n\n",
                   COLOR_OK, COLOR_RESET, COLOR_VAL, save_demo_path, COLOR_RESET);
        } else {
            fprintf(stderr, "imgview: erro ao salvar '%s'\n", save_demo_path);
        }
        utilipc_close();
        return 0;
    }

    if (opt_demo) {
        const char *tmp = getenv("TMPDIR");
        if (!tmp || strlen(tmp) == 0) tmp = "/tmp";
        char demo_file[256];
        snprintf(demo_file, sizeof(demo_file), "%s/imgview_demo.bmp", tmp);

        if (create_demo_bmp(demo_file) == 0) {
            DecodedImage *img = load_image_file(demo_file);
            if (img) {
                render_image_terminal(img, "Paleta Procedural Demo", custom_width, opt_gray, opt_ascii, opt_zoom);
                free_decoded_image(img);
            }
            unlink(demo_file);
        }
        utilipc_close();
        return 0;
    }

    if (!filepath) {
        print_help();
        utilipc_close();
        return 1;
    }

    DecodedImage *img = load_image_file(filepath);
    if (!img) {
        utilipc_close();
        return 1;
    }

    if (opt_info) {
        print_image_info(filepath, img);
    } else {
        render_image_terminal(img, filepath, custom_width, opt_gray, opt_ascii, opt_zoom);
    }

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "imgview: rendered '%s' (%s %dx%d)", filepath, img->format, img->width, img->height);
    utilipc_write_status(-1, -1, -1, log_msg);

    free_decoded_image(img);
    utilipc_close();
    return 0;
}
