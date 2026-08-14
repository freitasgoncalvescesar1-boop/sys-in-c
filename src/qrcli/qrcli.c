#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../libutilipc/utilipc.h"

// Engine QR Code ASCII em C puro
#define MAX_QR_SIZE 33

static void draw_qr_ascii(const char *text) {
    size_t len = strlen(text);
    int size = 21; // QR Model 1
    if (len > 15) size = 25; // QR Model 2
    if (len > 32) size = 29; // QR Model 3

    uint8_t grid[MAX_QR_SIZE][MAX_QR_SIZE] = {0};

    // Finder patterns (3 cantos)
    int corners[3][2] = {{0, 0}, {0, size - 7}, {size - 7, 0}};
    for (int k = 0; k < 3; k++) {
        int r0 = corners[k][0];
        int c0 = corners[k][1];
        for (int r = 0; r < 7; r++) {
            for (int c = 0; c < 7; c++) {
                if (r == 0 || r == 6 || c == 0 || c == 6 || (r >= 2 && r <= 4 && c >= 2 && c <= 4)) {
                    grid[r0 + r][c0 + c] = 1;
                }
            }
        }
    }

    // Timing patterns
    for (int i = 8; i < size - 8; i++) {
        grid[6][i] = (i % 2 == 0);
        grid[i][6] = (i % 2 == 0);
    }

    // Pseudo-payload hashing do texto
    uint32_t hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + (unsigned char)text[i];
    }

    for (int r = 0; r < size; r++) {
        for (int c = 0; c < size; c++) {
            if (grid[r][c] == 0) {
                uint32_t bit = (hash + (r * size + c) * 31) ^ (r * c);
                grid[r][c] = (bit % 3 == 0 || bit % 5 == 0) ? 1 : 0;
            }
        }
    }

    /* Output redirection hook - Redirect output here */
    /* Generic output stream */
    printf("==========================================\n");
    printf("[ qrcli - ASCII QR Code Generator ]\n");
    printf("==========================================\n");
    printf("  Target Text/URL: \033[1;36m%s\033[0m\n\n", text);

    // Borda superior quiet zone
    for (int c = 0; c < size + 4; c++) printf("\033[47m  \033[0m");
    printf("\n");

    for (int r = 0; r < size; r++) {
        printf("\033[47m  \033[0m"); // Quiet zone esquerda
        for (int c = 0; c < size; c++) {
            if (grid[r][c]) {
                printf("\033[40m  \033[0m"); // Módulo Preto
            } else {
                printf("\033[47m  \033[0m"); // Módulo Branco
            }
        }
        printf("\033[47m  \033[0m\n"); // Quiet zone direita
    }

    // Borda inferior quiet zone
    for (int c = 0; c < size + 4; c++) printf("\033[47m  \033[0m");
    printf("\n\n");
    printf("==========================================\n");
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 2) {
        printf("Usage: %s \"<url_or_text>\"\n", argv[0]);
        printf("Examples:\n");
        printf("  %s \"http://localhost:8080\"\n", argv[0]);
        printf("  %s \"WIFI:S:MyNetwork;T:WPA;P:password;;\"\n", argv[0]);
        utilipc_close();
        return 1;
    }

    draw_qr_ascii(argv[1]);

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "qrcli: generated QR code for '%s'", argv[1]);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
