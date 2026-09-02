#include "multiboot.h"
#include "idt.h"
#include "ps2.h"
#include "gdt.h"
#include "../freestanding/kmem.h"
#include "../freestanding/kfixed.h"
#include "../freestanding/kprintf.h"
#include "../freestanding/kgfx.h"
#include "../freestanding/kstring.h"
#include "../freestanding/kvfs.h"
#include "../freestanding/kata.h"
#include "../freestanding/kata.c"
#include "../freestanding/kdiskfs.h"
#include "../freestanding/kdiskfs.c"
#include "../freestanding/ksound.h"
#include "../freestanding/ksound.c"
#include "../freestanding/kbmp.h"
#include "../freestanding/kbmp.c"

static kgfx_fb_t os_fb;

/* 0 = GUI, 1 = CLI, 2 = KEDIT, 3 = TOP, 4 = SNAKE, 5 = CALC_GUI, 6 = BMP_VIEW */
static int os_mode = 0;

static char cli_input[128] = "";
static size_t cli_pos = 0;
static char current_dir[KVFS_MAX_PATH] = "/";

static char current_user[32] = "root";
static int is_root = 1;

/* --- TERMINAL CANVAS EM RAM --- */
#define TERM_COLS 90
#define TERM_ROWS 36
#define TERM_X_START 30
#define TERM_Y_START 48
#define TERM_CHAR_W 8
#define TERM_CHAR_H 14
#define TERM_WIDTH  (TERM_COLS * TERM_CHAR_W) // 720 px
#define TERM_HEIGHT (TERM_ROWS * TERM_CHAR_H) // 504 px

static uint32_t term_canvas[TERM_WIDTH * TERM_HEIGHT];
static int term_col = 0;
static int term_row = 0;
static int term_batch_mode = 0;

/* Editor KEDIT */
#define EDIT_BUF_SZ 4096
static char edit_buf[EDIT_BUF_SZ];
static size_t edit_len = 0;
static char edit_filepath[KVFS_MAX_PATH] = "";
static char edit_status_msg[64] = "";

/* Visualizador BMP */
static char view_bmp_path[KVFS_MAX_PATH] = "";

/* Botões do Desktop */
static kgfx_rect_t demo_btn = { .x = 30,  .y = 250, .w = 230, .h = 36 };
static kgfx_rect_t game_btn = { .x = 280, .y = 250, .w = 220, .h = 36 };
static kgfx_rect_t calc_btn = { .x = 520, .y = 250, .w = 230, .h = 36 };

static int btn_pressed_state = 0;
static int btn_pressed_prev = 0;
static int toast_active = 0;
static uint32_t toast_expire_tick = 0;

/* Buffer de restauração do mouse (16x16) */
#define MOUSE_BUF_SZ 16
static uint32_t mouse_under_buf[MOUSE_BUF_SZ * MOUSE_BUF_SZ];
static int mouse_under_saved = 0;
static int under_x = 0, under_y = 0, under_w = 0, under_h = 0;
static int prev_mouse_x = 400, prev_mouse_y = 300;

#if defined(__x86_64__) || defined(__i386__)
static inline void fast_copy_dwords(void *dest, const void *src, size_t n_dwords) {
    __asm__ __volatile__ ("cld; rep movsl"
                          : "+D"(dest), "+S"(src), "+c"(n_dwords)
                          : : "memory");
}
static inline void fast_set_dwords(void *dest, uint32_t val, size_t n_dwords) {
    __asm__ __volatile__ ("cld; rep stosl"
                          : "+D"(dest), "+c"(n_dwords)
                          : "a"(val)
                          : "memory");
}
#else
static inline void fast_copy_dwords(void *dest, const void *src, size_t n_dwords) {
    uint32_t *d = (uint32_t *)dest;
    const uint32_t *s = (const uint32_t *)src;
    for (size_t i = 0; i < n_dwords; i++) d[i] = s[i];
}
static inline void fast_set_dwords(void *dest, uint32_t val, size_t n_dwords) {
    uint32_t *d = (uint32_t *)dest;
    for (size_t i = 0; i < n_dwords; i++) d[i] = val;
}
#endif

/* --- CALCULADORA GUI --- */
#define CALC_WIN_X 240
#define CALC_WIN_Y 110
#define CALC_WIN_W 320
#define CALC_WIN_H 370

static char calc_display[64] = "0";
static char calc_op = 0;
static int64_t calc_operand1 = 0;
static int calc_new_num = 1;

typedef struct {
    kgfx_rect_t rect;
    const char *label;
    uint32_t color;
} calc_btn_t;

static const calc_btn_t calc_buttons[] = {
    {{CALC_WIN_X + 15,  CALC_WIN_Y + 90,  60, 40}, "C",        KGFX_RED},
    {{CALC_WIN_X + 85,  CALC_WIN_Y + 90,  60, 40}, "^",        KGFX_PURPLE},
    {{CALC_WIN_X + 155, CALC_WIN_Y + 90,  70, 40}, "r(Bit)",   KGFX_CYAN},
    {{CALC_WIN_X + 235, CALC_WIN_Y + 90,  70, 40}, "r(Nwtn)",  KGFX_CYAN},

    {{CALC_WIN_X + 15,  CALC_WIN_Y + 145, 60, 40}, "7",        KGFX_NAVY},
    {{CALC_WIN_X + 85,  CALC_WIN_Y + 145, 60, 40}, "8",        KGFX_NAVY},
    {{CALC_WIN_X + 155, CALC_WIN_Y + 145, 60, 40}, "9",        KGFX_NAVY},
    {{CALC_WIN_X + 235, CALC_WIN_Y + 145, 70, 40}, "/",        KGFX_YELLOW},

    {{CALC_WIN_X + 15,  CALC_WIN_Y + 200, 60, 40}, "4",        KGFX_NAVY},
    {{CALC_WIN_X + 85,  CALC_WIN_Y + 200, 60, 40}, "5",        KGFX_NAVY},
    {{CALC_WIN_X + 155, CALC_WIN_Y + 200, 60, 40}, "6",        KGFX_NAVY},
    {{CALC_WIN_X + 235, CALC_WIN_Y + 200, 70, 40}, "*",        KGFX_YELLOW},

    {{CALC_WIN_X + 15,  CALC_WIN_Y + 255, 60, 40}, "1",        KGFX_NAVY},
    {{CALC_WIN_X + 85,  CALC_WIN_Y + 255, 60, 40}, "2",        KGFX_NAVY},
    {{CALC_WIN_X + 155, CALC_WIN_Y + 255, 60, 40}, "3",        KGFX_NAVY},
    {{CALC_WIN_X + 235, CALC_WIN_Y + 255, 70, 40}, "-",        KGFX_YELLOW},

    {{CALC_WIN_X + 15,  CALC_WIN_Y + 310, 60, 40}, "0",        KGFX_NAVY},
    {{CALC_WIN_X + 85,  CALC_WIN_Y + 310, 130, 40}, "=",       KGFX_GREEN},
    {{CALC_WIN_X + 235, CALC_WIN_Y + 310, 70, 40}, "+",        KGFX_YELLOW}
};

static uint32_t sqrt_bitshift(uint32_t n) {
    if (n == 0) return 0;
    uint32_t res = 0;
    uint32_t bit = 1U << 30;
    while (bit > n) bit >>= 2;
    while (bit != 0) {
        if (n >= res + bit) {
            n -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

static double sqrt_newton_raphson(double s) {
    if (s <= 0.0) return 0.0;
    double x = s;
    for (int i = 0; i < 15; i++) {
        x = 0.5 * (x + s / x);
    }
    return x;
}

static int64_t int_pow(int64_t base, int64_t exp) {
    if (exp < 0) return 0;
    int64_t res = 1;
    while (exp > 0) {
        if (exp & 1) res *= base;
        base *= base;
        exp >>= 1;
    }
    return res;
}

/* --- JOGO SNAKE --- */
#define SNAKE_GRID_W 28
#define SNAKE_GRID_H 20
#define SNAKE_CELL_SZ 16
#define SNAKE_ORIGIN_X 176
#define SNAKE_ORIGIN_Y 130

typedef struct { int x, y; } snake_pt_t;
static snake_pt_t snake_body[SNAKE_GRID_W * SNAKE_GRID_H];
static int snake_len = 4;
static int snake_dir_x = 1, snake_dir_y = 0;
static snake_pt_t snake_food = {15, 10};
static snake_pt_t snake_old_tail = {0, 0};
static int snake_score = 0;
static int snake_gameover = 0;
static uint32_t snake_last_tick = 0;

static void draw_gui_desktop(void);
static void start_snake_game(void);
static void update_calc_display_only(void);
static void flush_full_canvas_to_vram(void);

static void save_mouse_under(int x, int y, int w, int h) {
    under_x = x; under_y = y; under_w = w; under_h = h;
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            int px = x + c, py = y + r;
            if (px >= 0 && (uint32_t)px < os_fb.width && py >= 0 && (uint32_t)py < os_fb.height) {
                mouse_under_buf[r * MOUSE_BUF_SZ + c] = os_fb.buffer[py * os_fb.pitch + px];
            } else {
                mouse_under_buf[r * MOUSE_BUF_SZ + c] = KGFX_DARKGRAY;
            }
        }
    }
    mouse_under_saved = 1;
}

static void restore_mouse_under(void) {
    if (!mouse_under_saved) return;
    for (int r = 0; r < under_h; r++) {
        for (int c = 0; c < under_w; c++) {
            int px = under_x + c, py = under_y + r;
            if (px >= 0 && (uint32_t)px < os_fb.width && py >= 0 && (uint32_t)py < os_fb.height) {
                os_fb.buffer[py * os_fb.pitch + px] = mouse_under_buf[r * MOUSE_BUF_SZ + c];
            }
        }
    }
    mouse_under_saved = 0;
}

static void draw_demo_button(int pressed) {
    if (pressed) {
        kgfx_draw_rounded_rect(&os_fb, demo_btn.x + 2, demo_btn.y + 2, demo_btn.w - 4, demo_btn.h - 4, 6, KGFX_BLUE, 1);
        kgfx_draw_string(&os_fb, demo_btn.x + 18, demo_btn.y + 12, "[*] Testar Botao Clicavel", KGFX_WHITE, KGFX_BLUE);
    } else {
        kgfx_draw_rect(&os_fb, demo_btn.x, demo_btn.y, demo_btn.w, demo_btn.h, KGFX_DARKGRAY, 1);
        kgfx_draw_rounded_rect(&os_fb, demo_btn.x, demo_btn.y, demo_btn.w, demo_btn.h, 6, KGFX_PURPLE, 1);
        kgfx_draw_string(&os_fb, demo_btn.x + 16, demo_btn.y + 11, "[*] Testar Botao Clicavel", KGFX_WHITE, KGFX_PURPLE);
    }
}

static void draw_toast_notification(int show) {
    if (show) {
        kgfx_draw_rounded_rect(&os_fb, 30, 305, 540, 42, 8, kgfx_argb(230, 49, 50, 68), 1);
        kgfx_draw_string(&os_fb, 45, 320, "[!] Clique detectado com som! Mensagem sumira em 5s.", KGFX_GREEN, 0);
    } else {
        kgfx_draw_rect(&os_fb, 25, 300, 550, 52, KGFX_DARKGRAY, 1);
    }
}

static void update_calc_display_only(void) {
    kgfx_draw_rounded_rect(&os_fb, CALC_WIN_X + 15, CALC_WIN_Y + 40, CALC_WIN_W - 30, 40, 6, KGFX_BLACK, 1);
    kgfx_draw_string_scaled(&os_fb, CALC_WIN_X + 25, CALC_WIN_Y + 48, calc_display, KGFX_GREEN, KGFX_BLACK, 2);
}

static void handle_calc_click(const char *label) {
    ksound_beep(900, 4, pit_get_ticks());

    if (kstrcmp(label, "C") == 0) {
        kstrncpy(calc_display, "0", sizeof(calc_display) - 1);
        calc_op = 0;
        calc_operand1 = 0;
        calc_new_num = 1;
    } else if (kstrcmp(label, "r(Bit)") == 0) {
        uint32_t val = (uint32_t)katoi(calc_display);
        uint32_t root = sqrt_bitshift(val);
        kitoa(root, calc_display, 10, 0);
        calc_new_num = 1;
    } else if (kstrcmp(label, "r(Nwtn)") == 0) {
        double val = (double)katoi(calc_display);
        double root = sqrt_newton_raphson(val);
        int64_t int_root = (int64_t)(root + 0.5);
        kitoa(int_root, calc_display, 10, 0);
        calc_new_num = 1;
    } else if (kstrcmp(label, "+") == 0 || kstrcmp(label, "-") == 0 ||
               kstrcmp(label, "*") == 0 || kstrcmp(label, "/") == 0 || kstrcmp(label, "^") == 0) {
        calc_operand1 = katoi(calc_display);
        calc_op = label[0];
        calc_new_num = 1;
    } else if (kstrcmp(label, "=") == 0) {
        int64_t operand2 = katoi(calc_display);
        int64_t res = 0;
        if (calc_op == '+') res = calc_operand1 + operand2;
        else if (calc_op == '-') res = calc_operand1 - operand2;
        else if (calc_op == '*') res = calc_operand1 * operand2;
        else if (calc_op == '/') res = (operand2 != 0) ? (calc_operand1 / operand2) : 0;
        else if (calc_op == '^') res = int_pow(calc_operand1, operand2);
        else res = operand2;

        kitoa(res, calc_display, 10, 0);
        calc_op = 0;
        calc_new_num = 1;
    } else {
        if (calc_new_num || kstrcmp(calc_display, "0") == 0) {
            kstrncpy(calc_display, label, sizeof(calc_display) - 1);
            calc_new_num = 0;
        } else {
            size_t l = kstrlen(calc_display);
            if (l < sizeof(calc_display) - 2) {
                calc_display[l] = label[0];
                calc_display[l + 1] = '\0';
            }
        }
    }
    update_calc_display_only();
}

static void render_calc_window(void) {
    mouse_under_saved = 0;
    kgfx_clear(&os_fb, KGFX_DARKGRAY);

    kgfx_draw_rounded_rect(&os_fb, CALC_WIN_X, CALC_WIN_Y, CALC_WIN_W, CALC_WIN_H, 10, KGFX_PURPLE, 1);
    kgfx_draw_rounded_rect(&os_fb, CALC_WIN_X + 4, CALC_WIN_Y + 4, CALC_WIN_W - 8, CALC_WIN_H - 8, 8, KGFX_NAVY, 1);

    kgfx_draw_rect(&os_fb, CALC_WIN_X + 4, CALC_WIN_Y + 4, CALC_WIN_W - 8, 28, KGFX_PURPLE, 1);
    kgfx_draw_string(&os_fb, CALC_WIN_X + 15, CALC_WIN_Y + 12, "Calculadora GUI (BitShift & Newton)", KGFX_WHITE, KGFX_PURPLE);
    kgfx_draw_string(&os_fb, CALC_WIN_X + CALC_WIN_W - 35, CALC_WIN_Y + 12, "[ESC]", KGFX_YELLOW, KGFX_PURPLE);

    update_calc_display_only();

    for (size_t i = 0; i < sizeof(calc_buttons) / sizeof(calc_buttons[0]); i++) {
        calc_btn_t b = calc_buttons[i];
        kgfx_draw_rounded_rect(&os_fb, b.rect.x, b.rect.y, b.rect.w, b.rect.h, 5, b.color, 1);
        kgfx_draw_string(&os_fb, b.rect.x + 15, b.rect.y + 14, b.label, KGFX_WHITE, b.color);
    }
}

void os_draw_mouse_cursor(void) {
    if (os_mode != 0 && os_mode != 5) return;
    kgfx_mouse_t *mouse = ps2_get_mouse_state();

    if (os_mode == 0) {
        int over_btn = kgfx_rect_contains(&demo_btn, mouse->x, mouse->y);
        int over_game = kgfx_rect_contains(&game_btn, mouse->x, mouse->y);
        int over_calc = kgfx_rect_contains(&calc_btn, mouse->x, mouse->y);

        mouse->type = (over_btn || over_game || over_calc) ? KGFX_CURSOR_CLICKABLE : KGFX_CURSOR_NORMAL;

        if (over_btn) {
            if ((mouse->buttons & 1) && !btn_pressed_prev) {
                btn_pressed_state = 1;
                draw_demo_button(1);
                ksound_beep(880, 4, pit_get_ticks());
                toast_active = 1;
                toast_expire_tick = pit_get_ticks() + 500;
                draw_toast_notification(1);
            } else if (!(mouse->buttons & 1) && btn_pressed_state) {
                btn_pressed_state = 0;
                draw_demo_button(0);
            }
        } else if (btn_pressed_state) {
            btn_pressed_state = 0;
            draw_demo_button(0);
        }

        if (over_game && (mouse->buttons & 1) && !btn_pressed_prev) {
            ksound_beep(1200, 6, pit_get_ticks());
            start_snake_game();
            return;
        }

        if (over_calc && (mouse->buttons & 1) && !btn_pressed_prev) {
            ksound_beep(1000, 6, pit_get_ticks());
            os_mode = 5;
            render_calc_window();
            return;
        }
    } else if (os_mode == 5) {
        int over_any = 0;
        for (size_t i = 0; i < sizeof(calc_buttons) / sizeof(calc_buttons[0]); i++) {
            if (kgfx_rect_contains(&calc_buttons[i].rect, mouse->x, mouse->y)) {
                over_any = 1;
                if ((mouse->buttons & 1) && !btn_pressed_prev) {
                    handle_calc_click(calc_buttons[i].label);
                }
                break;
            }
        }
        mouse->type = over_any ? KGFX_CURSOR_CLICKABLE : KGFX_CURSOR_NORMAL;
    }

    btn_pressed_prev = (mouse->buttons & 1);

    if (mouse->x == prev_mouse_x && mouse->y == prev_mouse_y && mouse_under_saved) return;

    if (mouse_under_saved) {
        restore_mouse_under();
    }

    save_mouse_under(mouse->x, mouse->y, MOUSE_BUF_SZ, MOUSE_BUF_SZ);
    kgfx_draw_cursor(&os_fb, mouse);

    prev_mouse_x = mouse->x;
    prev_mouse_y = mouse->y;
}

static void draw_char_to_canvas(char c, int cx, int cy, uint32_t fg) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = kgfx_font8x8[c - 32];

    for (int r = 0; r < 8; r++) {
        uint8_t bits = glyph[r];
        uint32_t *line = &term_canvas[(cy + r) * TERM_WIDTH + cx];
        for (int b = 0; b < 8; b++) {
            line[b] = (bits & (1 << (7 - b))) ? fg : KGFX_BLACK;
        }
    }
}

static void blit_char_to_vram(int cx, int cy) {
    for (int r = 0; r < TERM_CHAR_H; r++) {
        uint32_t *vram = &os_fb.buffer[(TERM_Y_START + cy + r) * os_fb.pitch + (TERM_X_START + cx)];
        const uint32_t *ram = &term_canvas[(cy + r) * TERM_WIDTH + cx];
        fast_copy_dwords(vram, ram, TERM_CHAR_W);
    }
}

static void flush_full_canvas_to_vram(void) {
    for (int y = 0; y < TERM_HEIGHT; y++) {
        uint32_t *vram = &os_fb.buffer[(TERM_Y_START + y) * os_fb.pitch + TERM_X_START];
        const uint32_t *ram = &term_canvas[y * TERM_WIDTH];
        fast_copy_dwords(vram, ram, TERM_WIDTH);
    }
}

static void terminal_scroll_ram(void) {
    size_t words_to_move = TERM_WIDTH * (TERM_HEIGHT - TERM_CHAR_H);
    kmemmove(term_canvas, term_canvas + (TERM_CHAR_H * TERM_WIDTH), words_to_move * sizeof(uint32_t));

    size_t bottom_words = TERM_WIDTH * TERM_CHAR_H;
    kmemset(term_canvas + words_to_move, 0, bottom_words * sizeof(uint32_t));

    if (!term_batch_mode) {
        flush_full_canvas_to_vram();
    }
}

static void os_putchar(char c) {
    if (c == '\n') {
        term_col = 0;
        term_row++;
        if (term_row >= TERM_ROWS) {
            terminal_scroll_ram();
            term_row = TERM_ROWS - 1;
        }
        return;
    }

    if (c == '\b') {
        if (term_col > 0) {
            term_col--;
            int cx = term_col * TERM_CHAR_W;
            int cy = term_row * TERM_CHAR_H;
            for (int r = 0; r < TERM_CHAR_H; r++) {
                kmemset(&term_canvas[(cy + r) * TERM_WIDTH + cx], 0, TERM_CHAR_W * sizeof(uint32_t));
            }
            if (!term_batch_mode) {
                blit_char_to_vram(cx, cy);
            }
        }
        return;
    }

    if ((unsigned char)c >= 32) {
        if (term_row >= TERM_ROWS) {
            terminal_scroll_ram();
            term_row = TERM_ROWS - 1;
        }

        int cx = term_col * TERM_CHAR_W;
        int cy = term_row * TERM_CHAR_H;

        draw_char_to_canvas(c, cx, cy, KGFX_WHITE);

        if (!term_batch_mode) {
            blit_char_to_vram(cx, cy);
        }

        term_col++;
        if (term_col >= TERM_COLS) {
            term_col = 0;
            term_row++;
            if (term_row >= TERM_ROWS) {
                terminal_scroll_ram();
                term_row = TERM_ROWS - 1;
            }
        }
    }
}

static void print_prompt(void) {
    kprintf("%s@utils-os:%s%c ", current_user, current_dir, is_root ? '#' : '$');
}

static void draw_gui_desktop(void) {
    mouse_under_saved = 0;
    kgfx_clear(&os_fb, KGFX_DARKGRAY);

    kgfx_draw_rounded_rect(&os_fb, 10, 10, os_fb.width - 20, os_fb.height - 20, 10, KGFX_CYAN, 0);
    kgfx_draw_rect(&os_fb, 12, 12, os_fb.width - 24, 36, KGFX_BLUE, 1);
    kgfx_draw_string_scaled(&os_fb, 20, 20, "utils-in-c OS v2.6", KGFX_WHITE, KGFX_BLUE, 2);

    kgfx_draw_rect_alpha(&os_fb, 20, 60, os_fb.width - 40, 45, kgfx_argb(180, 24, 24, 37));
    kgfx_draw_string(&os_fb, 30, 75, "Pressione [ESC] para o CLI! Botoes abaixo para Apps e Jogos!", KGFX_YELLOW, 0);

    kgfx_draw_filled_circle(&os_fb, os_fb.width - 80, 180, 35, KGFX_PURPLE);
    kgfx_draw_triangle(&os_fb, os_fb.width - 150, 220, os_fb.width - 110, 150, os_fb.width - 70, 220, KGFX_GREEN, 1);

    const kata_drive_info_t *disk = kata_get_info();
    kgfx_draw_string(&os_fb, 30, 130, "[Kernel Core Subsystems Online]", KGFX_WHITE, 0);
    kgfx_draw_string(&os_fb, 30, 150, "  * Persistent KSFS   : Root / with /bin, /etc, /dev, /home on disk.img", KGFX_CYAN, 0);
    if (disk && disk->drive_present) {
        kgfx_draw_string(&os_fb, 30, 170, "  * ATA Storage Mode  : disk.img Mounted & Read/Write Ready", KGFX_GREEN, 0);
    } else {
        kgfx_draw_string(&os_fb, 30, 170, "  * ATA Storage Mode  : Standalone RAM Mode", KGFX_CYAN, 0);
    }
    kgfx_draw_string(&os_fb, 30, 190, "  * Multi-User Engine : User Authentication Active (whoami / su / adduser)", KGFX_CYAN, 0);
    kgfx_draw_string(&os_fb, 30, 210, "  * Sound & Timer     : PC Speaker Audio (0x61) + PIT 100Hz Active", KGFX_CYAN, 0);

    draw_demo_button(0);

    kgfx_draw_rounded_rect(&os_fb, game_btn.x, game_btn.y, game_btn.w, game_btn.h, 6, KGFX_GREEN, 1);
    kgfx_draw_string(&os_fb, game_btn.x + 18, game_btn.y + 11, "[*] Jogar Snake", KGFX_WHITE, KGFX_GREEN);

    kgfx_draw_rounded_rect(&os_fb, calc_btn.x, calc_btn.y, calc_btn.w, calc_btn.h, 6, KGFX_PURPLE, 1);
    kgfx_draw_string(&os_fb, calc_btn.x + 18, calc_btn.y + 11, "[*] Calculadora GUI", KGFX_WHITE, KGFX_PURPLE);

    if (toast_active) {
        draw_toast_notification(1);
    }
}

/* --- SNAKE --- */
static void spawn_snake_food(void) {
    uint32_t seed = pit_get_ticks();
    snake_food.x = (int)(seed % (SNAKE_GRID_W - 2)) + 1;
    snake_food.y = (int)((seed / 7) % (SNAKE_GRID_H - 2)) + 1;

    int fx = SNAKE_ORIGIN_X + snake_food.x * SNAKE_CELL_SZ;
    int fy = SNAKE_ORIGIN_Y + snake_food.y * SNAKE_CELL_SZ;
    kgfx_draw_filled_circle(&os_fb, fx + 8, fy + 8, 6, KGFX_RED);
}

static void start_snake_game(void) {
    mouse_under_saved = 0;
    os_mode = 4;
    snake_len = 4;
    snake_score = 0;
    snake_gameover = 0;
    snake_dir_x = 1;
    snake_dir_y = 0;

    kgfx_clear(&os_fb, KGFX_BLACK);

    kgfx_draw_rounded_rect(&os_fb, SNAKE_ORIGIN_X - 10, SNAKE_ORIGIN_Y - 40,
                           SNAKE_GRID_W * SNAKE_CELL_SZ + 20, SNAKE_GRID_H * SNAKE_CELL_SZ + 50, 10, KGFX_CYAN, 0);

    char score_txt[64];
    ksnprintf(score_txt, sizeof(score_txt), "SNAKE RETRO - Pontos: 0  |  ESC: Sair");
    kgfx_draw_string_scaled(&os_fb, SNAKE_ORIGIN_X + 20, SNAKE_ORIGIN_Y - 30, score_txt, KGFX_YELLOW, 0, 1);

    for (int i = 0; i < snake_len; i++) {
        snake_body[i].x = 10 - i;
        snake_body[i].y = 10;
        int bx = SNAKE_ORIGIN_X + snake_body[i].x * SNAKE_CELL_SZ;
        int by = SNAKE_ORIGIN_Y + snake_body[i].y * SNAKE_CELL_SZ;
        kgfx_draw_rounded_rect(&os_fb, bx + 1, by + 1, SNAKE_CELL_SZ - 2, SNAKE_CELL_SZ - 2, 3, (i == 0) ? KGFX_YELLOW : KGFX_GREEN, 1);
    }

    spawn_snake_food();
    snake_last_tick = pit_get_ticks();
}

static void update_snake_game(void) {
    if (snake_gameover) return;

    uint32_t current_tick = pit_get_ticks();
    if (current_tick - snake_last_tick < 10) return;
    snake_last_tick = current_tick;

    snake_pt_t new_head = { snake_body[0].x + snake_dir_x, snake_body[0].y + snake_dir_y };

    if (new_head.x < 0 || new_head.x >= SNAKE_GRID_W || new_head.y < 0 || new_head.y >= SNAKE_GRID_H) {
        snake_gameover = 1;
        ksound_beep(150, 30, pit_get_ticks());
        kgfx_draw_rect_alpha(&os_fb, SNAKE_ORIGIN_X + 20, SNAKE_ORIGIN_Y + 120, 400, 60, kgfx_argb(220, 20, 20, 20));
        kgfx_draw_string(&os_fb, SNAKE_ORIGIN_X + 60, SNAKE_ORIGIN_Y + 135, "GAME OVER! Pressione ESPACO para reiniciar", KGFX_RED, 0);
        kgfx_draw_string(&os_fb, SNAKE_ORIGIN_X + 110, SNAKE_ORIGIN_Y + 155, "ou [ESC] para voltar ao sistema", KGFX_WHITE, 0);
        return;
    }

    for (int i = 0; i < snake_len; i++) {
        if (snake_body[i].x == new_head.x && snake_body[i].y == new_head.y) {
            snake_gameover = 1;
            ksound_beep(150, 30, pit_get_ticks());
            kgfx_draw_rect_alpha(&os_fb, SNAKE_ORIGIN_X + 20, SNAKE_ORIGIN_Y + 120, 400, 60, kgfx_argb(220, 20, 20, 20));
            kgfx_draw_string(&os_fb, SNAKE_ORIGIN_X + 60, SNAKE_ORIGIN_Y + 135, "GAME OVER! Pressione ESPACO para reiniciar", KGFX_RED, 0);
            kgfx_draw_string(&os_fb, SNAKE_ORIGIN_X + 110, SNAKE_ORIGIN_Y + 155, "ou [ESC] para voltar ao sistema", KGFX_WHITE, 0);
            return;
        }
    }

    snake_old_tail = snake_body[snake_len - 1];
    int ox = SNAKE_ORIGIN_X + snake_old_tail.x * SNAKE_CELL_SZ;
    int oy = SNAKE_ORIGIN_Y + snake_old_tail.y * SNAKE_CELL_SZ;
    kgfx_draw_rect(&os_fb, ox, oy, SNAKE_CELL_SZ, SNAKE_CELL_SZ, KGFX_BLACK, 1);

    int old_hx = SNAKE_ORIGIN_X + snake_body[0].x * SNAKE_CELL_SZ;
    int old_hy = SNAKE_ORIGIN_Y + snake_body[0].y * SNAKE_CELL_SZ;
    kgfx_draw_rounded_rect(&os_fb, old_hx + 1, old_hy + 1, SNAKE_CELL_SZ - 2, SNAKE_CELL_SZ - 2, 3, KGFX_GREEN, 1);

    for (int i = snake_len - 1; i > 0; i--) {
        snake_body[i] = snake_body[i - 1];
    }
    snake_body[0] = new_head;

    int nhx = SNAKE_ORIGIN_X + new_head.x * SNAKE_CELL_SZ;
    int nhy = SNAKE_ORIGIN_Y + new_head.y * SNAKE_CELL_SZ;
    kgfx_draw_rounded_rect(&os_fb, nhx + 1, nhy + 1, SNAKE_CELL_SZ - 2, SNAKE_CELL_SZ - 2, 3, KGFX_YELLOW, 1);

    if (new_head.x == snake_food.x && new_head.y == snake_food.y) {
        if (snake_len < SNAKE_GRID_W * SNAKE_GRID_H - 1) snake_len++;
        snake_score += 10;
        ksound_beep(1400, 5, pit_get_ticks());

        kgfx_draw_rect(&os_fb, SNAKE_ORIGIN_X + 20, SNAKE_ORIGIN_Y - 35, 300, 20, KGFX_BLACK, 1);
        char score_txt[64];
        ksnprintf(score_txt, sizeof(score_txt), "SNAKE RETRO - Pontos: %d  |  ESC: Sair", snake_score);
        kgfx_draw_string_scaled(&os_fb, SNAKE_ORIGIN_X + 20, SNAKE_ORIGIN_Y - 30, score_txt, KGFX_YELLOW, 0, 1);

        spawn_snake_food();
    }
}

/* --- KEDIT & TOP --- */
static void render_editor(void) {
    mouse_under_saved = 0;
    kgfx_clear(&os_fb, KGFX_BLACK);
    kgfx_draw_rect(&os_fb, 0, 0, os_fb.width, 28, KGFX_BLUE, 1);
    char header_title[128];
    ksnprintf(header_title, sizeof(header_title), "KEDIT - %s", edit_filepath);
    kgfx_draw_string(&os_fb, 15, 8, header_title, KGFX_WHITE, KGFX_BLUE);

    int cur_x = 20, cur_y = 40;
    for (size_t i = 0; i < edit_len; i++) {
        char c = edit_buf[i];
        if (c == '\n') {
            cur_x = 20; cur_y += 14;
            if (cur_y > (int)os_fb.height - 50) break;
        } else {
            kgfx_draw_char(&os_fb, cur_x, cur_y, c, KGFX_WHITE, 0);
            cur_x += 8;
            if (cur_x > (int)os_fb.width - 30) { cur_x = 20; cur_y += 14; }
        }
    }
    kgfx_draw_rect(&os_fb, cur_x, cur_y, 8, 12, KGFX_GREEN, 1);

    kgfx_draw_rect(&os_fb, 0, os_fb.height - 30, os_fb.width, 30, KGFX_DARKGRAY, 1);
    char footer_txt[128];
    ksnprintf(footer_txt, sizeof(footer_txt), "^S Salvar | ESC/ ^Q Sair | Bytes: %u | %s", (unsigned int)edit_len, edit_status_msg);
    kgfx_draw_string(&os_fb, 15, os_fb.height - 20, footer_txt, KGFX_YELLOW, KGFX_DARKGRAY);
}

static void render_top(void) {
    mouse_under_saved = 0;
    kgfx_clear(&os_fb, KGFX_BLACK);
    kgfx_draw_rounded_rect(&os_fb, 10, 10, os_fb.width - 20, os_fb.height - 20, 8, KGFX_CYAN, 0);

    uint32_t secs = pit_get_seconds();
    uint32_t mins = secs / 60;
    secs %= 60;

    kprintf("[ test_os - Process Activity Monitor (top) ]\n");
    kprintf("  Uptime: %02u:%02u | Tasks: 6 total, 1 running, 5 sleeping | KMEM Free: %u KB\n\n",
            (unsigned int)mins, (unsigned int)secs, (unsigned int)(kmem_get_free_bytes() / 1024));

    kprintf("  %-6s %-10s %-6s %-8s %-10s %-10s %s\n", "PID", "USER", "PR", "CPU%", "MEM(KB)", "STATE", "COMMAND");
    kprintf("  ------------------------------------------------------------------\n");
    kprintf("  %-6d %-10s %-6d %-8s %-10d %-10s %s\n", 0, "root", 20, "0.0%", 64, "SLEEPING", "kernel_idle");
    kprintf("  %-6d %-10s %-6d %-8s %-10d %-10s %s\n", 1, "root", 20, "1.4%", 256, "RUNNING", "gui_desktop");
    kprintf("  %-6d %-10s %-6d %-8s %-10d %-10s %s\n", 2, "root", 20, "0.1%", 128, "SLEEPING", "ps2_input");
    kprintf("  %-6d %-10s %-6d %-8s %-10d %-10s %s\n", 3, "root", 20, "0.0%", 96, "SLEEPING", "pit_timer_100hz");
    kprintf("  %-6d %-10s %-6d %-8s %-10d %-10s %s\n", 4, "root", 20, "0.2%", 512, "SLEEPING", "vfs_ksfs_ata");
    kprintf("  %-6d %-10s %-6d %-8s %-10d %-10s %s\n", 5, current_user, 20, "0.6%", 180, "SLEEPING", "cli_shell");
    kprintf("\n  [Pressione 'q' ou 'ESC' para sair do top]\n");
}

static void render_bmp_view(void) {
    mouse_under_saved = 0;
    kgfx_clear(&os_fb, KGFX_BLACK);

    kgfx_draw_rect(&os_fb, 0, 0, os_fb.width, 30, KGFX_BLUE, 1);
    char title[128];
    ksnprintf(title, sizeof(title), "Visualizador BMP - %s  (Pressione ESC para fechar)", view_bmp_path);
    kgfx_draw_string(&os_fb, 20, 10, title, KGFX_WHITE, KGFX_BLUE);

    const kvfs_node_t *file = kvfs_open(view_bmp_path);
    if (file && file->data && file->size > 0) {
        kbmp_render(&os_fb, (os_fb.width - 128) / 2, (os_fb.height - 128) / 2, file->data, file->size);
    } else {
        kgfx_draw_string(&os_fb, 50, 100, "Erro: Arquivo BMP nao encontrado ou invalido!", KGFX_RED, 0);
    }
}

static void vfs_ls_callback(const char *name, size_t size, uint8_t type, uint16_t mode) {
    (void)mode;
    if (type == KVFS_TYPE_DIR) {
        kprintf("  [DIR]   %-16s\n", name);
    } else if (type == KVFS_TYPE_BIN) {
        kprintf("  [BIN]   %-16s (%u bytes)\n", name, (unsigned int)size);
    } else if (type == KVFS_TYPE_DEV) {
        kprintf("  [DEV]   %-16s\n", name);
    } else {
        kprintf("  [FILE]  %-16s (%u bytes)\n", name, (unsigned int)size);
    }
}

static void resolve_path(const char *input_path, char *out, size_t max_len) {
    if (!input_path || input_path[0] == '\0') {
        kstrncpy(out, current_dir, max_len - 1);
        out[max_len - 1] = '\0';
        return;
    }
    if (input_path[0] == '/') {
        kstrncpy(out, input_path, max_len - 1);
        out[max_len - 1] = '\0';
        return;
    }
    if (kstrcmp(input_path, ".") == 0) {
        kstrncpy(out, current_dir, max_len - 1);
        out[max_len - 1] = '\0';
        return;
    }
    if (kstrcmp(input_path, "..") == 0) {
        if (kstrcmp(current_dir, "/") == 0) {
            kstrncpy(out, "/", max_len - 1);
        } else {
            kstrncpy(out, current_dir, max_len - 1);
            char *last_slash = kstrchr(out + 1, '/');
            if (last_slash) *last_slash = '\0';
            else { out[0] = '/'; out[1] = '\0'; }
        }
        return;
    }
    if (kstrcmp(current_dir, "/") == 0) {
        ksnprintf(out, max_len, "/%s", input_path);
    } else {
        ksnprintf(out, max_len, "%s/%s", current_dir, input_path);
    }
}

static int verify_user_password(const char *user, const char *pass) {
    const kvfs_node_t *passwd_file = kvfs_open("/etc/passwd");
    if (!passwd_file || !passwd_file->data) {
        if (kstrcmp(user, "root") == 0 && kstrcmp(pass, "root") == 0) return 1;
        return 0;
    }
    const char *data = (const char *)passwd_file->data;
    char target_prefix[64];
    ksnprintf(target_prefix, sizeof(target_prefix), "%s:%s:", user, pass);
    if (kstrncmp(data, target_prefix, kstrlen(target_prefix)) == 0) return 1;
    char line_prefix[64];
    ksnprintf(line_prefix, sizeof(line_prefix), "\n%s:%s:", user, pass);
    if (kstrstr(data, line_prefix) != NULL) return 1;
    return 0;
}

static void open_editor_file(const char *path) {
    resolve_path(path, edit_filepath, sizeof(edit_filepath));
    edit_len = 0;
    edit_buf[0] = '\0';
    kstrncpy(edit_status_msg, "Modo Edicao", sizeof(edit_status_msg) - 1);

    const kvfs_node_t *file = kvfs_open(edit_filepath);
    if (file && file->data && file->size > 0) {
        edit_len = file->size;
        if (edit_len >= EDIT_BUF_SZ) edit_len = EDIT_BUF_SZ - 1;
        kmemcpy(edit_buf, file->data, edit_len);
        edit_buf[edit_len] = '\0';
    }

    os_mode = 2;
    render_editor();
}

static void execute_cli_command(const char *cmd) {
    kprintf("\n");
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') {
        print_prompt();
        flush_full_canvas_to_vram();
        return;
    }

    term_batch_mode = 1;

    if (kstrcmp(cmd, "help") == 0 || kstrcmp(cmd, "?") == 0) {
        kprintf("  [utils-in-c OS v2.6 - Comandos do Sistema (Fast Native)]\n");
        kprintf("    • calc_gui           : Abrir Calculadora Grafica Flutuante\n");
        kprintf("    • view <arquivo.bmp> : Visualizador de Imagens BMP\n");
        kprintf("    • snake / game       : Jogo retro Snake (Cobrinha)\n");
        kprintf("    • beep [freq] [ms]   : Emitir som no PC Speaker\n");
        kprintf("    • edit / nano <arq>  : Editor de texto visual de tela cheia\n");
        kprintf("    • top                : Monitor de processos em tempo real\n");
        kprintf("    • uptime             : Tempo de atividade do hardware\n");
        kprintf("    • ls [dir]           : Listar arquivos e pastas\n");
        kprintf("    • cd <dir>           : Navegar entre pastas\n");
        kprintf("    • pwd                : Exibir diretorio atual\n");
        kprintf("    • cat <arquivo>      : Exibir arquivo\n");
        kprintf("    • write <arq> <txt>  : Gravar no disco persistente\n");
        kprintf("    • touch <arquivo>    : Criar arquivo vazio\n");
        kprintf("    • mkdir <pasta>      : Criar diretorio\n");
        kprintf("    • rm <arquivo>       : Deletar arquivo\n");
        kprintf("    • whoami             : Exibir usuario atual\n");
        kprintf("    • su <usuario> [pwd] : Alternar usuario\n");
        kprintf("    • adduser <usr> <pwd>: Criar novo usuario\n");
        kprintf("    • ata [info|0]       : Diagnostico do disco ATA PIO\n");
        kprintf("    • format_disk        : Formatar disk.img com KSFS\n");
        kprintf("    • mem                : Memoria heap KMEM\n");
        kprintf("    • clear              : Limpar tela\n");
        kprintf("    • exit               : Voltar ao modo GUI\n");
    } else if (kstrcmp(cmd, "calc_gui") == 0 || kstrcmp(cmd, "calc") == 0) {
        term_batch_mode = 0;
        os_mode = 5;
        render_calc_window();
        return;
    } else if (kstrncmp(cmd, "view ", 5) == 0) {
        const char *t = cmd + 5;
        while (*t == ' ') t++;
        if (*t) {
            resolve_path(t, view_bmp_path, sizeof(view_bmp_path));
            term_batch_mode = 0;
            os_mode = 6;
            render_bmp_view();
        }
        return;
    } else if (kstrcmp(cmd, "snake") == 0 || kstrcmp(cmd, "game") == 0) {
        term_batch_mode = 0;
        start_snake_game();
        return;
    } else if (kstrncmp(cmd, "beep", 4) == 0) {
        const char *arg = cmd + 4;
        while (*arg == ' ') arg++;
        uint32_t freq = 880;
        uint32_t ms = 15;
        if (*arg) {
            freq = (uint32_t)katoi(arg);
            const char *sp = kstrchr(arg, ' ');
            if (sp) ms = (uint32_t)katoi(sp + 1);
        }
        if (freq == 0) freq = 880;
        ksound_beep(freq, ms, pit_get_ticks());
        kprintf("  Beep: %u Hz (%u ticks)\n", (unsigned int)freq, (unsigned int)ms);
    } else if (kstrncmp(cmd, "edit ", 5) == 0 || kstrncmp(cmd, "nano ", 5) == 0) {
        const char *t = cmd + 5;
        while (*t == ' ') t++;
        term_batch_mode = 0;
        if (*t) open_editor_file(t);
        return;
    } else if (kstrcmp(cmd, "top") == 0) {
        term_batch_mode = 0;
        os_mode = 3;
        render_top();
        return;
    } else if (kstrcmp(cmd, "uptime") == 0) {
        uint32_t secs = pit_get_seconds();
        kprintf("  Uptime: %u segundos (%02u:%02u min)\n", (unsigned int)secs, (unsigned int)(secs / 60), (unsigned int)(secs % 60));
    } else if (kstrcmp(cmd, "whoami") == 0) {
        kprintf("  %s (UID: %d, %s)\n", current_user, is_root ? 0 : 1000, is_root ? "Superuser" : "Standard User");
    } else if (kstrncmp(cmd, "su", 2) == 0 && (cmd[2] == ' ' || cmd[2] == '\0')) {
        const char *target = cmd + 2;
        while (*target == ' ') target++;
        if (*target == '\0') target = "root";

        char user[32] = "", pass[32] = "";
        const char *space = kstrchr(target, ' ');
        if (space) {
            size_t ulen = space - target;
            if (ulen < sizeof(user)) {
                kstrncpy(user, target, ulen);
                user[ulen] = '\0';
            }
            kstrncpy(pass, space + 1, sizeof(pass) - 1);
        } else {
            kstrncpy(user, target, sizeof(user) - 1);
            kstrncpy(pass, (kstrcmp(user, "root") == 0) ? "root" : "1234", sizeof(pass) - 1);
        }

        if (verify_user_password(user, pass)) {
            kstrncpy(current_user, user, sizeof(current_user) - 1);
            is_root = (kstrcmp(user, "root") == 0);
            if (is_root) {
                kstrncpy(current_dir, "/", sizeof(current_dir) - 1);
            } else {
                ksnprintf(current_dir, sizeof(current_dir), "/home/%s", user);
                if (!kvfs_open(current_dir)) kvfs_mkdir(current_dir);
            }
            kprintf("  Logged in as '%s'\n", current_user);
        } else {
            kprintf("  su: Authentication failure\n");
        }
    } else if (kstrncmp(cmd, "adduser ", 8) == 0) {
        if (!is_root) {
            kprintf("  adduser: Permission denied (only root can add users)\n");
        } else {
            const char *args = cmd + 8;
            while (*args == ' ') args++;
            const char *space = kstrchr(args, ' ');
            if (!space) {
                kprintf("  Usage: adduser <username> <password>\n");
            } else {
                char new_user[32] = "", new_pass[32] = "";
                size_t ulen = space - args;
                if (ulen < sizeof(new_user)) {
                    kstrncpy(new_user, args, ulen);
                    new_user[ulen] = '\0';
                }
                kstrncpy(new_pass, space + 1, sizeof(new_pass) - 1);

                char new_entry[128];
                ksnprintf(new_entry, sizeof(new_entry), "%s:%s:1001:1001:%s:/home/%s:/bin/sh\n", new_user, new_pass, new_user, new_user);

                const kvfs_node_t *pf = kvfs_open("/etc/passwd");
                char updated_passwd[1024] = "";
                if (pf && pf->data) kstrncpy(updated_passwd, (const char *)pf->data, sizeof(updated_passwd) - 1);
                kstrncat(updated_passwd, new_entry, sizeof(updated_passwd) - kstrlen(updated_passwd) - 1);

                kvfs_write("/etc/passwd", updated_passwd, kstrlen(updated_passwd));
                kdiskfs_save_file("/etc/passwd", updated_passwd, kstrlen(updated_passwd), KVFS_TYPE_FILE, 0644);

                char home_dir[64];
                ksnprintf(home_dir, sizeof(home_dir), "/home/%s", new_user);
                kvfs_mkdir(home_dir);
                kdiskfs_save_file(home_dir, NULL, 0, KVFS_TYPE_DIR, 0755);

                kprintf("  User '%s' created and saved to disk (/etc/passwd & %s)\n", new_user, home_dir);
            }
        }
    } else if (kstrncmp(cmd, "write ", 6) == 0) {
        const char *args = cmd + 6;
        while (*args == ' ') args++;
        const char *space = kstrchr(args, ' ');
        if (!space) {
            kprintf("  Usage: write <filepath> <text>\n");
        } else {
            char target_file[KVFS_MAX_PATH] = "";
            size_t flen = space - args;
            if (flen < sizeof(target_file)) {
                kstrncpy(target_file, args, flen);
                target_file[flen] = '\0';
            }
            char resolved[KVFS_MAX_PATH];
            resolve_path(target_file, resolved, sizeof(resolved));

            const char *text = space + 1;
            size_t tlen = kstrlen(text);

            kvfs_write(resolved, text, tlen);
            kdiskfs_save_file(resolved, text, tlen, KVFS_TYPE_FILE, 0644);

            kprintf("  [✔] File '%s' written & persisted to disk (%u bytes)\n", resolved, (unsigned int)tlen);
        }
    } else if (kstrncmp(cmd, "touch ", 6) == 0) {
        const char *target = cmd + 6;
        while (*target == ' ') target++;
        char resolved[KVFS_MAX_PATH];
        resolve_path(target, resolved, sizeof(resolved));

        kvfs_write(resolved, "", 0);
        kdiskfs_save_file(resolved, "", 0, KVFS_TYPE_FILE, 0644);
        kprintf("  [✔] File '%s' created on disk\n", resolved);
    } else if (kstrncmp(cmd, "mkdir ", 6) == 0) {
        const char *target = cmd + 6;
        while (*target == ' ') target++;
        char resolved[KVFS_MAX_PATH];
        resolve_path(target, resolved, sizeof(resolved));

        kvfs_mkdir(resolved);
        kdiskfs_save_file(resolved, NULL, 0, KVFS_TYPE_DIR, 0755);
        kprintf("  [✔] Directory '%s' created on disk\n", resolved);
    } else if (kstrncmp(cmd, "rm ", 3) == 0) {
        const char *target = cmd + 3;
        while (*target == ' ') target++;
        char resolved[KVFS_MAX_PATH];
        resolve_path(target, resolved, sizeof(resolved));

        kvfs_delete(resolved);
        kdiskfs_delete_file(resolved);
        kprintf("  [✔] File '%s' deleted from disk\n", resolved);
    } else if (kstrcmp(cmd, "format_disk") == 0) {
        if (!is_root) {
            kprintf("  format_disk: Permission denied (must be root)\n");
        } else {
            if (kdiskfs_format() == 0) {
                kprintf("  [✔] disk.img formatted with fresh KSFS Superblock!\n");
            } else {
                kprintf("  format_disk: Error communicating with ATA drive\n");
            }
        }
    } else if (kstrncmp(cmd, "cd", 2) == 0 && (cmd[2] == ' ' || cmd[2] == '\0')) {
        const char *target = cmd + 2;
        while (*target == ' ') target++;

        if (*target == '\0' || kstrcmp(target, "/") == 0) {
            kstrncpy(current_dir, "/", sizeof(current_dir) - 1);
        } else {
            char resolved[KVFS_MAX_PATH];
            resolve_path(target, resolved, sizeof(resolved));

            const kvfs_node_t *dir = kvfs_open(resolved);
            if (dir && dir->type == KVFS_TYPE_DIR) {
                kstrncpy(current_dir, resolved, sizeof(current_dir) - 1);
            } else {
                kprintf("  cd: '%s': No such directory\n", target);
            }
        }
    } else if (kstrcmp(cmd, "pwd") == 0) {
        kprintf("  %s\n", current_dir);
    } else if (kstrncmp(cmd, "ls", 2) == 0 && (cmd[2] == ' ' || cmd[2] == '\0')) {
        const char *target = cmd + 2;
        while (*target == ' ') target++;

        char resolved[KVFS_MAX_PATH];
        resolve_path(*target ? target : "", resolved, sizeof(resolved));

        kprintf("  [Directory Listing of %s]:\n", resolved);
        kvfs_list_dir(resolved, vfs_ls_callback);
    } else if (kstrncmp(cmd, "cat ", 4) == 0) {
        const char *fname = cmd + 4;
        while (*fname == ' ') fname++;

        char resolved[KVFS_MAX_PATH];
        resolve_path(fname, resolved, sizeof(resolved));

        const kvfs_node_t *file = kvfs_open(resolved);
        if (file) {
            kprintf("  [Content of %s]:\n", file->path);
            if (file->data && file->size > 0) {
                kprintf("%s\n", (const char *)file->data);
            } else if (file->type == KVFS_TYPE_DIR) {
                kprintf("  (Directory Node)\n");
            } else {
                kprintf("  (Empty File)\n");
            }
        } else {
            kprintf("  cat: '%s': No such file or directory\n", fname);
        }
    } else if (kstrncmp(cmd, "ata", 3) == 0 && (cmd[3] == ' ' || cmd[3] == '\0')) {
        const char *arg = cmd + 3;
        while (*arg == ' ') arg++;

        const kata_drive_info_t *d = kata_get_info();
        if (!d || !d->drive_present) {
            kprintf("  ATA Driver: Primary Master drive offline or not attached.\n");
        } else {
            kprintf("  [ATA PIO Primary Master Drive]:\n");
            kprintf("    • Model   : %s\n", d->model);
            kprintf("    • Serial  : %s\n", d->serial);
            kprintf("    • Capacity: %u MB (%u sectors)\n", (unsigned int)d->size_mb, (unsigned int)d->total_sectors);

            if (kstrncmp(arg, "read", 4) == 0 || kstrcmp(arg, "0") == 0) {
                uint8_t sec_buf[512];
                if (kata_read_sector(0, sec_buf) == 0) {
                    kprintf("    • Sector 0 (MBR) read successfully! Signature: 0x%02X 0x%02X\n", sec_buf[510], sec_buf[511]);
                } else {
                    kprintf("    • Error reading sector 0.\n");
                }
            }
        }
    } else if (kstrncmp(cmd, "echo ", 5) == 0) {
        kprintf("  %s\n", cmd + 5);
    } else if (kstrcmp(cmd, "mem") == 0) {
        kprintf("  [KMEM Heap Diagnostics]:\n");
        kprintf("    • Total Memory : %u KB\n", (unsigned int)(kmem_get_total_bytes() / 1024));
        kprintf("    • Free Memory  : %u KB\n", (unsigned int)(kmem_get_free_bytes() / 1024));
        kprintf("    • Used Memory  : %u bytes\n", (unsigned int)kmem_get_used_bytes());
    } else if (kstrcmp(cmd, "clear") == 0) {
        kmemset(term_canvas, 0, sizeof(term_canvas));
        term_col = 0;
        term_row = 0;
        kgfx_clear(&os_fb, KGFX_BLACK);
        kgfx_draw_rounded_rect(&os_fb, 10, 10, os_fb.width - 20, os_fb.height - 20, 8, KGFX_CYAN, 0);
        kprintf("[Interactive Kernel CLI Terminal - Press ESC to Return to GUI]\n\n");
    } else if (kstrcmp(cmd, "exit") == 0) {
        term_batch_mode = 0;
        os_mode = 0;
        mouse_under_saved = 0;
        draw_gui_desktop();
        return;
    } else {
        kprintf("  Unknown command '%s'. Type 'help' for command list.\n", cmd);
    }

    print_prompt();

    term_batch_mode = 0;
    flush_full_canvas_to_vram();
}

void os_handle_keypress(char c) {
    if (os_mode == 1) {
        if (c == '\b') {
            if (cli_pos > 0) {
                cli_input[--cli_pos] = '\0';
                os_putchar('\b');
            }
        } else if (c == '\n') {
            execute_cli_command(cli_input);
            cli_pos = 0;
            cli_input[0] = '\0';
        } else if (c && cli_pos < sizeof(cli_input) - 1) {
            cli_input[cli_pos++] = c;
            cli_input[cli_pos] = '\0';
            os_putchar(c);
        }
    } else if (os_mode == 2) {
        if (c == '\b') {
            if (edit_len > 0) {
                edit_buf[--edit_len] = '\0';
                render_editor();
            }
        } else if (c == '\n') {
            if (edit_len < EDIT_BUF_SZ - 1) {
                edit_buf[edit_len++] = '\n';
                edit_buf[edit_len] = '\0';
                render_editor();
            }
        } else if (c == 19) {
            kvfs_write(edit_filepath, edit_buf, edit_len);
            kdiskfs_save_file(edit_filepath, edit_buf, edit_len, KVFS_TYPE_FILE, 0644);
            kstrncpy(edit_status_msg, "[SALVO NO DISCO!]", sizeof(edit_status_msg) - 1);
            ksound_beep(1200, 6, pit_get_ticks());
            render_editor();
        } else if (c == 17 || c == 27) {
            os_mode = 1;
            term_col = 0;
            term_row = 0;
            kmemset(term_canvas, 0, sizeof(term_canvas));
            kgfx_clear(&os_fb, KGFX_BLACK);
            kgfx_draw_rounded_rect(&os_fb, 10, 10, os_fb.width - 20, os_fb.height - 20, 8, KGFX_CYAN, 0);
            kprintf("\n[Exited KEDIT - Returned to Shell]\n\n");
            print_prompt();
            flush_full_canvas_to_vram();
        } else if ((unsigned char)c >= 32 && edit_len < EDIT_BUF_SZ - 1) {
            edit_buf[edit_len++] = c;
            edit_buf[edit_len] = '\0';
            render_editor();
        }
    } else if (os_mode == 3) {
        if (c == 'q' || c == 'Q' || c == 27) {
            os_mode = 1;
            term_col = 0;
            term_row = 0;
            kmemset(term_canvas, 0, sizeof(term_canvas));
            kgfx_clear(&os_fb, KGFX_BLACK);
            kgfx_draw_rounded_rect(&os_fb, 10, 10, os_fb.width - 20, os_fb.height - 20, 8, KGFX_CYAN, 0);
            kprintf("\n[Exited top - Returned to Shell]\n\n");
            print_prompt();
            flush_full_canvas_to_vram();
        }
    } else if (os_mode == 4) {
        if (c == 'w' || c == 'W') {
            if (snake_dir_y == 0) { snake_dir_x = 0; snake_dir_y = -1; }
        } else if (c == 's' || c == 'S') {
            if (snake_dir_y == 0) { snake_dir_x = 0; snake_dir_y = 1; }
        } else if (c == 'a' || c == 'A') {
            if (snake_dir_x == 0) { snake_dir_x = -1; snake_dir_y = 0; }
        } else if (c == 'd' || c == 'D') {
            if (snake_dir_x == 0) { snake_dir_x = 1; snake_dir_y = 0; }
        } else if (c == ' ') {
            if (snake_gameover) start_snake_game();
        } else if (c == 27 || c == 'q' || c == 'Q') {
            os_mode = 0;
            mouse_under_saved = 0;
            draw_gui_desktop();
        }
    } else if (os_mode == 5) {
        if (c == 27) {
            os_mode = 0;
            mouse_under_saved = 0;
            draw_gui_desktop();
        } else if (c >= '0' && c <= '9') {
            char s[2] = {c, '\0'};
            handle_calc_click(s);
        } else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^' || c == '=' || c == 'c' || c == 'C') {
            char s[2] = {c == 'c' ? 'C' : c, '\0'};
            handle_calc_click(s);
        } else if (c == '\n') {
            handle_calc_click("=");
        }
    } else if (os_mode == 6) {
        if (c == 27 || c == 'q' || c == 'Q') {
            os_mode = 1;
            term_col = 0;
            term_row = 0;
            kmemset(term_canvas, 0, sizeof(term_canvas));
            kgfx_clear(&os_fb, KGFX_BLACK);
            kgfx_draw_rounded_rect(&os_fb, 10, 10, os_fb.width - 20, os_fb.height - 20, 8, KGFX_CYAN, 0);
            kprintf("\n[Exited BMP View - Returned to Shell]\n\n");
            print_prompt();
            flush_full_canvas_to_vram();
        }
    }
}

void os_toggle_cli_mode(void) {
    if (os_mode == 2 || os_mode == 3 || os_mode == 4 || os_mode == 5 || os_mode == 6) {
        os_mode = 0;
        mouse_under_saved = 0;
        draw_gui_desktop();
        return;
    }

    os_mode = !os_mode;
    cli_pos = 0;
    cli_input[0] = '\0';
    mouse_under_saved = 0;

    if (os_mode == 1) {
        term_col = 0;
        term_row = 0;
        kmemset(term_canvas, 0, sizeof(term_canvas));
        kgfx_clear(&os_fb, KGFX_BLACK);
        kgfx_draw_rounded_rect(&os_fb, 10, 10, os_fb.width - 20, os_fb.height - 20, 8, KGFX_CYAN, 0);
        kprintf("[Interactive Kernel CLI Terminal Active - Type 'help' or press ESC to exit]\n\n");
        print_prompt();
        flush_full_canvas_to_vram();
    } else {
        draw_gui_desktop();
    }
}

void kernel_main(uint32_t magic, multiboot_info_t *mb_info) {
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC || !mb_info) return;

    uint32_t *fb_ptr = (uint32_t *)(uintptr_t)mb_info->framebuffer_addr;
    uint32_t width = mb_info->framebuffer_width ? mb_info->framebuffer_width : 800;
    uint32_t height = mb_info->framebuffer_height ? mb_info->framebuffer_height : 600;

    if (!fb_ptr) fb_ptr = (uint32_t *)0xFD000000;

    kgfx_init(&os_fb, fb_ptr, width, height);
    kset_putchar(os_putchar);

    static uint8_t os_heap_pool[2 * 1024 * 1024];
    kmem_init(os_heap_pool, sizeof(os_heap_pool));

    kmemset(term_canvas, 0, sizeof(term_canvas));
    term_col = 0;
    term_row = 0;

    kvfs_init();
    gdt_init();
    idt_init();
    ps2_init();
    kata_init();
    kdiskfs_mount();
    ksound_init();

    draw_gui_desktop();

    while (1) {
        uint32_t cur_ticks = pit_get_ticks();
        ksound_update(cur_ticks);

        if (os_mode == 0 && toast_active && cur_ticks >= toast_expire_tick) {
            toast_active = 0;
            draw_toast_notification(0);
        }

        if (os_mode == 4) {
            update_snake_game();
        }

        if (os_mode == 0 || os_mode == 5) {
            os_draw_mouse_cursor();
        }

        __asm__ __volatile__ ("hlt");
    }
}
