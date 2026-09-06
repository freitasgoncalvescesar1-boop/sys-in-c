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
#include "../libutilipc/utilipc.h"

#define PI 3.14159265358979323846
#define SCREEN_BUF_SZ (256 * 1024)

typedef struct { double x, y, z; } Vec3;

static inline Vec3 v_add(Vec3 a, Vec3 b) { return (Vec3){a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline Vec3 v_sub(Vec3 a, Vec3 b) { return (Vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline Vec3 v_scale(Vec3 a, double s) { return (Vec3){a.x * s, a.y * s, a.z * s}; }
static inline double v_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 v_norm(Vec3 a) {
    double l = sqrt(v_dot(a, a));
    return (l > 0.0) ? v_scale(a, 1.0 / l) : a;
}

typedef struct {
    Vec3 center;
    double radius;
    Vec3 color;
    double specular;
    double reflect;
} Sphere;

static struct termios orig_termios;
static volatile int keep_running = 1;
static char screen_buf[SCREEN_BUF_SZ];

// Estado de Controles Interativos
static double cam_rot_x = 0.0;
static double cam_rot_y = 0.0;
static int animate_light = 1;
static int enable_reflections = 1;

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

static int ray_sphere_intersect(Vec3 ro, Vec3 rd, Sphere s, double *t_out) {
    Vec3 oc = v_sub(ro, s.center);
    double b = 2.0 * v_dot(oc, rd);
    double c = v_dot(oc, oc) - s.radius * s.radius;
    double disc = b * b - 4.0 * c;
    if (disc < 0.0) return 0;
    double t = (-b - sqrt(disc)) * 0.5;
    if (t > 0.001) {
        *t_out = t;
        return 1;
    }
    return 0;
}

static Vec3 trace_ray(Vec3 ro, Vec3 rd, Sphere *spheres, int num_spheres, Vec3 light_pos, int depth) {
    if (depth > (enable_reflections ? 2 : 0)) return (Vec3){0, 0, 0};

    double closest_t = 1e9;
    int hit_idx = -1;
    int hit_floor = 0;

    for (int i = 0; i < num_spheres; i++) {
        double t;
        if (ray_sphere_intersect(ro, rd, spheres[i], &t)) {
            if (t < closest_t) {
                closest_t = t;
                hit_idx = i;
            }
        }
    }

    if (rd.y < -0.001) {
        double t = (-1.5 - ro.y) / rd.y;
        if (t > 0.001 && t < closest_t) {
            closest_t = t;
            hit_floor = 1;
            hit_idx = -1;
        }
    }

    if (hit_idx == -1 && !hit_floor) {
        double unit_y = (rd.y + 1.0) * 0.5;
        return (Vec3){0.05 * unit_y, 0.1 * unit_y, 0.25 * unit_y};
    }

    Vec3 hit_pt = v_add(ro, v_scale(rd, closest_t));
    Vec3 normal;
    Vec3 mat_color;
    double spec = 0.0, refl = 0.0;

    if (hit_floor) {
        normal = (Vec3){0, 1, 0};
        int cx = (int)(floor(hit_pt.x * 0.8));
        int cz = (int)(floor(hit_pt.z * 0.8));
        if ((cx + cz) % 2 == 0) mat_color = (Vec3){0.75, 0.75, 0.75};
        else mat_color = (Vec3){0.18, 0.18, 0.24};
        spec = 16.0;
        refl = 0.20;
    } else {
        normal = v_norm(v_sub(hit_pt, spheres[hit_idx].center));
        mat_color = spheres[hit_idx].color;
        spec = spheres[hit_idx].specular;
        refl = spheres[hit_idx].reflect;
    }

    Vec3 l_dir = v_norm(v_sub(light_pos, hit_pt));
    double diff = v_dot(normal, l_dir);
    if (diff < 0.0) diff = 0.0;

    int in_shadow = 0;
    for (int i = 0; i < num_spheres; i++) {
        if (i == hit_idx && !hit_floor) continue;
        double st;
        if (ray_sphere_intersect(v_add(hit_pt, v_scale(normal, 0.01)), l_dir, spheres[i], &st)) {
            in_shadow = 1;
            break;
        }
    }

    double ambient = 0.18;
    double light_intensity = in_shadow ? ambient : (ambient + diff * 0.82);

    double spec_intensity = 0.0;
    if (!in_shadow && spec > 0.0) {
        Vec3 v = v_norm(v_scale(rd, -1.0));
        Vec3 r = v_sub(v_scale(normal, 2.0 * v_dot(normal, l_dir)), l_dir);
        double r_dot_v = v_dot(r, v);
        if (r_dot_v > 0.0) {
            spec_intensity = pow(r_dot_v, spec) * 0.6;
        }
    }

    Vec3 final_col = v_add(v_scale(mat_color, light_intensity), (Vec3){spec_intensity, spec_intensity, spec_intensity});

    if (enable_reflections && refl > 0.0) {
        Vec3 ref_dir = v_sub(rd, v_scale(normal, 2.0 * v_dot(rd, normal)));
        Vec3 ref_col = trace_ray(v_add(hit_pt, v_scale(normal, 0.01)), ref_dir, spheres, num_spheres, light_pos, depth + 1);
        final_col = v_add(v_scale(final_col, 1.0 - refl), v_scale(ref_col, refl));
    }

    return final_col;
}

int main(int argc, char *argv[]) {
    utilipc_init();
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        printf("Usage: asciiray\nUltra-fast 3D Ray Tracing engine rendered in 24-bit TrueColor ASCII (Single-Buffer 60 FPS).\n");
        return 0;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    enable_raw_mode();

    int frame = 0;
    double smoothed_fps = 60.0;
    double last_time = get_time_sec();
    double anim_t = 0.0;

    while (keep_running) {
        double cur_time = get_time_sec();
        double dt = cur_time - last_time;
        last_time = cur_time;

        if (dt > 0.0001) {
            double inst_fps = 1.0 / dt;
            smoothed_fps = smoothed_fps * 0.85 + inst_fps * 0.15;
        }

        if (animate_light) {
            anim_t += dt * 1.5;
        }

        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0) {
            ws.ws_col = 80; ws.ws_row = 24;
        }

        int width = ws.ws_col;
        int height = (ws.ws_row - 3) * 2;
        if (width < 20) width = 20;
        if (width > 160) width = 160;
        if (height < 16) height = 16;
        if (height > 120) height = 120;

        Sphere spheres[4];
        spheres[0] = (Sphere){(Vec3){cos(anim_t) * 1.5, 0.0, 3.5 + sin(anim_t) * 0.5}, 0.8, (Vec3){0.9, 0.2, 0.3}, 30.0, 0.3};
        spheres[1] = (Sphere){(Vec3){cos(anim_t + PI) * 1.5, 0.0, 3.5 + sin(anim_t + PI) * 0.5}, 0.8, (Vec3){0.2, 0.7, 0.9}, 50.0, 0.4};
        spheres[2] = (Sphere){(Vec3){0.0, -0.5 + sin(anim_t * 2.0) * 0.4, 3.0}, 0.5, (Vec3){0.3, 0.9, 0.3}, 40.0, 0.2};
        spheres[3] = (Sphere){(Vec3){0.0, 1.2, 4.0}, 0.6, (Vec3){0.9, 0.8, 0.2}, 60.0, 0.5};

        Vec3 light_pos = (Vec3){sin(anim_t * 1.2) * 4.0, 4.0, 1.0 + cos(anim_t * 1.2) * 2.0};
        Vec3 cam_pos = (Vec3){cam_rot_x, 0.2 + cam_rot_y, 0};

        // MONTAGEM DO FRAME EM BUFFER ÚNICO (ULTRA RÁPIDO & ZERO FLICKER)
        char *out = screen_buf;
        out += sprintf(out, "\033[H\033[1;35m╭─── [ 🌌 asciiray 2.0 | FPS: \033[1;32m%5.1f\033[1;35m | Luz: \033[1;33m(%.1f, %.1f)\033[1;35m | Reflexos: %s\033[1;35m ] ───╮\033[0m\r\n",
                       smoothed_fps, light_pos.x, light_pos.z, enable_reflections ? "\033[1;32mON" : "\033[1;31mOFF");

        int last_fg_r = -1, last_fg_g = -1, last_fg_b = -1;
        int last_bg_r = -1, last_bg_g = -1, last_bg_b = -1;

        for (int y = 0; y < height; y += 2) {
            for (int x = 0; x < width; x++) {
                double u1 = ((double)x / width) * 2.0 - 1.0;
                double v1 = -(((double)y / height) * 2.0 - 1.0) * ((double)height / width);
                Vec3 rd1 = v_norm((Vec3){u1, v1, 1.3});
                Vec3 c1 = trace_ray(cam_pos, rd1, spheres, 4, light_pos, 0);

                double u2 = ((double)x / width) * 2.0 - 1.0;
                double v2 = -(((double)(y + 1) / height) * 2.0 - 1.0) * ((double)height / width);
                Vec3 rd2 = v_norm((Vec3){u2, v2, 1.3});
                Vec3 c2 = trace_ray(cam_pos, rd2, spheres, 4, light_pos, 0);

                int r1 = (int)(c1.x * 255.0); if (r1 > 255) r1 = 255;
                int g1 = (int)(c1.y * 255.0); if (g1 > 255) g1 = 255;
                int b1 = (int)(c1.z * 255.0); if (b1 > 255) b1 = 255;

                int r2 = (int)(c2.x * 255.0); if (r2 > 255) r2 = 255;
                int g2 = (int)(c2.y * 255.0); if (g2 > 255) g2 = 255;
                int b2 = (int)(c2.z * 255.0); if (b2 > 255) b2 = 255;

                // Cache de cores ANSI: só emite escape code se a cor mudar!
                if (r1 != last_fg_r || g1 != last_fg_g || b1 != last_fg_b) {
                    out += sprintf(out, "\033[38;2;%d;%d;%dm", r1, g1, b1);
                    last_fg_r = r1; last_fg_g = g1; last_fg_b = b1;
                }
                if (r2 != last_bg_r || g2 != last_bg_g || b2 != last_bg_b) {
                    out += sprintf(out, "\033[48;2;%d;%d;%dm", r2, g2, b2);
                    last_bg_r = r2; last_bg_g = g2; last_bg_b = b2;
                }
                *out++ = 0xE2; *out++ = 0x96; *out++ = 0x80; // '▀' em UTF-8
            }
            out += sprintf(out, "\033[0m\r\n");
            last_fg_r = last_fg_g = last_fg_b = -1;
            last_bg_r = last_bg_g = last_bg_b = -1;
        }

        out += sprintf(out, "\033[1;35m╰─── [ \033[1;33mW/A/S/D: Mover Câmera | ESPAÇO: Pausar Luz | R: Reflexos | ESC/Q: Sair\033[1;35m ] ───╯\033[0m");

        // 1 ÚNICA ESCRITA DIRETA (MAX SPEED)
        write(STDOUT_FILENO, screen_buf, out - screen_buf);

        frame++;

        // Controle de entrada de teclado sem latência
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 16000 }; // ~60 FPS

        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            char ch[16];
            ssize_t n = read(STDIN_FILENO, ch, sizeof(ch));
            for (ssize_t i = 0; i < n; i++) {
                char c = ch[i];
                if (c == 27) {
                    if (n == 1 || ch[i+1] == 0) { keep_running = 0; break; }
                    if (i + 2 < n && ch[i+1] == '[') {
                        if (ch[i+2] == 'A') cam_rot_y += 0.15;
                        else if (ch[i+2] == 'B') cam_rot_y -= 0.15;
                        else if (ch[i+2] == 'D') cam_rot_x -= 0.2;
                        else if (ch[i+2] == 'C') cam_rot_x += 0.2;
                        i += 2;
                    }
                }
                if (c == 'q' || c == 'Q' || c == 3) { keep_running = 0; break; }
                if (c == ' ') animate_light = !animate_light;
                if (c == 'r' || c == 'R') enable_reflections = !enable_reflections;
                if (c == 'a' || c == 'A') cam_rot_x -= 0.2;
                if (c == 'd' || c == 'D') cam_rot_x += 0.2;
                if (c == 'w' || c == 'W') cam_rot_y += 0.15;
                if (c == 's' || c == 'S') cam_rot_y -= 0.15;
            }
        }
    }

    return 0;
}
