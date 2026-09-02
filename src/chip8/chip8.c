#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include "../libutilipc/utilipc.h"

#define MEMORY_SIZE   4096
#define V_REG_COUNT   16
#define STACK_SIZE    16
#define GFX_WIDTH     64
#define GFX_HEIGHT    32
#define KEY_COUNT     16
#define FONTSET_START 0x50
#define PROGRAM_START 0x200

#define COLOR_RESET   "\033[0m"
#define COLOR_PIXEL   "\033[1;32m██\033[0m" // Pixel verde
#define COLOR_BG      "\033[0;90m░░\033[0m" // Pixel apagado
#define COLOR_BORDER  "\033[1;35m"

static const uint8_t chip8_fontset[80] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

// ROM Embutida: Gerador de Labirintos do Chip-8 (Maze Demo)
static const uint8_t default_maze_rom[] = {
    0x60, 0x00, 0x61, 0x00, 0xA2, 0x22, 0xC2, 0x01,
    0x32, 0x01, 0xA2, 0x1E, 0xD0, 0x14, 0x70, 0x04,
    0x30, 0x40, 0x12, 0x04, 0x60, 0x00, 0x71, 0x04,
    0x31, 0x20, 0x12, 0x04, 0x12, 0x1C, 0x80, 0x40,
    0x20, 0x10, 0x20, 0x40, 0x80, 0x10
};

typedef struct {
    uint8_t  memory[MEMORY_SIZE];
    uint8_t  V[V_REG_COUNT];
    uint16_t I;
    uint16_t pc;
    uint8_t  gfx[GFX_WIDTH * GFX_HEIGHT];
    uint8_t  delay_timer;
    uint8_t  sound_timer;
    uint16_t stack[STACK_SIZE];
    uint16_t sp;
    uint8_t  key[KEY_COUNT];
    int      draw_flag;
} Chip8;

static Chip8 cpu;
static struct termios orig_termios;
static volatile int keep_running = 1;

static void restore_term(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    printf("\033[?1049l\033[?25h\033[0m\033[H\033[J");
    fflush(stdout);
    utilipc_close();
}

static void sig_handler(int sig) {
    (void)sig;
    keep_running = 0;
}

static void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(restore_term);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    printf("\033[?1049h\033[?25l\033[H\033[J");
    fflush(stdout);
}

static void chip8_init(void) {
    memset(&cpu, 0, sizeof(Chip8));
    cpu.pc = PROGRAM_START;
    memcpy(&cpu.memory[FONTSET_START], chip8_fontset, sizeof(chip8_fontset));
    cpu.draw_flag = 1;
}

static int chip8_load_rom(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return -1;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0 || size > (MEMORY_SIZE - PROGRAM_START)) {
        fclose(fp);
        return -1;
    }

    size_t r = fread(&cpu.memory[PROGRAM_START], 1, size, fp);
    fclose(fp);
    return (r > 0) ? 0 : -1;
}

static void chip8_cycle(void) {
    uint16_t opcode = (cpu.memory[cpu.pc] << 8) | cpu.memory[cpu.pc + 1];
    uint16_t nnn = opcode & 0x0FFF;
    uint8_t  n   = opcode & 0x000F;
    uint8_t  x   = (opcode & 0x0F00) >> 8;
    uint8_t  y   = (opcode & 0x00F0) >> 4;
    uint8_t  kk  = opcode & 0x00FF;

    cpu.pc += 2;

    switch (opcode & 0xF000) {
        case 0x0000:
            if (opcode == 0x00E0) { // CLS
                memset(cpu.gfx, 0, sizeof(cpu.gfx));
                cpu.draw_flag = 1;
            } else if (opcode == 0x00EE) { // RET
                if (cpu.sp > 0) cpu.pc = cpu.stack[--cpu.sp];
            }
            break;

        case 0x1000: // JP addr
            cpu.pc = nnn;
            break;

        case 0x2000: // CALL addr
            if (cpu.sp < STACK_SIZE) {
                cpu.stack[cpu.sp++] = cpu.pc;
                cpu.pc = nnn;
            }
            break;

        case 0x3000: // SE Vx, byte
            if (cpu.V[x] == kk) cpu.pc += 2;
            break;

        case 0x4000: // SNE Vx, byte
            if (cpu.V[x] != kk) cpu.pc += 2;
            break;

        case 0x5000: // SE Vx, Vy
            if (cpu.V[x] == cpu.V[y]) cpu.pc += 2;
            break;

        case 0x6000: // LD Vx, byte
            cpu.V[x] = kk;
            break;

        case 0x7000: // ADD Vx, byte
            cpu.V[x] += kk;
            break;

        case 0x8000:
            switch (n) {
                case 0x0: cpu.V[x] = cpu.V[y]; break;
                case 0x1: cpu.V[x] |= cpu.V[y]; break;
                case 0x2: cpu.V[x] &= cpu.V[y]; break;
                case 0x3: cpu.V[x] ^= cpu.V[y]; break;
                case 0x4: {
                    uint16_t sum = cpu.V[x] + cpu.V[y];
                    cpu.V[0xF] = (sum > 255) ? 1 : 0;
                    cpu.V[x] = (uint8_t)(sum & 0xFF);
                    break;
                }
                case 0x5:
                    cpu.V[0xF] = (cpu.V[x] >= cpu.V[y]) ? 1 : 0;
                    cpu.V[x] -= cpu.V[y];
                    break;
                case 0x6:
                    cpu.V[0xF] = cpu.V[x] & 1;
                    cpu.V[x] >>= 1;
                    break;
                case 0x7:
                    cpu.V[0xF] = (cpu.V[y] >= cpu.V[x]) ? 1 : 0;
                    cpu.V[x] = cpu.V[y] - cpu.V[x];
                    break;
                case 0xE:
                    cpu.V[0xF] = (cpu.V[x] >> 7) & 1;
                    cpu.V[x] <<= 1;
                    break;
            }
            break;

        case 0x9000: // SNE Vx, Vy
            if (cpu.V[x] != cpu.V[y]) cpu.pc += 2;
            break;

        case 0xA000: // LD I, addr
            cpu.I = nnn;
            break;

        case 0xB000: // JP V0, addr
            cpu.pc = nnn + cpu.V[0];
            break;

        case 0xC000: // RND Vx, byte
            cpu.V[x] = (rand() % 256) & kk;
            break;

        case 0xD000: { // DRW Vx, Vy, nibble
            uint8_t vx = cpu.V[x] % GFX_WIDTH;
            uint8_t vy = cpu.V[y] % GFX_HEIGHT;
            cpu.V[0xF] = 0;

            for (int row = 0; row < n; row++) {
                if (vy + row >= GFX_HEIGHT) break;
                uint8_t sprite_byte = cpu.memory[cpu.I + row];

                for (int col = 0; col < 8; col++) {
                    if (vx + col >= GFX_WIDTH) break;
                    if (sprite_byte & (0x80 >> col)) {
                        int idx = (vy + row) * GFX_WIDTH + (vx + col);
                        if (cpu.gfx[idx] == 1) cpu.V[0xF] = 1;
                        cpu.gfx[idx] ^= 1;
                    }
                }
            }
            cpu.draw_flag = 1;
            break;
        }

        case 0xE000:
            if (kk == 0x9E) { // SKP Vx
                if (cpu.key[cpu.V[x] & 0xF]) cpu.pc += 2;
            } else if (kk == 0xA1) { // SKNP Vx
                if (!cpu.key[cpu.V[x] & 0xF]) cpu.pc += 2;
            }
            break;

        case 0xF000:
            switch (kk) {
                case 0x07: cpu.V[x] = cpu.delay_timer; break;
                case 0x0A: { // Wait for key
                    int key_pressed = -1;
                    for (int k = 0; k < KEY_COUNT; k++) {
                        if (cpu.key[k]) { key_pressed = k; break; }
                    }
                    if (key_pressed >= 0) cpu.V[x] = (uint8_t)key_pressed;
                    else cpu.pc -= 2;
                    break;
                }
                case 0x15: cpu.delay_timer = cpu.V[x]; break;
                case 0x18: cpu.sound_timer = cpu.V[x]; break;
                case 0x1E: cpu.I += cpu.V[x]; break;
                case 0x29: cpu.I = FONTSET_START + (cpu.V[x] * 5); break;
                case 0x33:
                    cpu.memory[cpu.I]     = cpu.V[x] / 100;
                    cpu.memory[cpu.I + 1] = (cpu.V[x] / 10) % 10;
                    cpu.memory[cpu.I + 2] = cpu.V[x] % 10;
                    break;
                case 0x55:
                    for (int k = 0; k <= x; k++) cpu.memory[cpu.I + k] = cpu.V[k];
                    break;
                case 0x65:
                    for (int k = 0; k <= x; k++) cpu.V[k] = cpu.memory[cpu.I + k];
                    break;
            }
            break;
    }
}

static void map_key(char c, int state) {
    int k = -1;
    switch (c) {
        case '1': k = 0x1; break; case '2': k = 0x2; break; case '3': k = 0x3; break; case '4': k = 0xC; break;
        case 'q': case 'Q': k = 0x4; break; case 'w': case 'W': k = 0x5; break; case 'e': case 'E': k = 0x6; break; case 'r': case 'R': k = 0xD; break;
        case 'a': case 'A': k = 0x7; break; case 's': case 'S': k = 0x8; break; case 'd': case 'D': k = 0x9; break; case 'f': case 'F': k = 0xE; break;
        case 'z': case 'Z': k = 0xA; break; case 'x': case 'X': k = 0x0; break; case 'c': case 'C': k = 0xB; break; case 'v': case 'V': k = 0xF; break;
    }
    if (k >= 0) cpu.key[k] = state;
}

static void render_screen(const char *rom_name) {
    if (!cpu.draw_flag) return;
    cpu.draw_flag = 0;

    printf("\033[H");
    printf("%s╭────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────╮%s\r\n", COLOR_BORDER, COLOR_RESET);
    printf("%s│%s \033[1;37m[ chip8 - 8-Bit VM Emulator ]\033[0m ROM: \033[1;36m%-20.20s\033[0m PC: 0x%04X | SP: %d | Timers: DT=%-2d ST=%-2d %s│%s\r\n",
           COLOR_BORDER, COLOR_RESET, rom_name, cpu.pc, cpu.sp, cpu.delay_timer, cpu.sound_timer, COLOR_BORDER, COLOR_RESET);
    printf("%s├────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤%s\r\n", COLOR_BORDER, COLOR_RESET);

    for (int y = 0; y < GFX_HEIGHT; y++) {
        printf("%s│%s ", COLOR_BORDER, COLOR_RESET);
        for (int x = 0; x < GFX_WIDTH; x++) {
            if (cpu.gfx[y * GFX_WIDTH + x]) printf(COLOR_PIXEL);
            else printf(COLOR_BG);
        }
        printf(" %s│%s\r\n", COLOR_BORDER, COLOR_RESET);
    }

    printf("%s├────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤%s\r\n", COLOR_BORDER, COLOR_RESET);
    printf("%s│%s Teclado Hex: [1 2 3 4] / [Q W E R] / [A S D F] / [Z X C V] | \033[1;33mPressione '0' ou ESC para Sair\033[0m                           %s│%s\r\n", COLOR_BORDER, COLOR_RESET, COLOR_BORDER, COLOR_RESET);
    printf("%s╰────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────╯%s", COLOR_BORDER, COLOR_RESET);
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    utilipc_init();
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    const char *rom_name = "Embedded Maze Generator";
    chip8_init();

    if (argc >= 2) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            printf("Usage: ./chip8 [ROM_FILE.ch8]\n");
            return 0;
        }
        if (chip8_load_rom(argv[1]) < 0) {
            fprintf(stderr, "chip8: erro ao carregar ROM '%s'\n", argv[1]);
            return 1;
        }
        rom_name = argv[1];
    } else {
        memcpy(&cpu.memory[PROGRAM_START], default_maze_rom, sizeof(default_maze_rom));
    }

    enable_raw_mode();
    srand(time(NULL));

    struct timespec last_timer_update;
    clock_gettime(CLOCK_MONOTONIC, &last_timer_update);

    while (keep_running) {
        for (int i = 0; i < 10; i++) {
            chip8_cycle();
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - last_timer_update.tv_sec) + (now.tv_nsec - last_timer_update.tv_nsec) / 1e9;

        if (elapsed >= 0.0166) {
            last_timer_update = now;
            if (cpu.delay_timer > 0) cpu.delay_timer--;
            if (cpu.sound_timer > 0) cpu.sound_timer--;
        }

        render_screen(rom_name);

        memset(cpu.key, 0, sizeof(cpu.key));
        char ch;
        while (read(STDIN_FILENO, &ch, 1) > 0) {
            if (ch == 27 || ch == '0' || ch == 3) {
                keep_running = 0;
                break;
            }
            map_key(ch, 1);
        }

        usleep(2000);
    }

    return 0;
}
