#include "server.h"

void ensure_www_structure(void) {
    const char *home = getenv("HOME");
    if (!home || strlen(home) == 0) home = ".";

    snprintf(g_www_root, sizeof(g_www_root), "%s/simplehost_www", home);
    mkdir(g_www_root, 0755);

    char uploads_path[1024];
    snprintf(uploads_path, sizeof(uploads_path), "%s/uploads", g_www_root);
    mkdir(uploads_path, 0755);

    char index_path[1024];
    snprintf(index_path, sizeof(index_path), "%s/index.html", g_www_root);

    struct stat st;
    if (stat(index_path, &st) != 0) {
        FILE *fp = fopen(index_path, "w");
        if (fp) {
            fprintf(fp,
                "<!DOCTYPE html>\n"
                "<html lang=\"pt-BR\">\n"
                "<head>\n"
                "  <meta charset=\"UTF-8\">\n"
                "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
                "  <title>simplehost 2.5 - Secure Web Server</title>\n"
                "  <style>\n"
                "    :root { --bg: #181825; --card: #1e1e2e; --accent: #a6e3a1; --text: #cdd6f4; --sub: #bac2de; --btn: #89b4fa; }\n"
                "    body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 40px 20px; text-align: center; }\n"
                "    .container { max-width: 650px; margin: 0 auto; background: var(--card); padding: 35px; border-radius: 16px; box-shadow: 0 10px 30px rgba(0,0,0,0.5); border: 1px solid #313244; }\n"
                "    h1 { color: var(--accent); margin-top: 0; font-size: 2.2em; }\n"
                "    p { color: var(--sub); line-height: 1.6; }\n"
                "    .drop-zone { border: 2px dashed #45475a; border-radius: 12px; padding: 30px; margin: 25px 0; background: #11111b; cursor: pointer; transition: 0.2s; display: block; }\n"
                "    .drop-zone:hover { border-color: var(--accent); }\n"
                "    input[type=file] { display: none; }\n"
                "    .btn { background: var(--btn); color: #11111b; font-weight: bold; border: none; padding: 12px 28px; border-radius: 8px; cursor: pointer; font-size: 1em; text-decoration: none; display: inline-block; transition: 0.2s; }\n"
                "    .btn:hover { filter: brightness(1.1); transform: translateY(-2px); }\n"
                "    .links { margin-top: 25px; display: flex; justify-content: center; gap: 15px; }\n"
                "  </style>\n"
                "</head>\n"
                "<body>\n"
                "  <div class=\"container\">\n"
                "    <h1>🚀 simplehost 2.5</h1>\n"
                "    <p>Servidor Web Modular & Central de Arquivos Blindada com Protecao Anti-404.</p>\n"
                "    <form action=\"/upload\" method=\"POST\" enctype=\"multipart/form-data\" id=\"uploadForm\">\n"
                "      <label class=\"drop-zone\" for=\"fileInput\" id=\"dropLabel\">\n"
                "        📁 <strong>Clique aqui</strong> ou arraste um arquivo para fazer upload\n"
                "        <input type=\"file\" id=\"fileInput\" name=\"upload_file\" onchange=\"document.getElementById('dropLabel').innerHTML = '📄 ' + this.files[0].name;\" required>\n"
                "      </label><br>\n"
                "      <button type=\"submit\" class=\"btn\">Enviar Arquivo</button>\n"
                "    </form>\n"
                "    <div class=\"links\">\n"
                "      <a href=\"/uploads\" class=\"btn\" style=\"background:#cba6f7;\">📂 Explorar Arquivos (/uploads)</a>\n"
                "    </div>\n"
                "  </div>\n"
                "</body>\n"
                "</html>\n");
            fclose(fp);
        }
    }
}

// Renderiza Página de Erro 404 / 403 / 500 Estilizada em Catppuccin Dark
void send_custom_error_page(int client_fd, int status_code, const char *status_title, const char *message, const char *missing_target) {
    char html[4096];
    const char *color_accent = (status_code == 404) ? "#f38ba8" : (status_code == 403) ? "#fab387" : "#eba0ac";

    snprintf(html, sizeof(html),
        "<!DOCTYPE html>\n"
        "<html lang=\"pt-BR\">\n"
        "<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "  <title>%d - %s</title>\n"
        "  <style>\n"
        "    :root { --bg: #181825; --card: #1e1e2e; --accent: %s; --text: #cdd6f4; --sub: #a6adc8; --btn: #89b4fa; }\n"
        "    body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 20px; display: flex; justify-content: center; align-items: center; min-height: 85vh; text-align: center; }\n"
        "    .card { background: var(--card); padding: 45px 35px; border-radius: 20px; max-width: 560px; width: 100%%; box-shadow: 0 20px 40px rgba(0,0,0,0.6); border: 1px solid #313244; }\n"
        "    .code { font-size: 5.5rem; font-weight: 900; color: var(--accent); margin: 0; line-height: 1; text-shadow: 0 4px 20px rgba(243, 139, 168, 0.25); }\n"
        "    h2 { font-size: 1.7rem; margin: 20px 0 10px; color: var(--text); }\n"
        "    p { color: var(--sub); font-size: 1.1rem; line-height: 1.6; margin-bottom: 25px; }\n"
        "    .target-badge { background: #11111b; color: #f9e2af; padding: 6px 12px; border-radius: 8px; font-family: 'Courier New', monospace; font-size: 1em; word-break: break-all; border: 1px solid #45475a; display: inline-block; margin: 8px 0; }\n"
        "    .btn-group { display: flex; gap: 12px; justify-content: center; flex-wrap: wrap; margin-top: 20px; }\n"
        "    .btn { background: var(--btn); color: #11111b; text-decoration: none; font-weight: bold; padding: 13px 26px; border-radius: 10px; transition: 0.2s; font-size: 1rem; }\n"
        "    .btn:hover { filter: brightness(1.15); transform: translateY(-2px); }\n"
        "    .btn-sub { background: #313244; color: var(--text); }\n"
        "    .btn-sub:hover { background: #45475a; }\n"
        "  </style>\n"
        "</head>\n"
        "<body>\n"
        "  <div class=\"card\">\n"
        "    <div class=\"code\">%d</div>\n"
        "    <h2>%s</h2>\n"
        "    <p>%s<br><span class=\"target-badge\">%s</span></p>\n"
        "    <div class=\"btn-group\">\n"
        "      <a href=\"/\" class=\"btn\">🏠 Voltar para a Página Inicial</a>\n"
        "      <a href=\"/uploads\" class=\"btn btn-sub\">📂 Explorar Arquivos (/uploads)</a>\n"
        "    </div>\n"
        "  </div>\n"
        "</body>\n"
        "</html>\n",
        status_code, status_title, color_accent, status_code, status_title, message, missing_target ? missing_target : "/");

    size_t len = strlen(html);
    char hdr[512];
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Server: %s\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: %zu\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Connection: close\r\n\r\n", status_code, status_title, SERVER_BANNER, len);

    send(client_fd, hdr, strlen(hdr), 0);
    send(client_fd, html, len, 0);
}

void send_directory_listing(int client_fd, const char *fs_path, const char *url_path) {
    DIR *dir = opendir(fs_path);
    if (!dir) {
        send_custom_error_page(client_fd, 500, "Internal Server Error", "Nao foi possivel abrir o diretorio solicitado", url_path);
        return;
    }

    char *html_buf = malloc(128 * 1024);
    if (!html_buf) { closedir(dir); return; }

    char *p = html_buf;
    p += sprintf(p,
        "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'>"
        "<title>Explorador de Arquivos - %s</title><style>"
        "body{font-family:sans-serif;background:#181825;color:#cdd6f4;padding:30px;max-width:800px;margin:0 auto;}"
        "table{width:100%%;border-collapse:collapse;margin-top:20px;background:#1e1e2e;border-radius:12px;overflow:hidden;}"
        "th,td{padding:12px 16px;text-align:left;border-bottom:1px solid #313244;}"
        "th{background:#313244;color:#a6e3a1;}"
        "a{color:#89b4fa;text-decoration:none;} a:hover{text-decoration:underline;}"
        "</style></head><body>"
        "<h1>📂 Pasta: %s</h1><p><a href='/'>⬅ Voltar para a Pagina Inicial</a></p>"
        "<table><tr><th>Nome</th><th>Tamanho</th><th>Acao</th></tr>",
        url_path, url_path);

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        char item_fs_path[1024];
        snprintf(item_fs_path, sizeof(item_fs_path), "%s/%s", fs_path, de->d_name);
        struct stat st;
        if (stat(item_fs_path, &st) != 0) continue;

        char sz_str[32];
        if (S_ISDIR(st.st_mode)) {
            strcpy(sz_str, "DIR");
        } else {
            double mb = (double)st.st_size / (1024.0 * 1024.0);
            if (mb >= 1.0) snprintf(sz_str, sizeof(sz_str), "%.2f MB", mb);
            else snprintf(sz_str, sizeof(sz_str), "%.1f KB", (double)st.st_size / 1024.0);
        }

        p += sprintf(p, "<tr><td>%s <a href='%s/%s'>%s</a></td><td>%s</td><td><a href='%s/%s' download>⬇ Baixar</a></td></tr>",
                     S_ISDIR(st.st_mode) ? "📁" : "📄",
                     url_path, de->d_name, de->d_name,
                     sz_str,
                     url_path, de->d_name);
    }
    closedir(dir);

    p += sprintf(p, "</table></body></html>");

    size_t body_len = p - html_buf;
    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Server: %s\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: %zu\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Connection: close\r\n\r\n", SERVER_BANNER, body_len);

    send(client_fd, header, strlen(header), 0);
    send(client_fd, html_buf, body_len, 0);
    free(html_buf);
}
