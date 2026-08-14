#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include "../libutilipc/utilipc.h"

#define DEFAULT_MAX_DEPTH 15
#define COLOR_RESET   "\033[0m"
#define COLOR_MATCH   "\033[1;32m"
#define COLOR_DIR     "\033[1;34m"
#define COLOR_PATH    "\033[0;33m"
#define COLOR_SIZE    "\033[1;36m"

static int max_depth = DEFAULT_MAX_DEPTH;
static int total_matches = 0;
static int total_scanned = 0;

static const char *ignored_system_paths[] = {
    "/proc", "/sys", "/dev", "/system", "/vendor",
    "/data/dalvik-cache", "/storage/emulated/0/Android/data",
    "/storage/emulated/0/Android/obb", NULL
};

static int is_system_ignored(const char *path) {
    for (int i = 0; ignored_system_paths[i] != NULL; i++) {
        if (strncmp(path, ignored_system_paths[i], strlen(ignored_system_paths[i])) == 0) return 1;
    }
    return 0;
}

static int strcasestr_custom(const char *haystack, const char *needle) {
    if (!needle || !*needle) return 1;
    for (; *haystack; haystack++) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
            const char *h = haystack + 1;
            const char *n = needle + 1;
            while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
                h++; n++;
            }
            if (!*n) return 1;
        }
    }
    return 0;
}

static void search_recursive(const char *dir_path, const char *pattern, int current_depth) {
    if (current_depth > max_depth) return;
    if (is_system_ignored(dir_path)) return;

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    char path[2048];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (entry->d_name[0] == '.') continue;

        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        total_scanned++;

        int matches = strcasestr_custom(entry->d_name, pattern);

#ifdef DT_DIR
        if (entry->d_type == DT_DIR) {
            if (matches) {
                printf("  [%sDIR%s]  %s%s/%s%s\n", COLOR_DIR, COLOR_RESET, COLOR_PATH, path, COLOR_MATCH, COLOR_RESET);
                total_matches++;
            }
            search_recursive(path, pattern, current_depth + 1);
            continue;
        } else if (entry->d_type == DT_REG) {
            if (matches) {
                struct stat st;
                if (stat(path, &st) == 0) {
                    double size_mb = (double)st.st_size / (1024.0 * 1024.0);
                    if (size_mb >= 1.0) {
                        printf("  [%s%.2f MB%s] %s%s%s\n", COLOR_SIZE, size_mb, COLOR_RESET, COLOR_PATH, path, COLOR_RESET);
                    } else {
                        printf("  [%s%.1f KB%s] %s%s%s\n", COLOR_SIZE, (double)st.st_size / 1024.0, COLOR_RESET, COLOR_PATH, path, COLOR_RESET);
                    }
                } else {
                    printf("  [FILE] %s%s%s\n", COLOR_PATH, path, COLOR_RESET);
                }
                total_matches++;
            }
            continue;
        }
#endif

        struct stat st;
        if (lstat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (matches) {
                    printf("  [%sDIR%s]  %s%s/%s%s\n", COLOR_DIR, COLOR_RESET, COLOR_PATH, path, COLOR_MATCH, COLOR_RESET);
                    total_matches++;
                }
                search_recursive(path, pattern, current_depth + 1);
            } else if (S_ISREG(st.st_mode)) {
                if (matches) {
                    double size_mb = (double)st.st_size / (1024.0 * 1024.0);
                    if (size_mb >= 1.0) {
                        printf("  [%s%.2f MB%s] %s%s%s\n", COLOR_SIZE, size_mb, COLOR_RESET, COLOR_PATH, path, COLOR_RESET);
                    } else {
                        printf("  [%s%.1f KB%s] %s%s%s\n", COLOR_SIZE, (double)st.st_size / 1024.0, COLOR_RESET, COLOR_PATH, path, COLOR_RESET);
                    }
                    total_matches++;
                }
            }
        }
    }
    closedir(dir);
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 2) {
        printf("Usage: ffind <search_pattern> [start_dir] [max_depth]\n");
        utilipc_close();
        return 1;
    }

    const char *pattern = argv[1];
    const char *start_dir = (argc >= 3) ? argv[2] : ".";
    if (argc >= 4) {
        max_depth = atoi(argv[3]);
        if (max_depth <= 0) max_depth = DEFAULT_MAX_DEPTH;
    }

    printf("======================\n");
    printf("[ffind - Searching for '%s' in '%s' (Max Depth: %d)]\n", pattern, start_dir, max_depth);
    printf("======================\n");

    search_recursive(start_dir, pattern, 1);

    printf("======================\n");
    printf("[Done. Matches: %d | Items Scanned: %d]\n", total_matches, total_scanned);
    printf("======================\n");

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "ffind: searched '%s' (%d matches)", pattern, total_matches);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
