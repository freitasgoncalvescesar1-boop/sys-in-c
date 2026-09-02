#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_H1      "\033[1;37;44m" // Branco em fundo azul
#define COLOR_H2      "\033[1;33m"    // Amarelo
#define COLOR_H3      "\033[1;36m"    // Ciano
#define COLOR_BOLD    "\033[1;37m"    // Branco negrito
#define COLOR_LINK    "\033[0;34;4m"  // Azul sublinhado
#define COLOR_IMG     "\033[1;33m"    // Amarelo
#define COLOR_CODE    "\033[0;32m"    // Verde
#define COLOR_TAG     "\033[0;90m"

static double get_time_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static void print_help(void) {
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ httpget - Pure C Web Client & Terminal HTML Reader ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  httpget <URL>                  (Renderiza pagina HTML formatada no terminal)\n");
    printf("  httpget <URL> -o <arquivo>     (Baixa arquivo com barra de progresso)\n");
    printf("  httpget <URL> --raw            (Exibe resposta HTTP bruta)\n\n");
    printf("Exemplos:\n");
    printf("  httpget http://example.com\n");
    printf("  httpget http://info.cern.ch\n");
    printf("  httpget http://speed.cloudflare.com/__down?bytes=10000000 -o teste.bin\n");
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

// Decodifica entidades HTML comuns
static void print_decoded_char(const char *entity, size_t *skip) {
    if (strncmp(entity, "&amp;", 5) == 0)  { putchar('&'); *skip = 4; }
    else if (strncmp(entity, "&lt;", 4) == 0)   { putchar('<'); *skip = 3; }
    else if (strncmp(entity, "&gt;", 4) == 0)   { putchar('>'); *skip = 3; }
    else if (strncmp(entity, "&quot;", 6) == 0) { putchar('"'); *skip = 5; }
    else if (strncmp(entity, "&#39;", 5) == 0)  { putchar('\''); *skip = 4; }
    else if (strncmp(entity, "&nbsp;", 6) == 0) { putchar(' '); *skip = 5; }
    else { putchar(entity[0]); *skip = 0; }
}

// Renderiza HTML formatando tags, títulos, links e imagens no terminal
static void render_html_page(const char *html, const char *page_url) {
    char page_title[256] = "Web Page";
    const char *t_start = strcasestr(html, "<title>");
    if (t_start) {
        t_start += 7;
        const char *t_end = strcasestr(t_start, "</title>");
        if (t_end) {
            size_t tlen = t_end - t_start;
            if (tlen >= sizeof(page_title)) tlen = sizeof(page_title) - 1;
            strncpy(page_title, t_start, tlen);
            page_title[tlen] = '\0';
        }
    }

    printf("\n\033[1;35m╭────────────────────────────────────────────────────────────────────────────╮\033[0m\n");
    printf("\033[1;35m│\033[0m  \033[1;36m🌐 WEB READER:\033[0m %-58.58s \033[1;35m│\033[0m\n", page_title);
    printf("\033[1;35m│\033[0m  \033[0;90mURL: %-67.67s\033[0m \033[1;35m│\033[0m\n", page_url);
    printf("\033[1;35m╰────────────────────────────────────────────────────────────────────────────╯\033[0m\n\n");

    const char *p = html;
    int in_tag = 0;
    char tag_buf[512];
    size_t tag_idx = 0;
    int in_script_or_style = 0;

    while (*p) {
        if (*p == '<') {
            in_tag = 1;
            tag_idx = 0;
            tag_buf[0] = '\0';
            p++;
            continue;
        }

        if (*p == '>') {
            in_tag = 0;
            tag_buf[tag_idx] = '\0';
            p++;

            // Detecta e ignora tags de scripts e css
            if (strncasecmp(tag_buf, "script", 6) == 0 || strncasecmp(tag_buf, "style", 5) == 0) {
                in_script_or_style = 1;
                continue;
            }
            if (strncasecmp(tag_buf, "/script", 7) == 0 || strncasecmp(tag_buf, "/style", 6) == 0) {
                in_script_or_style = 0;
                continue;
            }

            if (in_script_or_style) continue;

            // Formatação de Títulos e Quebras
            if (strncasecmp(tag_buf, "h1", 2) == 0) printf("\n\n%s ❖ ", COLOR_H1);
            else if (strncasecmp(tag_buf, "/h1", 3) == 0) printf(" %s\n\n", COLOR_RESET);
            else if (strncasecmp(tag_buf, "h2", 2) == 0) printf("\n\n%s► ", COLOR_H2);
            else if (strncasecmp(tag_buf, "/h2", 3) == 0) printf("%s\n\n", COLOR_RESET);
            else if (strncasecmp(tag_buf, "h3", 2) == 0) printf("\n\n%s● ", COLOR_H3);
            else if (strncasecmp(tag_buf, "/h3", 3) == 0) printf("%s\n", COLOR_RESET);
            else if (strncasecmp(tag_buf, "p", 1) == 0) printf("\n  ");
            else if (strncasecmp(tag_buf, "/p", 2) == 0) printf("\n");
            else if (strncasecmp(tag_buf, "br", 2) == 0) printf("\n  ");
            else if (strncasecmp(tag_buf, "li", 2) == 0) printf("\n   %s•%s ", COLOR_H3, COLOR_RESET);
            else if (strncasecmp(tag_buf, "code", 4) == 0 || strncasecmp(tag_buf, "pre", 3) == 0) printf("%s", COLOR_CODE);
            else if (strncasecmp(tag_buf, "/code", 5) == 0 || strncasecmp(tag_buf, "/pre", 4) == 0) printf("%s", COLOR_RESET);
            else if (strncasecmp(tag_buf, "b", 1) == 0 || strncasecmp(tag_buf, "strong", 6) == 0) printf("%s", COLOR_BOLD);
            else if (strncasecmp(tag_buf, "/b", 2) == 0 || strncasecmp(tag_buf, "/strong", 7) == 0) printf("%s", COLOR_RESET);
            
            // Tratamento especial de IMAGENS: corta e substitui por [IMG: alt / src]
            else if (strncasecmp(tag_buf, "img", 3) == 0) {
                char alt_text[128] = "";
                char src_text[256] = "";

                char *alt_ptr = strcasestr(tag_buf, "alt=\"");
                if (!alt_ptr) alt_ptr = strcasestr(tag_buf, "alt='");
                if (alt_ptr) {
                    alt_ptr += 5;
                    char *end = strpbrk(alt_ptr, "\"'");
                    if (end) snprintf(alt_text, sizeof(alt_text), "%.*s", (int)(end - alt_ptr), alt_ptr);
                }

                char *src_ptr = strcasestr(tag_buf, "src=\"");
                if (!src_ptr) src_ptr = strcasestr(tag_buf, "src='");
                if (src_ptr) {
                    src_ptr += 5;
                    char *end = strpbrk(src_ptr, "\"'");
                    if (end) snprintf(src_text, sizeof(src_text), "%.*s", (int)(end - src_ptr), src_ptr);
                }

                printf(" %s🖼️ [IMG: %s%s%s]%s ", COLOR_IMG,
                       strlen(alt_text) > 0 ? alt_text : "Imagem",
                       strlen(src_text) > 0 ? " | " : "",
                       src_text, COLOR_RESET);
            }
            continue;
        }

        if (in_tag) {
            if (tag_idx < sizeof(tag_buf) - 1) {
                tag_buf[tag_idx++] = *p;
            }
            p++;
            continue;
        }

        if (in_script_or_style) {
            p++;
            continue;
        }

        // Decodificação de entidades HTML
        if (*p == '&') {
            size_t skip = 0;
            print_decoded_char(p, &skip);
            p += skip + 1;
            continue;
        }

        putchar(*p++);
    }
    printf("\n\n\033[0;90m────────────────────────────────────────────────────────────────────────────\033[0m\n\n");
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help();
        utilipc_close();
        return 0;
    }

    const char *url = argv[1];
    const char *out_file = NULL;
    int raw_mode = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_file = argv[++i];
        } else if (strcmp(argv[i], "--raw") == 0) {
            raw_mode = 1;
        }
    }

    // Parser básico de URL
    char host[256] = "";
    char path[1024] = "/";
    int port = 80;

    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) p += 7;

    const char *slash = strchr(p, '/');
    if (slash) {
        size_t hlen = slash - p;
        if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
        strncpy(host, p, hlen);
        host[hlen] = '\0';
        strncpy(path, slash, sizeof(path) - 1);
    } else {
        strncpy(host, p, sizeof(host) - 1);
    }

    char *colon = strchr(host, ':');
    if (colon) {
        *colon = '\0';
        port = atoi(colon + 1);
    }

    printf("  Conectando a \033[1;36m%s:%d%s%s...\n", host, port, path, COLOR_RESET);

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[16]; snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        fprintf(stderr, "httpget: erro ao resolver host '%s'\n", host);
        utilipc_close();
        return 1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        utilipc_close();
        return 1;
    }

    struct timeval tv = { .tv_sec = 8, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        fprintf(stderr, "httpget: falha ao conectar a %s:%d\n", host, port);
        close(fd);
        freeaddrinfo(res);
        utilipc_close();
        return 1;
    }
    freeaddrinfo(res);

    char req[2048];
    snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: sys-in-c-httpget/1.0\r\n"
        "Accept: text/html,application/xhtml+xml,*/*\r\n"
        "Connection: close\r\n\r\n", path, host);

    send(fd, req, strlen(req), 0);

    char *response_buf = malloc(512 * 1024); // Buffer de até 512KB para páginas
    size_t total_received = 0;
    char chunk[8192];
    ssize_t n;
    int is_header_done = 0;
    size_t content_len = 0;
    char content_type[128] = "";
    int status_code = 0;

    FILE *out_fp = out_file ? fopen(out_file, "wb") : NULL;
    double t_start = get_time_sec();

    while ((n = recv(fd, chunk, sizeof(chunk), 0)) > 0) {
        if (!is_header_done) {
            char *body = strstr(chunk, "\r\n\r\n");
            if (body) {
                *body = '\0';
                sscanf(chunk, "HTTP/%*s %d", &status_code);

                char *cl = strcasestr(chunk, "Content-Length: ");
                if (cl) content_len = strtoull(cl + 16, NULL, 10);

                char *ct = strcasestr(chunk, "Content-Type: ");
                if (ct) {
                    char *end_ct = strstr(ct, "\r\n");
                    if (end_ct) snprintf(content_type, sizeof(content_type), "%.*s", (int)(end_ct - (ct + 14)), ct + 14);
                }

                is_header_done = 1;
                char *payload_start = body + 4;
                size_t payload_bytes = n - (payload_start - chunk);

                if (out_fp) {
                    fwrite(payload_start, 1, payload_bytes, out_fp);
                    total_received += payload_bytes;
                } else if (response_buf) {
                    memcpy(response_buf, payload_start, payload_bytes);
                    total_received += payload_bytes;
                }
                continue;
            }
        }

        if (out_fp) {
            fwrite(chunk, 1, n, out_fp);
            total_received += n;

            if (content_len > 0) {
                int pct = (int)(((double)total_received / (double)content_len) * 100.0);
                double elapsed = get_time_sec() - t_start;
                double speed = (elapsed > 0) ? ((double)total_received / (1024.0 * 1024.0 * elapsed)) : 0;
                printf("\r  Baixando: \033[1;32m%d%%\033[0m (%.2f/%.2f MB | %.2f MB/s)",
                       pct, (double)total_received/(1024*1024), (double)content_len/(1024*1024), speed);
                fflush(stdout);
            }
        } else if (response_buf && total_received + n < 512 * 1024 - 1) {
            memcpy(response_buf + total_received, chunk, n);
            total_received += n;
        }
    }
    close(fd);

    if (out_fp) {
        fclose(out_fp);
        printf("\n  \033[1;32m[✔] Arquivo salvo em '%s' (%zu bytes)\033[0m\n", out_file, total_received);
    } else if (response_buf) {
        response_buf[total_received] = '\0';
        if (raw_mode) {
            printf("%s\n", response_buf);
        } else if (strstr(content_type, "text/html") != NULL || strstr(response_buf, "<html") != NULL || strstr(response_buf, "<HTML") != NULL) {
            render_html_page(response_buf, url);
        } else {
            printf("%s\n", response_buf);
        }
        free(response_buf);
    }

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "httpget: %s (Status: %d | %zu bytes)", host, status_code, total_received);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
