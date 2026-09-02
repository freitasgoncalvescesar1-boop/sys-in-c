#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <signal.h>
#include <time.h>
#include "../libutilipc/utilipc.h"

#define MAX_COLS 256
#define MAX_DURATION_SEC 10.0

typedef struct {
    int y;        // Posição Y da cabeça da gota
    int length;   // Comprimento da cauda
    int speed;    // Velocidade
    int delay;    // Contador de delay
    char head_c;  // Caractere da cabeça
} RainDrop;

static struct termios orig_termios;
static volatile int keep_running = 1;

static double get_time_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static void restore_terminal(void) {
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
    atexit(restore_terminal);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    printf("\033[?1049h\033[?25l\033[H\033[J");
    fflush(stdout);
}

static char get_matrix_char(void) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%&*+-=<>~";
    return charset[rand() % (sizeof(charset) - 1)];
}

int main(void) {
    utilipc_init();
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0) {
        ws.ws_col = 80;
        ws.ws_row = 24;
    }

    int cols = ws.ws_col > MAX_COLS ? MAX_COLS : ws.ws_col;
    int rows = ws.ws_row;

    RainDrop drops[MAX_COLS];
    srand(time(NULL));

    for (int c = 0; c < cols; c++) {
        drops[c].y = -(rand() % rows);
        drops[c].length = (rand() % (rows / 2)) + 4;
        drops[c].speed = (rand() % 2) + 1;
        drops[c].delay = 0;
        drops[c].head_c = get_matrix_char();
    }

    enable_raw_mode();

    double start_time = get_time_sec();

    while (keep_running) {
        double elapsed = get_time_sec() - start_time;
        if (elapsed >= MAX_DURATION_SEC) break; // Trava máxima de 10 segundos!

        // Atualiza a cada coluna
        for (int c = 0; c < cols; c += 2) {
            drops[c].delay++;
            if (drops[c].delay < drops[c].speed) continue;
            drops[c].delay = 0;

            int head_y = drops[c].y;

            // 1. Apaga a cauda antiga
            int tail_y = head_y - drops[c].length;
            if (tail_y >= 1 && tail_y <= rows) {
                printf("\033[%d;%dH ", tail_y, c + 1);
            }

            // 2. Desenha a cauda verde
            for (int k = 1; k < drops[c].length; k++) {
                int body_y = head_y - k;
                if (body_y >= 1 && body_y <= rows) {
                    if (k < 3) printf("\033[%d;%dH\033[1;32m%c", body_y, c + 1, get_matrix_char());
                    else printf("\033[%d;%dH\033[0;32m%c", body_y, c + 1, get_matrix_char());
                }
            }

            // 3. Desenha a cabeça brilhante em branco
            if (head_y >= 1 && head_y <= rows) {
                printf("\033[%d;%dH\033[1;37m%c", head_y, c + 1, drops[c].head_c);
            }

            drops[c].head_c = get_matrix_char();
            drops[c].y++;

            // Reseta se a cauda sumir
            if (drops[c].y - drops[c].length > rows) {
                drops[c].y = 0;
                drops[c].length = (rand() % (rows / 2)) + 4;
                drops[c].speed = (rand() % 2) + 1;
            }
        }

        // Timer no canto superior direito
        int remaining = (int)(MAX_DURATION_SEC - elapsed);
        printf("\033[1;%dH\033[1;30;42m [ %ds | 'q' p/ Sair ] \033[0m", cols - 24, remaining + 1);
        fflush(stdout);

        // Verifica tecla para interrupção instantânea
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 33000 }; // ~30 FPS

        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            char ch;
            if (read(STDIN_FILENO, &ch, 1) > 0) {
                if (ch == 'q' || ch == 'Q' || ch == 27 || ch == 3) break;
            }
        }
    }

    return 0;
}
