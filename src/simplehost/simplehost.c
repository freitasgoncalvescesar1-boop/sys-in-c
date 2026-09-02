#include "server.h"

char g_www_root[1024];

static void *client_handler_thread(void *arg) {
    client_task_t *task = (client_task_t *)arg;
    int client_fd = task->client_fd;
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &task->client_addr.sin_addr, client_ip, sizeof(client_ip));
    free(task);

    pthread_detach(pthread_self());

    struct timeval tv = { .tv_sec = 8, .tv_usec = 0 };
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    char header_buf[4096];
    ssize_t n = recv(client_fd, header_buf, sizeof(header_buf) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return NULL;
    }
    header_buf[n] = '\0';

    char method[16] = "", raw_uri[512] = "";
    if (sscanf(header_buf, "%15s %511s", method, raw_uri) < 2) {
        close(client_fd);
        return NULL;
    }

    // 1. Processamento de UPLOAD (POST /upload)
    if (strcasecmp(method, "POST") == 0 && (strcasecmp(raw_uri, "/upload") == 0 || strcasecmp(raw_uri, "/upload/") == 0)) {
        if (handle_multipart_upload(client_fd, header_buf, strlen(header_buf), n) == 0) {
            const char *ok_resp =
                "HTTP/1.1 200 OK\r\n"
                "Server: " SERVER_BANNER "\r\n"
                "Content-Type: text/html; charset=UTF-8\r\n"
                "Connection: close\r\n\r\n"
                "<html><body style='background:#181825;color:#a6e3a1;text-align:center;font-family:sans-serif;padding:50px;'>"
                "<h1>✔ Upload Concluido com Sucesso!</h1>"
                "<p><a href='/uploads' style='color:#89b4fa;font-size:1.2em;'>📂 Ver Arquivos Enviados (/uploads)</a> | "
                "<a href='/' style='color:#cba6f7;font-size:1.2em;'>Inicio</a></p></body></html>";
            send(client_fd, ok_resp, strlen(ok_resp), 0);
        } else {
            send_custom_error_page(client_fd, 400, "Bad Request", "Falha no processamento do arquivo enviado", raw_uri);
        }
        close(client_fd);
        return NULL;
    }

    // 2. Proteção contra Path Traversal
    char full_fs_path[1024];
    if (!is_safe_canonical_path(raw_uri, full_fs_path, sizeof(full_fs_path))) {
        send_custom_error_page(client_fd, 403, "Forbidden", "Acesso bloqueado por violacao de seguranca", raw_uri);
        printf("  \033[1;31m[BLOQUEIO DE SEGURANCA]\033[0m Tentativa de Path Traversal de %s: '%s'\n", client_ip, raw_uri);
        close(client_fd);
        return NULL;
    }

    struct stat st;
    if (stat(full_fs_path, &st) != 0) {
        // ✨ PROTEÇÃO NOT FOUND (404 ESTILIZADO)
        send_custom_error_page(client_fd, 404, "Not Found", "O arquivo nao foi achado no servidor", raw_uri);
        printf("  \033[1;33m[404 NOT FOUND]\033[0m Requisicao de %s para '%s'\n", client_ip, raw_uri);
        close(client_fd);
        return NULL;
    }

    // Se for diretório, serve index.html ou gera File Explorer
    if (S_ISDIR(st.st_mode)) {
        char index_check[1024];
        snprintf(index_check, sizeof(index_check), "%s/index.html", full_fs_path);
        struct stat idx_st;
        if (stat(index_check, &idx_st) == 0) {
            strncpy(full_fs_path, index_check, sizeof(full_fs_path) - 1);
            st = idx_st;
        } else {
            send_directory_listing(client_fd, full_fs_path, raw_uri);
            close(client_fd);
            return NULL;
        }
    }

    // 3. Suporte a HTTP 206 Partial Content (Range Requests)
    off_t range_start = 0, range_end = st.st_size - 1;
    int is_partial = 0;
    char *range_hdr = strcasestr(header_buf, "Range: bytes=");
    if (range_hdr) {
        range_hdr += 13;
        sscanf(range_hdr, "%lld-%lld", (long long *)&range_start, (long long *)&range_end);
        if (range_end >= st.st_size || range_end == 0) range_end = st.st_size - 1;
        if (range_start <= range_end) is_partial = 1;
    }

    int file_fd = open(full_fs_path, O_RDONLY);
    if (file_fd < 0) {
        send_custom_error_page(client_fd, 500, "Internal Server Error", "Nao foi possivel ler o arquivo do disco", raw_uri);
        close(client_fd);
        return NULL;
    }

    const char *mime = get_mime_type(full_fs_path);
    char resp_hdr[1024];
    size_t bytes_to_send = is_partial ? (size_t)(range_end - range_start + 1) : (size_t)st.st_size;

    if (is_partial) {
        snprintf(resp_hdr, sizeof(resp_hdr),
            "HTTP/1.1 206 Partial Content\r\n"
            "Server: %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Range: bytes %lld-%lld/%lld\r\n"
            "Content-Length: %zu\r\n"
            "Accept-Ranges: bytes\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "Connection: close\r\n\r\n",
            SERVER_BANNER, mime, (long long)range_start, (long long)range_end, (long long)st.st_size, bytes_to_send);
        lseek(file_fd, range_start, SEEK_SET);
    } else {
        snprintf(resp_hdr, sizeof(resp_hdr),
            "HTTP/1.1 200 OK\r\n"
            "Server: %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Accept-Ranges: bytes\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "Connection: close\r\n\r\n",
            SERVER_BANNER, mime, bytes_to_send);
    }

    send(client_fd, resp_hdr, strlen(resp_hdr), 0);

    char chunk[CHUNK_SIZE];
    size_t total_sent = 0;
    while (total_sent < bytes_to_send) {
        size_t to_r = (bytes_to_send - total_sent > sizeof(chunk)) ? sizeof(chunk) : (bytes_to_send - total_sent);
        ssize_t r = read(file_fd, chunk, to_r);
        if (r <= 0) break;
        send(client_fd, chunk, r, 0);
        total_sent += r;
    }

    close(file_fd);
    close(client_fd);
    return NULL;
}

int main(int argc, char *argv[]) {
    utilipc_init();
    ensure_www_structure();

    int port = 8080;
    if (argc >= 2) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            printf("Usage: simplehost [PORT]\n");
            printf("Modular Hardened HTTP Server with Custom 404 & File Explorer.\n");
            return 0;
        }
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) port = 8080;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("simplehost: socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "simplehost: erro ao escutar na porta %d: %s\n", port, strerror(errno));
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 32) < 0) {
        perror("simplehost: listen");
        close(server_fd);
        return 1;
    }

    printf("\n\033[1;35m=================================================================================\033[0m\n");
    printf("\033[1;35m[ simplehost 2.5 - Servidor Web Modular, Blindado & com Protecoes 404 ]\033[0m\n");
    printf("\033[1;35m=================================================================================\033[0m\n\n");
    printf("  • Raiz do Servidor   : \033[1;36m%s\033[0m\n", g_www_root);
    printf("  • Pasta de Uploads   : \033[1;36m%s/uploads\033[0m\n", g_www_root);
    printf("  • Porta TCP Ativa    : \033[1;33m%d\033[0m\n", port);
    printf("  • Acesso Local       : \033[1;32mhttp://localhost:%d/\033[0m\n", port);
    printf("  • Explorador Web     : \033[1;32mhttp://localhost:%d/uploads\033[0m\n\n", port);
    printf("\033[0;90m[Servidor modular ativo com pagina 404 personalizada e protecao contra Path Traversal]\033[0m\n\n");

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "simplehost: running on port %d (modular)", port);
    utilipc_write_status(-1, -1, -1, log_msg);

    while (1) {
        client_task_t *task = malloc(sizeof(client_task_t));
        if (!task) continue;

        socklen_t clen = sizeof(task->client_addr);
        task->client_fd = accept(server_fd, (struct sockaddr *)&task->client_addr, &clen);
        if (task->client_fd < 0) {
            free(task);
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler_thread, task) != 0) {
            close(task->client_fd);
            free(task);
        }
    }

    close(server_fd);
    utilipc_close();
    return 0;
}
