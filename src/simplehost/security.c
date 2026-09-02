#include "server.h"

void url_decode(const char *src, char *dst, size_t dst_len) {
    size_t d = 0;
    while (*src && d < dst_len - 1) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = { src[1], src[2], '\0' };
            dst[d++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[d++] = ' ';
            src++;
        } else {
            dst[d++] = *src++;
        }
    }
    dst[d] = '\0';
}

int is_safe_canonical_path(const char *rel_path, char *out_full_path, size_t max_len) {
    char decoded[1024];
    url_decode(rel_path, decoded, sizeof(decoded));

    char *q = strchr(decoded, '?');
    if (q) *q = '\0';

    if (strstr(decoded, "..") != NULL || strchr(decoded, '\\') != NULL) {
        return 0;
    }

    if (decoded[0] == '/') {
        snprintf(out_full_path, max_len, "%s%s", g_www_root, decoded);
    } else {
        snprintf(out_full_path, max_len, "%s/%s", g_www_root, decoded);
    }

    if (strncmp(out_full_path, g_www_root, strlen(g_www_root)) != 0) {
        return 0;
    }
    return 1;
}

const char *get_mime_type(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot) return "application/octet-stream";
    if (strcasecmp(dot, ".html") == 0 || strcasecmp(dot, ".htm") == 0) return "text/html; charset=UTF-8";
    if (strcasecmp(dot, ".css") == 0) return "text/css; charset=UTF-8";
    if (strcasecmp(dot, ".js") == 0) return "application/javascript; charset=UTF-8";
    if (strcasecmp(dot, ".json") == 0) return "application/json; charset=UTF-8";
    if (strcasecmp(dot, ".png") == 0) return "image/png";
    if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(dot, ".gif") == 0) return "image/gif";
    if (strcasecmp(dot, ".svg") == 0) return "image/svg+xml";
    if (strcasecmp(dot, ".ico") == 0) return "image/x-icon";
    if (strcasecmp(dot, ".mp3") == 0) return "audio/mpeg";
    if (strcasecmp(dot, ".wav") == 0) return "audio/wav";
    if (strcasecmp(dot, ".mp4") == 0) return "video/mp4";
    if (strcasecmp(dot, ".pdf") == 0) return "application/pdf";
    if (strcasecmp(dot, ".zip") == 0 || strcasecmp(dot, ".kr") == 0) return "application/zip";
    if (strcasecmp(dot, ".txt") == 0 || strcasecmp(dot, ".c") == 0 || strcasecmp(dot, ".h") == 0 || strcasecmp(dot, ".py") == 0) return "text/plain; charset=UTF-8";
    return "application/octet-stream";
}

int handle_multipart_upload(int client_fd, const char *header_buf, size_t header_len, ssize_t total_first_read) {
    (void)header_len;
    char *ct_str = strcasestr(header_buf, "Content-Type: multipart/form-data;");
    if (!ct_str) return -1;

    char *boundary_str = strcasestr(ct_str, "boundary=");
    if (!boundary_str) return -1;
    boundary_str += 9;
    char boundary[128] = "";
    sscanf(boundary_str, "%127s", boundary);
    char *sc = strchr(boundary, ';'); if (sc) *sc = '\0';
    char *cr = strchr(boundary, '\r'); if (cr) *cr = '\0';

    char *cl_str = strcasestr(header_buf, "Content-Length: ");
    size_t content_len = 0;
    if (cl_str) content_len = strtoull(cl_str + 16, NULL, 10);
    if (content_len == 0 || content_len > MAX_UPLOAD_SIZE) return -1;

    char *body_start = strstr(header_buf, "\r\n\r\n");
    if (!body_start) return -1;
    body_start += 4;
    size_t headers_sz = body_start - header_buf;
    size_t body_in_mem = total_first_read - headers_sz;

    char *fn_ptr = strcasestr(body_start, "filename=\"");
    char safe_filename[128] = "upload.bin";
    if (fn_ptr) {
        fn_ptr += 10;
        char *fn_end = strchr(fn_ptr, '"');
        if (fn_end) {
            size_t flen = fn_end - fn_ptr;
            if (flen >= sizeof(safe_filename)) flen = sizeof(safe_filename) - 1;
            strncpy(safe_filename, fn_ptr, flen);
            safe_filename[flen] = '\0';
        }
    }

    char clean_name[128] = "";
    size_t c_idx = 0;
    for (size_t i = 0; safe_filename[i] != '\0' && c_idx < sizeof(clean_name) - 1; i++) {
        unsigned char c = (unsigned char)safe_filename[i];
        if (isalnum(c) || c == '.' || c == '_' || c == '-') clean_name[c_idx++] = c;
    }
    clean_name[c_idx] = '\0';
    if (strlen(clean_name) == 0) snprintf(clean_name, sizeof(clean_name), "upload_%ld.bin", time(NULL));

    char save_path[1024];
    snprintf(save_path, sizeof(save_path), "%s/uploads/%s", g_www_root, clean_name);

    char *data_start = strstr(body_start, "\r\n\r\n");
    if (!data_start) return -1;
    data_start += 4;
    size_t part_hdr_sz = data_start - body_start;
    size_t raw_data_len = body_in_mem - part_hdr_sz;

    FILE *out_fp = fopen(save_path, "wb");
    if (!out_fp) return -1;

    size_t total_written = 0;
    fwrite(data_start, 1, raw_data_len, out_fp);
    total_written += raw_data_len;

    char chunk[CHUNK_SIZE];
    size_t remaining = content_len - body_in_mem;
    while (remaining > 0) {
        ssize_t n = recv(client_fd, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        fwrite(chunk, 1, n, out_fp);
        total_written += n;
        remaining -= n;
    }

    long fsize = ftell(out_fp);
    size_t boundary_trail_len = strlen(boundary) + 8;
    if (fsize > (long)boundary_trail_len) {
        ftruncate(fileno(out_fp), fsize - boundary_trail_len);
    }
    fclose(out_fp);

    printf("  \033[1;32m[UPLOAD OK]\033[0m Arquivo salvo: '%s' (%zu bytes)\n", clean_name, total_written);
    return 0;
}
