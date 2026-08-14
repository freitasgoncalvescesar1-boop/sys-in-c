#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include "../libutilipc/utilipc.h"

#define MAX_DEPTH 15
#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_GROUP   "\033[1;33m"
#define COLOR_PATH    "\033[0;36m"
#define COLOR_SIZE    "\033[1;32m"

static int compute_file_sha256(const char *filepath, uint8_t hash_out[32]) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return -1;

    SHA256_CTX ctx;
    sha256_init(&ctx);

    unsigned char buf[65536];
    size_t bytes = 0;
    while ((bytes = fread(buf, 1, sizeof(buf), fp)) > 0) {
        sha256_update(&ctx, buf, bytes);
    }
    fclose(fp);

    sha256_final(&ctx, hash_out);
    return 0;
}

typedef struct {
    char path[2048];
    off_t size;
    uint8_t hash[32];
    int hash_computed;
    int group_id;
} FileEntry;

static FileEntry *files = NULL;
static size_t file_count = 0;
static size_t file_capacity = 0;

static const char *ignored_dirs[] = {
    ".git", "node_modules", "vendor", ".cache", ".vscode", ".idea", "build", "dist",
    "/proc", "/sys", "/dev", "/system", "/data", NULL
};

static int is_ignored(const char *name) {
    for (int i = 0; ignored_dirs[i] != NULL; i++) {
        if (strcmp(name, ignored_dirs[i]) == 0 || strstr(name, ignored_dirs[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

static void add_file(const char *path, off_t size) {
    if (file_count >= file_capacity) {
        file_capacity = (file_capacity == 0) ? 128 : file_capacity * 2;
        files = realloc(files, file_capacity * sizeof(FileEntry));
    }
    strncpy(files[file_count].path, path, sizeof(files[file_count].path) - 1);
    files[file_count].path[sizeof(files[file_count].path) - 1] = '\0';
    files[file_count].size = size;
    files[file_count].hash_computed = 0;
    files[file_count].group_id = -1;
    file_count++;
}

static void scan_recursive(const char *dir_path, int depth) {
    if (depth > MAX_DEPTH) return;
    if (is_ignored(dir_path)) return;

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    char path[2048];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (entry->d_name[0] == '.') continue;
        if (is_ignored(entry->d_name)) continue;

        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (lstat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                scan_recursive(path, depth + 1);
            } else if (S_ISREG(st.st_mode) && st.st_size > 0) {
                add_file(path, st.st_size);
            }
        }
    }
    closedir(dir);
}

static int compare_by_size(const void *a, const void *b) {
    const FileEntry *fa = (const FileEntry *)a;
    const FileEntry *fb = (const FileEntry *)b;
    if (fb->size > fa->size) return 1;
    if (fb->size < fa->size) return -1;
    return 0;
}

int main(int argc, char *argv[]) {
    utilipc_init();

    const char *start_dir = ".";
    if (argc >= 2) start_dir = argv[1];

    printf("%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ fdup - Duplicate File Finder ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("  Scanning directory: %s...\n", start_dir);

    scan_recursive(start_dir, 1);

    if (file_count == 0) {
        printf("No files found.\n");
        free(files);
        utilipc_close();
        return 0;
    }

    qsort(files, file_count, sizeof(FileEntry), compare_by_size);

    int current_group = 0;
    size_t total_wasted_bytes = 0;

    for (size_t i = 0; i < file_count; i++) {
        if (files[i].group_id != -1) continue;

        size_t match_count = 0;
        for (size_t j = i + 1; j < file_count; j++) {
            if (files[j].size != files[i].size) break;
            match_count++;
        }

        if (match_count == 0) continue;

        if (!files[i].hash_computed) {
            if (compute_file_sha256(files[i].path, files[i].hash) < 0) continue;
            files[i].hash_computed = 1;
        }

        int group_found = 0;
        for (size_t j = i + 1; j < file_count; j++) {
            if (files[j].size != files[i].size) break;

            if (!files[j].hash_computed) {
                if (compute_file_sha256(files[j].path, files[j].hash) < 0) continue;
                files[j].hash_computed = 1;
            }

            if (memcmp(files[i].hash, files[j].hash, 32) == 0) {
                if (!group_found) {
                    current_group++;
                    files[i].group_id = current_group;
                    group_found = 1;

                    double size_mb = (double)files[i].size / (1024.0 * 1024.0);
                    printf("\n%s[Group %d - Size: %s%.2f MB%s | SHA256: ", COLOR_GROUP, current_group, COLOR_SIZE, size_mb, COLOR_GROUP);
                    for (int h = 0; h < 8; h++) printf("%02x", files[i].hash[h]);
                    printf("...]%s\n", COLOR_RESET);
                    printf("  • %s%s%s\n", COLOR_PATH, files[i].path, COLOR_RESET);
                }

                files[j].group_id = current_group;
                printf("  • %s%s%s\n", COLOR_PATH, files[j].path, COLOR_RESET);
                total_wasted_bytes += files[i].size;
            }
        }
    }

    printf("\n%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);
    double wasted_mb = (double)total_wasted_bytes / (1024.0 * 1024.0);
    if (wasted_mb >= 1024.0) {
        printf("[Done. Duplicate Groups: %d | Wasted Space: %s%.2f GB%s]\n", current_group, COLOR_SIZE, wasted_mb / 1024.0, COLOR_RESET);
    } else {
        printf("[Done. Duplicate Groups: %d | Wasted Space: %s%.2f MB%s]\n", current_group, COLOR_SIZE, wasted_mb, COLOR_RESET);
    }
    printf("%s==========================================%s\n", COLOR_TITLE, COLOR_RESET);

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "fdup: found %d duplicate groups (wasted: %.1fMB)", current_group, wasted_mb);
    utilipc_write_status(-1, -1, -1, log_msg);

    free(files);
    utilipc_close();
    return 0;
}
