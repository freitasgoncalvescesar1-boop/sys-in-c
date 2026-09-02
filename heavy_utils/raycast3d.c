#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <signal.h>
#include <math.h>
#include <time.h>
#include "../src/libutilipc/utilipc.h"

#define PI 3.14159265358979323846
#define MAP_W 16
#define MAP_H 16

// Mapa do Labirinto (1 = Vermelho, 2 = Azul, 3 = Verde, 4 = Ciano)
static const int map[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1},
    {1,0,2,2,0,0,1,0,3,3,3,0,4,4,0,1},
    {1,0,2,2,0,0,0,0,3,0,3,0,4,4,0,1},
    {1,0,0,0,0,0,1,0,3,3,3,0,0,0,0,1},
    {1,0,1,0,1,0,1,0,0,0,0,0,4,0,4,1},
    {1,0,1,0,1,0,1,1,1,0,1,0,4,0,4,1},
    {1,0,0,0,0,0,0,0,1,0,1,0,0,0,0,1},
    {1,0,3,0,3,0,0,0,1,0,1,1,1,0,1,1},
    {1,0,3,0,3,0,2,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,2,0,2,2,0,3,3,3,0,1},
    {1,0,4,4,4,0,2,0,2,2,0,3,0,3,0,1},
    {1,0,4,0,4,0,0,0,0,0,0,3,3,3,0,1},
    {1,0,4,4,4,0,1,1,1,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,1,0,0,0,2,2,2,2,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

static struct termios orig_termios;
static volatile int keep_running = 1;

static double posX = 2.5, posY = 2.5;
static double dirX = 1.0, dirY = 0.0;
static double planeX = 0.0, planeY = 0.66;
static int show_minimap = 1;

static int weapon_flash = 0;
static int player_ammo = 50;
static double bob_phase = 0.0;

static char screen_buf[128 * 1024];

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
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    printf("\033[?1049h\033[?25l\033[H\033[J");
    fflush(stdout);
}

static const char *get_wall_color_code(int wall_type, int side) {
    if (wall_type == 1) return side ? "\033[0;31m" : "\033[1;31m"; // Vermelho
    if (wall_type == 2) return side ? "\033[0;34m" : "\033[1;34m"; // Azul
    if (wall_type == 3) return side ? "\033[0;32m" : "\033[1;32m"; // Verde
    return side ? "\033[0;36m" : "\033[1;36m";                     // Ciano Neon
}

static const char *get_shade_char(double dist) {
    if (dist < 2.5) return "██";
    if (dist < 4.5) return "▓▓";
    if (dist < 7.5) return "▒▒";
    return "░░";
}

int main(void) {
    utilipc_init();
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    enable_raw_mode();

    double smoothed_fps = 60.0;
    double last_time = get_time_sec();

    while (keep_running) {
        double cur_time = get_time_sec();
        double dt = cur_time - last_time;
        last_time = cur_time;

        if (dt > 0.0001) {
            double inst_fps = 1.0 / dt;
            smoothed_fps = smoothed_fps * 0.85 + inst_fps * 0.15;
        }

        if (weapon_flash > 0) weapon_flash--;

        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0) {
            ws.ws_col = 80; ws.ws_row = 24;
        }

        // Cada coluna no buffer representa 2 caracteres horizontais no terminal (ex: "██")
        int num_rays = ws.ws_col / 2;
        int render_h = ws.ws_row - 3;
        if (num_rays < 20) num_rays = 20;
        if (render_h < 10) render_h = 10;

        // Buffers para cada coluna
        int wall_types[160];
        int wall_sides[160];
        double wall_dists[160];
        int line_heights[160];

        if (num_rays > 160) num_rays = 160;

        // 1. DDA RAYCASTING
        for (int x = 0; x < num_rays; x++) {
            double cameraX = 2.0 * x / (double)num_rays - 1.0;
            double rayDirX = dirX + planeX * cameraX;
            double rayDirY = dirY + planeY * cameraX;

            int mapX = (int)posX;
            int mapY = (int)posY;

            double sideDistX, sideDistY;
            double deltaDistX = (rayDirX == 0) ? 1e30 : fabs(1.0 / rayDirX);
            double deltaDistY = (rayDirY == 0) ? 1e30 : fabs(1.0 / rayDirY);
            double perpWallDist;

            int stepX, stepY;
            int hit = 0, side = 0;

            if (rayDirX < 0) {
                stepX = -1;
                sideDistX = (posX - mapX) * deltaDistX;
            } else {
                stepX = 1;
                sideDistX = (mapX + 1.0 - posX) * deltaDistX;
            }
            if (rayDirY < 0) {
                stepY = -1;
                sideDistY = (posY - mapY) * deltaDistY;
            } else {
                stepY = 1;
                sideDistY = (mapY + 1.0 - posY) * deltaDistY;
            }

            while (hit == 0) {
                if (sideDistX < sideDistY) {
                    sideDistX += deltaDistX;
                    mapX += stepX;
                    side = 0;
                } else {
                    sideDistY += deltaDistY;
                    mapY += stepY;
                    side = 1;
                }
                if (mapX >= 0 && mapX < MAP_H && mapY >= 0 && mapY < MAP_W) {
                    if (map[mapX][mapY] > 0) hit = 1;
                } else {
                    hit = 1;
                }
            }

            if (side == 0) perpWallDist = (sideDistX - deltaDistX);
            else           perpWallDist = (sideDistY - deltaDistY);
            if (perpWallDist < 0.05) perpWallDist = 0.05;

            int lh = (int)(render_h / perpWallDist);
            if (lh > render_h * 2) lh = render_h * 2;

            wall_types[x] = (mapX >= 0 && mapX < MAP_H && mapY >= 0 && mapY < MAP_W) ? map[mapX][mapY] : 1;
            wall_sides[x] = side;
            wall_dists[x] = perpWallDist;
            line_heights[x] = lh;
        }

        // 2. MONTAGEM ULTRA RÁPIDA DO FRAME (BUFFER ÚNICO)
        char *out = screen_buf;
        out += sprintf(out, "\033[H\033[1;35m╭── [ 🕹️ heavy_utils/raycast3d | FPS: \033[1;32m%5.1f\033[1;35m | ⚡ AMMO: %-2d ] ──╮\033[0m\r\n",
                       smoothed_fps, player_ammo);

        int center_ray = num_rays / 2;
        int gun_bob_x = (int)(sin(bob_phase) * 2.0);

        for (int y = 0; y < render_h; y++) {
            for (int x = 0; x < num_rays; x++) {
                // Desenha Arma no centro inferior
                if (y >= render_h - 4 && abs(x - (center_ray + gun_bob_x)) <= 2) {
                    if (weapon_flash > 0 && y == render_h - 4) {
                        out += sprintf(out, "\033[1;33m🔥\033[0m");
                    } else if (abs(x - (center_ray + gun_bob_x)) == 0) {
                        out += sprintf(out, "\033[1;37m[]\033[0m");
                    } else {
                        out += sprintf(out, "\033[0;90m▓▓\033[0m");
                    }
                    continue;
                }

                // Desenha Minimapa no canto superior direito
                if (show_minimap && y < 6 && x >= num_rays - 7) {
                    int mx = (int)posX - 3 + (x - (num_rays - 7));
                    int my = (int)posY - 3 + y;

                    if (x - (num_rays - 7) == 3 && y == 3) {
                        out += sprintf(out, "\033[1;32m▲▲\033[0m"); // Jogador
                    } else if (mx >= 0 && mx < MAP_H && my >= 0 && my < MAP_W && map[mx][my] > 0) {
                        out += sprintf(out, "\033[0;37m██\033[0m"); // Parede
                    } else {
                        out += sprintf(out, "\033[0;90m··\033[0m"); // Espaço
                    }
                    continue;
                }

                int drawStart = -line_heights[x] / 2 + render_h / 2;
                int drawEnd   =  line_heights[x] / 2 + render_h / 2;

                if (y < drawStart) {
                    // Teto
                    out += sprintf(out, "\033[0;90m··\033[0m");
                } else if (y <= drawEnd) {
                    // Parede Sombreada com cor do bloco
                    const char *color_code = get_wall_color_code(wall_types[x], wall_sides[x]);
                    const char *shade_str  = get_shade_char(wall_dists[x]);
                    out += sprintf(out, "%s%s\033[0m", color_code, shade_str);
                } else {
                    // Chão
                    out += sprintf(out, "\033[0;90m..\033[0m");
                }
            }
            out += sprintf(out, "\r\n");
        }

        out += sprintf(out, "\033[1;35m╰── [ \033[1;33mW/S: Mover | A/D: Strafe | Setas/Q/E: Girar | ESPAÇO: Atirar | ESC: Sair\033[1;35m ] ──╯\033[0m");

        // 1 ÚNICA ESCRITA (FLUIDO E ULTRA RÁPIDO)
        write(STDOUT_FILENO, screen_buf, out - screen_buf);

        // 3. ENTRADA DE TECLADO SEM LATÊNCIA
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 15000 }; // ~60 FPS

        double moveSpeed = dt * 4.2;
        double rotSpeed  = dt * 2.8;

        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            char ch[16];
            ssize_t n = read(STDIN_FILENO, ch, sizeof(ch));
            for (ssize_t i = 0; i < n; i++) {
                char c = ch[i];

                if (c == 27) {
                    if (n == 1 || ch[i+1] == 0) { keep_running = 0; break; }
                    if (i + 2 < n && ch[i+1] == '[') {
                        if (ch[i+2] == 'A') c = 'w';
                        else if (ch[i+2] == 'B') c = 's';
                        else if (ch[i+2] == 'D') c = 'q';
                        else if (ch[i+2] == 'C') c = 'e';
                        i += 2;
                    }
                }

                if (c == ' ' && weapon_flash == 0) {
                    weapon_flash = 2;
                    if (player_ammo > 0) player_ammo--;
                }

                int moved = 0;
                if (c == 'w' || c == 'W') {
                    if (map[(int)(posX + dirX * moveSpeed)][(int)posY] == 0) posX += dirX * moveSpeed;
                    if (map[(int)posX][(int)(posY + dirY * moveSpeed)] == 0) posY += dirY * moveSpeed;
                    moved = 1;
                }
                if (c == 's' || c == 'S') {
                    if (map[(int)(posX - dirX * moveSpeed)][(int)posY] == 0) posX -= dirX * moveSpeed;
                    if (map[(int)posX][(int)(posY - dirY * moveSpeed)] == 0) posY -= dirY * moveSpeed;
                    moved = 1;
                }
                if (c == 'a' || c == 'A') {
                    if (map[(int)(posX - planeX * moveSpeed)][(int)posY] == 0) posX -= planeX * moveSpeed;
                    if (map[(int)posX][(int)(posY - planeY * moveSpeed)] == 0) posY -= planeY * moveSpeed;
                    moved = 1;
                }
                if (c == 'd' || c == 'D') {
                    if (map[(int)(posX + planeX * moveSpeed)][(int)posY] == 0) posX += planeX * moveSpeed;
                    if (map[(int)posX][(int)(posY + planeY * moveSpeed)] == 0) posY += planeY * moveSpeed;
                    moved = 1;
                }

                if (moved) bob_phase += 0.5;

                if (c == 'q' || c == 'Q') {
                    double oldDirX = dirX;
                    dirX = dirX * cos(rotSpeed) - dirY * sin(rotSpeed);
                    dirY = oldDirX * sin(rotSpeed) + dirY * cos(rotSpeed);
                    double oldPlaneX = planeX;
                    planeX = planeX * cos(rotSpeed) - planeY * sin(rotSpeed);
                    planeY = oldPlaneX * sin(rotSpeed) + planeY * cos(rotSpeed);
                }
                if (c == 'e' || c == 'E') {
                    double oldDirX = dirX;
                    dirX = dirX * cos(-rotSpeed) - dirY * sin(-rotSpeed);
                    dirY = oldDirX * sin(-rotSpeed) + dirY * cos(-rotSpeed);
                    double oldPlaneX = planeX;
                    planeX = planeX * cos(-rotSpeed) - planeY * sin(-rotSpeed);
                    planeY = oldPlaneX * sin(-rotSpeed) + planeY * cos(-rotSpeed);
                }
                if (c == 'm' || c == 'M') {
                    show_minimap = !show_minimap;
                }
            }
        }
    }

    return 0;
}
