#define _GNU_SOURCE
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

#define MAX_REDIRECTS 8
#define MAX_BODY_SIZE (1024 * 1024)

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_ERROR   "\033[1;31m"
#define COLOR_WARN    "\033[1;33m"
#define COLOR_H1      "\033[1;37;44m"
#define COLOR_H2      "\033[1;33m"
#define COLOR_H3      "\033[1;36m"
#define COLOR_BOLD    "\033[1;37m"
#define COLOR_LINK    "\033[0;34;4m"
#define COLOR_HREF    "\033[0;90m"
#define COLOR_IMG     "\033[1;33m"
#define COLOR_CODE    "\033[1;32m"
#define COLOR_TAG     "\033[0;90m"
#define COLOR_HR      "\033[0;35m"

static double get_time_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static void print_help(void) {
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ httpget 2.0 - Terminal Web Browser, 301/302 Redirect Follower & HTML Engine ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  httpget <URL> [OPTIONS]\n\n");
    printf("Options:\n");
    printf("  %s-o <ARQUIVO>%s          Baixar payload/arquivo com barra de progresso\n", COLOR_OK, COLOR_RESET);
    printf("  %s--raw%s                 Exibir resposta HTTP bruta sem formatacao HTML\n", COLOR_OK, COLOR_RESET);
    printf("  %s--max-redirs <N>%s      Limite maximo de redirecionamentos [Padrao: 8]\n", COLOR_OK, COLOR_RESET);
    printf("  %s-h, --help%s            Exibir este guia formatado\n\n", COLOR_OK, COLOR_RESET);
    printf("Exemplos:\n");
    printf("  • %shttpget neverssl.com%s                (Site HTTP puro renderizado em ANSI)\n", COLOR_WARN, COLOR_RESET);
    printf("  • %shttpget info.cern.ch%s                (Primeiro site da historia renderizado)\n", COLOR_WARN, COLOR_RESET);
    printf("  • %shttpget google.com%s                  (Detecta e segue com suporte seguro)\n", COLOR_WARN, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

static size_t decode_html_entity(const char *src, char *out, size_t *skip_chars) {
    if (strncmp(src, "&nbsp;", 6) == 0)   { *out = ' ';  *skip_chars = 6; return 1; }
    if (strncmp(src, "&amp;", 5) == 0)    { *out = '&';  *skip_chars = 5; return 1; }
    if (strncmp(src, "&lt;", 4) == 0)     { *out = '<';  *skip_chars = 4; return 1; }
    if (strncmp(src, "&gt;", 4) == 0)     { *out = '>';  *skip_chars = 4; return 1; }
    if (strncmp(src, "&quot;", 6) == 0)   { *out = '"';  *skip_chars = 6; return 1; }
    if (strncmp(src, "&#39;", 5) == 0 || strncmp(src, "&apos;", 6) == 0) {
        *out = '\''; *skip_chars = (src[1] == '#') ? 5 : 6; return 1;
    }
    if (strncmp(src, "&mdash;", 7) == 0)  { strcpy(out, "—"); *skip_chars = 7; return strlen(out); }
    if (strncmp(src, "&ndash;", 7) == 0)  { strcpy(out, "-"); *skip_chars = 7; return strlen(out); }
    if (strncmp(src, "&bull;", 6) == 0)   { strcpy(out, "•"); *skip_chars = 6; return strlen(out); }
    if (strncmp(src, "&copy;", 6) == 0)   { strcpy(out, "©"); *skip_chars = 6; return strlen(out); }
    if (strncmp(src, "&euro;", 6) == 0)   { strcpy(out, "€"); *skip_chars = 6; return strlen(out); }

    if (src[0] == '&' && src[1] == '#') {
        char *semicolon = strchr(src, ';');
        if (semicolon && (semicolon - src) < 10) {
            long code = 0;
            if (src[2] == 'x' || src[2] == 'X') code = strtol(src + 3, NULL, 16);
            else code = strtol(src + 2, NULL, 10);

            *skip_chars = (semicolon - src) + 1;
            if (code > 0 && code <= 127) {
                *out = (char)code;
                return 1;
            } else if (code == 160) {
                *out = ' ';
                return 1;
            }
        }
    }

    *out = *src;
    *skip_chars = 1;
    return 1;
}

static void render_html_page(const char *html, const char *page_url) {
    char page_title[256] = "Página Web";
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

    printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s│%s  %s🌐 TERMINAL WEB:%s %-56.56s %s│%s\n", COLOR_TITLE, COLOR_RESET, COLOR_H3, COLOR_RESET, page_title, COLOR_TITLE, COLOR_RESET);
    printf("%s│%s  %sURL:%s %-67.67s %s│%s\n", COLOR_TITLE, COLOR_RESET, COLOR_TAG, COLOR_RESET, page_url, COLOR_TITLE, COLOR_RESET);
    printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n", COLOR_TITLE, COLOR_RESET);

    const char *p = html;
    int in_tag = 0;
    char tag_buf[512] = "";
    size_t tag_idx = 0;

    int in_hidden_block = 0;
    int consecutive_newlines = 0;
    char active_href[512] = "";
    int in_anchor = 0;

    while (*p) {
        if (strncmp(p, "<!--", 4) == 0) {
            char *comment_end = strstr(p, "-->");
            if (comment_end) { p = comment_end + 3; continue; }
        }

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

            if (strncasecmp(tag_buf, "script", 6) == 0 ||
                strncasecmp(tag_buf, "style", 5) == 0 ||
                strncasecmp(tag_buf, "head", 4) == 0 ||
                strncasecmp(tag_buf, "svg", 3) == 0 ||
                strncasecmp(tag_buf, "noscript", 8) == 0) {
                in_hidden_block = 1;
                continue;
            }
            if (strncasecmp(tag_buf, "/script", 7) == 0 ||
                strncasecmp(tag_buf, "/style", 6) == 0 ||
                strncasecmp(tag_buf, "/head", 5) == 0 ||
                strncasecmp(tag_buf, "/svg", 4) == 0 ||
                strncasecmp(tag_buf, "/noscript", 9) == 0) {
                in_hidden_block = 0;
                continue;
            }

            if (in_hidden_block) continue;

            if (strncasecmp(tag_buf, "h1", 2) == 0) {
                printf("\n\n%s ❖ ", COLOR_H1);
                consecutive_newlines = 0;
            } else if (strncasecmp(tag_buf, "/h1", 3) == 0) {
                printf(" %s\n\n", COLOR_RESET);
                consecutive_newlines = 2;
            } else if (strncasecmp(tag_buf, "h2", 2) == 0) {
                printf("\n\n%s► ", COLOR_H2);
                consecutive_newlines = 0;
            } else if (strncasecmp(tag_buf, "/h2", 3) == 0) {
                printf("%s\n\n", COLOR_RESET);
                consecutive_newlines = 2;
            } else if (strncasecmp(tag_buf, "h3", 2) == 0) {
                printf("\n\n%s● ", COLOR_H3);
                consecutive_newlines = 0;
            } else if (strncasecmp(tag_buf, "/h3", 3) == 0) {
                printf("%s\n", COLOR_RESET);
                consecutive_newlines = 1;
            } else if (strncasecmp(tag_buf, "p", 1) == 0 || strncasecmp(tag_buf, "div", 3) == 0) {
                if (consecutive_newlines < 1) { printf("\n  "); consecutive_newlines = 1; }
            } else if (strncasecmp(tag_buf, "/p", 2) == 0 || strncasecmp(tag_buf, "/div", 4) == 0) {
                if (consecutive_newlines < 2) { printf("\n"); consecutive_newlines++; }
            } else if (strncasecmp(tag_buf, "br", 2) == 0) {
                printf("\n  ");
                consecutive_newlines = 1;
            } else if (strncasecmp(tag_buf, "hr", 2) == 0) {
                printf("\n%s  ────────────────────────────────────────────────────────────%s\n", COLOR_HR, COLOR_RESET);
                consecutive_newlines = 1;
            } else if (strncasecmp(tag_buf, "li", 2) == 0) {
                printf("\n   %s•%s ", COLOR_H3, COLOR_RESET);
                consecutive_newlines = 0;
            } else if (strncasecmp(tag_buf, "code", 4) == 0 || strncasecmp(tag_buf, "pre", 3) == 0) {
                printf("%s", COLOR_CODE);
            } else if (strncasecmp(tag_buf, "/code", 5) == 0 || strncasecmp(tag_buf, "/pre", 4) == 0) {
                printf("%s", COLOR_RESET);
            } else if (strncasecmp(tag_buf, "b", 1) == 0 || strncasecmp(tag_buf, "strong", 6) == 0) {
                printf("%s", COLOR_BOLD);
            } else if (strncasecmp(tag_buf, "/b", 2) == 0 || strncasecmp(tag_buf, "/strong", 7) == 0) {
                printf("%s", COLOR_RESET);
            } else if (strncasecmp(tag_buf, "a ", 2) == 0) {
                char *href_ptr = strcasestr(tag_buf, "href=\"");
                if (!href_ptr) href_ptr = strcasestr(tag_buf, "href='");
                if (href_ptr) {
                    href_ptr += 6;
                    char *end_h = strpbrk(href_ptr, "\"' >");
                    if (end_h) snprintf(active_href, sizeof(active_href), "%.*s", (int)(end_h - href_ptr), href_ptr);
                    else snprintf(active_href, sizeof(active_href), "%s", href_ptr);
                }
                printf("%s", COLOR_LINK);
                in_anchor = 1;
            } else if (strncasecmp(tag_buf, "/a", 2) == 0) {
                printf("%s", COLOR_RESET);
                if (in_anchor && strlen(active_href) > 0 && strncmp(active_href, "javascript:", 11) != 0 && active_href[0] != '#') {
                    printf(" %s[%s]%s", COLOR_HREF, active_href, COLOR_RESET);
                }
                in_anchor = 0;
                active_href[0] = '\0';
            } else if (strncasecmp(tag_buf, "img", 3) == 0) {
                char alt_text[128] = "";
                char *alt_ptr = strcasestr(tag_buf, "alt=\"");
                if (!alt_ptr) alt_ptr = strcasestr(tag_buf, "alt='");
                if (alt_ptr) {
                    alt_ptr += 5;
                    char *end_a = strpbrk(alt_ptr, "\"'");
                    if (end_a) snprintf(alt_text, sizeof(alt_text), "%.*s", (int)(end_a - alt_ptr), alt_ptr);
                }
                printf(" %s🖼️ [IMG%s%s]%s ", COLOR_IMG, strlen(alt_text) > 0 ? ": " : "", alt_text, COLOR_RESET);
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

        if (in_hidden_block) {
            p++;
            continue;
        }

        char decoded_buf[8] = "";
        size_t skip = 0;
        size_t d_len = decode_html_entity(p, decoded_buf, &skip);

        for (size_t k = 0; k < d_len; k++) {
            char c = decoded_buf[k];
            if (c == '\n' || c == '\r') {
                if (consecutive_newlines < 2) {
                    putchar('\n');
                    consecutive_newlines++;
                }
            } else {
                putchar(c);
                consecutive_newlines = 0;
            }
        }
        p += skip;
    }

    printf("\n\n%s────────────────────────────────────────────────────────────────────────────%s\n\n", COLOR_HR, COLOR_RESET);
}

static int parse_url(const char *in_url, const char *base_host, int base_port,
                     char *out_host, size_t host_sz, int *out_port, char *out_path, size_t path_sz, int *is_https) {
    *out_port = 80;
    *is_https = 0;
    out_path[0] = '/';
    out_path[1] = '\0';

    const char *p = in_url;

    if (p[0] == '/') {
        strncpy(out_host, base_host, host_sz - 1);
        *out_port = base_port;
        strncpy(out_path, p, path_sz - 1);
        return 0;
    }

    if (strncasecmp(p, "http://", 7) == 0) {
        p += 7;
        *out_port = 80;
    } else if (strncasecmp(p, "https://", 8) == 0) {
        p += 8;
        *out_port = 443;
        *is_https = 1;
    }

    const char *slash = strchr(p, '/');
    if (slash) {
        size_t hlen = slash - p;
        if (hlen >= host_sz) hlen = host_sz - 1;
        strncpy(out_host, p, hlen);
        out_host[hlen] = '\0';
        strncpy(out_path, slash, path_sz - 1);
    } else {
        strncpy(out_host, p, host_sz - 1);
        out_path[0] = '/';
        out_path[1] = '\0';
    }

    char *colon = strchr(out_host, ':');
    if (colon) {
        *colon = '\0';
        *out_port = atoi(colon + 1);
    }

    return 0;
}

static int fetch_https_fallback(const char *url, char *out_buffer, size_t max_buf) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "curl -s -L -A 'Mozilla/5.0' '%s' 2>/dev/null", url);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    size_t n = fread(out_buffer, 1, max_buf - 1, fp);
    pclose(fp);
    if (n > 0) {
        out_buffer[n] = '\0';
        return (int)n;
    }
    return -1;
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help();
        utilipc_close();
        return 0;
    }

    const char *initial_url = argv[1];
    const char *out_file = NULL;
    int raw_mode = 0;
    int max_redirects = MAX_REDIRECTS;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_file = argv[++i];
        } else if (strcmp(argv[i], "--raw") == 0) {
            raw_mode = 1;
        } else if (strcmp(argv[i], "--max-redirs") == 0 && i + 1 < argc) {
            max_redirects = atoi(argv[++i]);
        }
    }

    char current_url[1024];
    strncpy(current_url, initial_url, sizeof(current_url) - 1);

    char host[256] = "";
    char path[1024] = "/";
    int port = 80;
    int is_https = 0;

    char *response_buf = malloc(MAX_BODY_SIZE);
    if (!response_buf) {
        fprintf(stderr, "httpget: erro ao alocar memoria\n");
        utilipc_close();
        return 1;
    }

    int redirect_count = 0;

    while (redirect_count < max_redirects) {
        parse_url(current_url, host, port, host, sizeof(host), &port, path, sizeof(path), &is_https);

        // Se o destino for HTTPS, usa o fallback seguro de TLS para evitar loops infinitos
        if (is_https || port == 443) {
            printf("  %sConectando via TLS/HTTPS a%s \033[1;36m%s%s...\033[0m\n",
                   COLOR_TITLE, COLOR_RESET, host, path);
            int res = fetch_https_fallback(current_url, response_buf, MAX_BODY_SIZE);
            if (res > 0) {
                if (out_file) {
                    FILE *ofp = fopen(out_file, "wb");
                    if (ofp) { fwrite(response_buf, 1, res, ofp); fclose(ofp); }
                    printf("\n  %s[✔]%s Arquivo salvo em '%s' (%d bytes)\n\n", COLOR_OK, COLOR_RESET, out_file, res);
                } else if (raw_mode) {
                    printf("%s\n", response_buf);
                } else {
                    render_html_page(response_buf, current_url);
                }
                free(response_buf);
                utilipc_close();
                return 0;
            }
        }

        printf("  %sConectando a%s \033[1;36m%s:%d%s%s...\033[0m\n",
               COLOR_TITLE, COLOR_RESET, host, port, path, COLOR_RESET);

        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port);

        if (getaddrinfo(host, port_str, &hints, &res) != 0) {
            fprintf(stderr, "\n  %s[ERRO]%s Nao foi possivel resolver o host '%s'\n\n", COLOR_ERR, COLOR_RESET, host);
            free(response_buf);
            utilipc_close();
            return 1;
        }

        int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd < 0) {
            freeaddrinfo(res);
            free(response_buf);
            utilipc_close();
            return 1;
        }

        struct timeval tv = { .tv_sec = 6, .tv_usec = 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
            fprintf(stderr, "\n  %s[ERRO]%s Falha na conexao com %s:%d\n\n", COLOR_ERR, COLOR_RESET, host, port);
            close(fd);
            freeaddrinfo(res);
            free(response_buf);
            utilipc_close();
            return 1;
        }
        freeaddrinfo(res);

        char req[2048];
        snprintf(req, sizeof(req),
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36 sys-in-c-httpget/2.0\r\n"
            "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
            "Accept-Language: pt-BR,pt;q=0.9,en;q=0.8\r\n"
            "Connection: close\r\n\r\n", path, host);

        send(fd, req, strlen(req), 0);

        size_t total_received = 0;
        char chunk[8192];
        ssize_t n;
        int is_header_done = 0;
        size_t content_len = 0;
        char content_type[128] = "";
        char location_url[1024] = "";
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

                    char *loc = strcasestr(chunk, "Location: ");
                    if (loc) {
                        loc += 10;
                        char *end_loc = strstr(loc, "\r\n");
                        if (end_loc) {
                            snprintf(location_url, sizeof(location_url), "%.*s", (int)(end_loc - loc), loc);
                        }
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
            } else if (response_buf && total_received + n < MAX_BODY_SIZE - 1) {
                memcpy(response_buf + total_received, chunk, n);
                total_received += n;
            }
        }
        close(fd);

        // Segue o redirect
        if ((status_code == 301 || status_code == 302 || status_code == 303 ||
             status_code == 307 || status_code == 308) && strlen(location_url) > 0) {
            redirect_count++;
            printf("  \033[1;33m[HTTP %d Redirect]\033[0m Seguindo para: \033[1;36m%s\033[0m (Salto %d/%d)\n",
                   status_code, location_url, redirect_count, max_redirects);

            if (out_fp) { fclose(out_fp); out_fp = NULL; }
            strncpy(current_url, location_url, sizeof(current_url) - 1);
            continue;
        }

        if (out_fp) {
            fclose(out_fp);
            printf("\n  \033[1;32m[✔] Arquivo salvo em '%s' (%zu bytes)\033[0m\n\n", out_file, total_received);
            break;
        }

        if (response_buf) {
            response_buf[total_received] = '\0';
            if (raw_mode) {
                printf("%s\n", response_buf);
            } else if (strcasestr(content_type, "text/html") != NULL ||
                       strcasestr(response_buf, "<html") != NULL ||
                       strcasestr(response_buf, "<!doctype") != NULL) {
                render_html_page(response_buf, current_url);
            } else {
                printf("%s\n", response_buf);
            }
        }
        break;
    }

    if (redirect_count >= max_redirects) {
        fprintf(stderr, "\n  %s[ERRO]%s Limite maximo de redirecionamentos atingido (%d saltos).\n\n",
                COLOR_ERR, COLOR_RESET, max_redirects);
    }

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "httpget: %s (Redirs: %d)", host, redirect_count);
    utilipc_write_status(-1, -1, -1, log_msg);

    free(response_buf);
    utilipc_close();
    return 0;
}
