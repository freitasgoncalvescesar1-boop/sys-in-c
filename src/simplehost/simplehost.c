#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "../libutilipc/utilipc.h"

static void ensure_www_dir(char *out_path, size_t max_len) {
    const char *home = getenv("HOME");
    if (!home || strlen(home) == 0) home = ".";

    snprintf(out_path, max_len, "%s/simplehost_www", home);
    mkdir(out_path, 0755);

    char index_path[1024];
    snprintf(index_path, sizeof(index_path), "%s/index.html", out_path);

    struct stat st;
    if (stat(index_path, &st) != 0) {
        FILE *fp = fopen(index_path, "w");
        if (fp) {
            fprintf(fp, "<!DOCTYPE html>\n<html>\n<head>\n");
            fprintf(fp, "  <meta charset=\"UTF-8\">\n");
            fprintf(fp, "  <title>simplehost - Welcome</title>\n");
            fprintf(fp, "  <style>\n");
            fprintf(fp, "    body { font-family: sans-serif; background: #1e1e2e; color: #cdd6f4; text-align: center; padding: 50px; }\n");
            fprintf(fp, "    h1 { color: #a6e3a1; font-size: 2.5em; }\n");
            fprintf(fp, "    p { font-size: 1.2em; color: #bac2de; }\n");
            fprintf(fp, "    .box { background: #313244; padding: 30px; border-radius: 12px; display: inline-block; box-shadow: 0 8px 24px rgba(0,0,0,0.4); }\n");
            fprintf(fp, "  </style>\n</head>\n<body>\n");
            fprintf(fp, "  <div class=\"box\">\n");
            fprintf(fp, "    <h1>🚀 simplehost is Live!</h1>\n");
            fprintf(fp, "    <p>Edit this file at <code>~/simplehost_www/index.html</code></p>\n");
            fprintf(fp, "    <p>Simple static web server powered by <strong>utils-in-c</strong></p>\n");
            fprintf(fp, "  </div>\n</body>\n</html>\n");
            fclose(fp);
        }
    }
}

static const char *get_mime_type(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot) return "text/plain";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) return "text/html; charset=UTF-8";
    if (strcmp(dot, ".css") == 0) return "text/css";
    if (strcmp(dot, ".js") == 0) return "application/javascript";
    if (strcmp(dot, ".json") == 0) return "application/json";
    if (strcmp(dot, ".png") == 0) return "image/png";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(dot, ".svg") == 0) return "image/svg+xml";
    return "text/plain";
}

int main(int argc, char *argv[]) {
    utilipc_init();

    int port = 8080;
    if (argc >= 2) port = atoi(argv[1]);
    if (port <= 0 || port > 65535) port = 8080;

    char www_dir[1024];
    if (argc >= 3) {
        strncpy(www_dir, argv[2], sizeof(www_dir) - 1);
        www_dir[sizeof(www_dir) - 1] = '\0';
        mkdir(www_dir, 0755);
    } else {
        ensure_www_dir(www_dir, sizeof(www_dir));
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        utilipc_close();
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
        perror("Bind failed");
        close(server_fd);
        utilipc_close();
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        close(server_fd);
        utilipc_close();
        return 1;
    }

    printf("==========================================\n");
    printf("[simplehost - Local Web Server Running]\n");
    printf("==========================================\n");
    printf("  • Root Directory : %s\n", www_dir);
    printf("  • Port           : %d\n", port);
    printf("  • URL            : \033[1;32mhttp://localhost:%d/\033[0m\n", port);
    printf("==========================================\n");
    printf("[Serving files directly | Press Ctrl+C to stop]\n\n");

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "simplehost: running on port %d", port);
    utilipc_write_status(-1, -1, -1, log_msg);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) continue;

        char request[2048];
        ssize_t bytes = recv(client_fd, request, sizeof(request) - 1, 0);
        if (bytes <= 0) {
            close(client_fd);
            continue;
        }
        request[bytes] = '\0';

        char req_path[512] = "/index.html";
        sscanf(request, "GET %511s", req_path);

        if (strcmp(req_path, "/") == 0) {
            strcpy(req_path, "/index.html");
        }

        char file_path[1024];
        snprintf(file_path, sizeof(file_path), "%s%s", www_dir, req_path);

        FILE *fp = fopen(file_path, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long fsize = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            const char *mime = get_mime_type(file_path);

            char header[512];
            snprintf(header, sizeof(header),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %ld\r\n"
                "Connection: close\r\n\r\n", mime, fsize);

            send(client_fd, header, strlen(header), 0);

            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
                send(client_fd, buf, n, 0);
            }
            fclose(fp);
        } else {
            const char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<h1>404 Not Found</h1>";
            send(client_fd, not_found, strlen(not_found), 0);
        }

        close(client_fd);
    }

    close(server_fd);
    utilipc_close();
    return 0;
}
