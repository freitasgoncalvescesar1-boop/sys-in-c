#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../libutilipc/utilipc.h"

#define MAX_DIRS 50
#define COLOR_RESET   "\033[0m"
#define COLOR_GB      "\033[1;31m"
#define COLOR_MB      "\033[1;36m"
#define COLOR_PATH    "\033[0;33m"

typedef struct {
    char path[2048];
    double size_mb;
} FileInfo;

static FileInfo *found_files = NULL;
static size_t file_count = 0;
static size_t file_capacity = 0;
static int scanned_dir_count = 0;
static double min_size_mb = 10.0;

static const char *ignored_dirs[] = {
    ".git", "node_modules", "vendor", ".cache", ".vscode", ".idea", "build", "dist", NULL
};

static int is_ignored(const char *name) {
    for (int i = 0; ignored_dirs[i] != NULL; i++) {
        if (strcmp(name, ignored_dirs[i]) == 0) return 1;
    }
    return 0;
}

static void add_file(const char *path, double size_mb) {
    if (file_count >= file_capacity) {
        file_capacity = (file_capacity == 0) ? 64 : file_capacity * 2;
        found_files = realloc(found_files, file_capacity * sizeof(FileInfo));
    }
    strncpy(found_files[file_count].path, path, sizeof(found_files[file_count].path) - 1);
    found_files[file_count].path[sizeof(found_files[file_count].path) - 1] = '\0';
    found_files[file_count].size_mb = size_mb;
    file_count++;
}

static int compare_files(const void *a, const void *b) {
    const FileInfo *fa = (const FileInfo *)a;
    const FileInfo *fb = (const FileInfo *)b;
    if (fb->size_mb > fa->size_mb) return 1;
    if (fb->size_mb < fa->size_mb) return -1;
    return 0;
}

static void scan_directory(const char *dir_path) {
    if (scanned_dir_count >= MAX_DIRS) return;
    scanned_dir_count++;

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    char path[2048];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (is_ignored(entry->d_name)) continue;

        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

#ifdef DT_DIR
        if (entry->d_type == DT_DIR) {
            if (scanned_dir_count < MAX_DIRS) scan_directory(path);
            continue;
        } else if (entry->d_type == DT_REG) {
            struct stat st;
            if (stat(path, &st) == 0) {
                double size_mb = (double)st.st_size / (1024.0 * 1024.0);
                if (size_mb >= min_size_mb) add_file(path, size_mb);
            }
            continue;
        }
#endif

        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (scanned_dir_count < MAX_DIRS) scan_directory(path);
            } else if (S_ISREG(st.st_mode)) {
                double size_mb = (double)st.st_size / (1024.0 * 1024.0);
                if (size_mb >= min_size_mb) add_file(path, size_mb);
            }
        }
    }
    closedir(dir);
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc > 1) {
        min_size_mb = atof(argv[1]);
        if (min_size_mb <= 0) min_size_mb = 10.0;
    }

    printf("======================\n");
    printf("[Search Min Size: %.2f MB | Dir Limit: %d]\n", min_size_mb, MAX_DIRS);
    printf("======================\n");

    scan_directory(".");
    qsort(found_files, file_count, sizeof(FileInfo), compare_files);

    for (size_t i = 0; i < file_count; i++) {
        if (found_files[i].size_mb >= 1024.0) {
            printf("  [%s%.2f GB%s] %s%s%s\n", COLOR_GB, found_files[i].size_mb / 1024.0, COLOR_RESET, COLOR_PATH, found_files[i].path, COLOR_RESET);
        } else {
            printf("  [%s%.2f MB%s] %s%s%s\n", COLOR_MB, found_files[i].size_mb, COLOR_RESET, COLOR_PATH, found_files[i].path, COLOR_RESET);
        }
    }

    printf("======================\n");
    printf("[Scan finished. Subdirectories: %d | Found: %lu]\n", scanned_dir_count, (unsigned long)file_count);
    printf("======================\n");

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "bigfiles: found %lu files >= %.1fMB", (unsigned long)file_count, min_size_mb);
    utilipc_write_status(-1, -1, -1, log_msg);

    free(found_files);
    utilipc_close();
    return 0;
}
