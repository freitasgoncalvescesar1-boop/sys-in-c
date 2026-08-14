#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <ctype.h>
#include <fcntl.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET     "\033[0m"
#define COLOR_OFFSET    "\033[1;33m"
#define COLOR_HEX       "\033[1;35m"
#define COLOR_BIN       "\033[1;32m"
#define COLOR_TEXT      "\033[1;36m"
#define COLOR_HEADER    "\033[1;37;44m"
#define COLOR_FOOTER    "\033[1;37;43m"

enum KeyCode {
    KEY_NONE = 0,
    KEY_CTRL_X = 24,
    KEY_CTRL_C = 3,
    KEY_UP = 1000,
    KEY_DOWN,
    KEY_PGUP,
    KEY_PGDN,
    KEY_HOME,
    KEY_END
};

static struct termios orig_termios;
static unsigned char *file_data = NULL;
static size_t file_size = 0;

static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    printf("\033[?25h\033[?1049l"); // Restaura cursor e tela principal
    fflush(stdout);
}

static void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    printf("\033[?1049h\033[?25l"); // Alterna tela buffer e esconde cursor
    fflush(stdout);
}

static int read_key(void) {
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) <= 0) return KEY_NONE;

    if (c == 27) { // Sequência ESC
        unsigned char seq[4];

        struct timeval tv = { .tv_sec = 0, .tv_usec = 50000 }; // 50ms timeout
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);

        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0) return 27;

        if (read(STDIN_FILENO, &seq[0], 1) <= 0) return 27;
        if (read(STDIN_FILENO, &seq[1], 1) <= 0) return 27;

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) <= 0) return 27;
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '5': return KEY_PGUP;
                        case '6': return KEY_PGDN;
                        case '1': case '7': return KEY_HOME;
                        case '4': case '8': return KEY_END;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return KEY_UP;
                    case 'B': return KEY_DOWN;
                    case 'H': return KEY_HOME;
                    case 'F': return KEY_END;
                }
            }
        }
        return 27;
    }

    return c;
}

static void byte_to_binary(unsigned char byte, char *out) {
    for (int i = 7; i >= 0; i--) {
        out[7 - i] = (byte & (1 << i)) ? '1' : '0';
    }
    out[8] = '\0';
}

static void get_terminal_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
}

static void render_ui(size_t scroll_row, const char *filename) {
    int term_rows, term_cols;
    get_terminal_size(&term_rows, &term_cols);

    int visible_data_rows = term_rows - 3;
    if (visible_data_rows < 1) visible_data_rows = 1;

    int bytes_per_row = (term_cols >= 115) ? 8 : 4;

    // Posiciona no topo sem apagar a tela inteira (elimina o piscar da tela)
    printf("\033[H");

    // 1. Cabeçalho estilo Nano
    int pad_len = term_cols - 35 - (int)strlen(filename);
    if (pad_len < 0) pad_len = 0;
    printf("%s DEVIEW [ReadOnly] - %s (%zu bytes) %*s%s\033[K\r\n", 
           COLOR_HEADER, filename, file_size, pad_len, "", COLOR_RESET);

    // 2. Colunas de Dados
    size_t total_rows = (file_size + bytes_per_row - 1) / bytes_per_row;

    for (int r = 0; r < visible_data_rows; r++) {
        size_t row_idx = scroll_row + r;
        size_t offset = row_idx * bytes_per_row;

        if (offset >= file_size) {
            printf("\033[K\r\n");
            continue;
        }

        // Offset
        printf("%s%08zX%s | ", COLOR_OFFSET, offset, COLOR_RESET);

        // Hexadecimal
        printf("%s", COLOR_HEX);
        for (int b = 0; b < bytes_per_row; b++) {
            if (offset + b < file_size) {
                printf("%02X ", file_data[offset + b]);
            } else {
                printf("   ");
            }
        }
        printf("%s| ", COLOR_RESET);

        // Binário
        printf("%s", COLOR_BIN);
        for (int b = 0; b < bytes_per_row; b++) {
            if (offset + b < file_size) {
                char bin_str[9];
                byte_to_binary(file_data[offset + b], bin_str);
                printf("%s ", bin_str);
            } else {
                printf("         ");
            }
        }
        printf("%s| ", COLOR_RESET);

        // Decodificado (ASCII)
        printf("%s", COLOR_TEXT);
        for (int b = 0; b < bytes_per_row; b++) {
            if (offset + b < file_size) {
                unsigned char c = file_data[offset + b];
                printf("%c", isprint(c) ? c : '.');
            } else {
                printf(" ");
            }
        }
        printf("%s\033[K\r\n", COLOR_RESET);
    }

    // 3. Rodapé Estilo Nano
    int footer_pad = term_cols - 55;
    if (footer_pad < 0) footer_pad = 0;
    printf("%s ^X Sair | ↑/↓ Rolar | PgUp/PgDn Página | Row: %zu/%zu %*s%s\033[K", 
           COLOR_FOOTER, scroll_row + 1, total_rows, footer_pad, "", COLOR_RESET);

    fflush(stdout);
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 2) {
        printf("Usage: %s <file_path>\n", argv[0]);
        utilipc_close();
        return 1;
    }

    const char *filename = argv[1];
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Error opening file");
        utilipc_close();
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz <= 0) {
        printf("File is empty or invalid.\n");
        fclose(fp);
        utilipc_close();
        return 0;
    }
    file_size = (size_t)sz;

    file_data = malloc(file_size);
    if (!file_data) {
        printf("Error: Out of memory.\n");
        fclose(fp);
        utilipc_close();
        return 1;
    }

    fread(file_data, 1, file_size, fp);
    fclose(fp);

    enable_raw_mode();

    size_t scroll_row = 0;

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "deview: inspecting %s (%zu bytes)", filename, file_size);
    utilipc_write_status(-1, -1, -1, log_msg);

    while (1) {
        int term_rows, term_cols;
        get_terminal_size(&term_rows, &term_cols);
        int bytes_per_row = (term_cols >= 115) ? 8 : 4;
        size_t total_rows = (file_size + bytes_per_row - 1) / bytes_per_row;
        int page_size = term_rows - 3;
        if (page_size < 1) page_size = 1;

        render_ui(scroll_row, filename);

        int k = read_key();

        if (k == KEY_CTRL_X || k == KEY_CTRL_C || k == 'q' || k == 'Q') {
            break;
        } else if (k == KEY_UP) {
            if (scroll_row > 0) scroll_row--;
        } else if (k == KEY_DOWN) {
            if (scroll_row + 1 < total_rows) scroll_row++;
        } else if (k == KEY_PGUP) {
            if (scroll_row > (size_t)page_size) scroll_row -= page_size;
            else scroll_row = 0;
        } else if (k == KEY_PGDN) {
            if (scroll_row + page_size < total_rows) scroll_row += page_size;
            else scroll_row = (total_rows > (size_t)page_size) ? total_rows - page_size : 0;
        } else if (k == KEY_HOME) {
            scroll_row = 0;
        } else if (k == KEY_END) {
            scroll_row = (total_rows > (size_t)page_size) ? total_rows - page_size : 0;
        }
    }

    free(file_data);
    utilipc_close();
    return 0;
}
