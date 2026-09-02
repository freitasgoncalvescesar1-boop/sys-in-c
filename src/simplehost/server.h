#ifndef SIMPLEHOST_SERVER_H
#define SIMPLEHOST_SERVER_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include "../libutilipc/utilipc.h"

#define MAX_UPLOAD_SIZE (100 * 1024 * 1024) // Limite de 100 MB
#define CHUNK_SIZE      65536
#define SERVER_BANNER   "sys-in-c-simplehost/2.5 (Hardened Modular Engine)"

typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
} client_task_t;

extern char g_www_root[1024];

// Funções de Segurança (security.c)
void url_decode(const char *src, char *dst, size_t dst_len);
int is_safe_canonical_path(const char *rel_path, char *out_full_path, size_t max_len);
const char *get_mime_type(const char *filename);
int handle_multipart_upload(int client_fd, const char *header_buf, size_t header_len, ssize_t total_first_read);

// Funções de Páginas HTTP (http_pages.c)
void ensure_www_structure(void);
void send_custom_error_page(int client_fd, int status_code, const char *status_title, const char *message, const char *missing_target);
void send_directory_listing(int client_fd, const char *fs_path, const char *url_path);

#endif
