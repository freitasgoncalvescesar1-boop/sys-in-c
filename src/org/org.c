#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../libutilipc/utilipc.h"

static void ensure_dot_ext(const char *in, char *out, size_t max_len) {
    if (in[0] == '.') {
        snprintf(out, max_len, "%s", in);
    } else {
        snprintf(out, max_len, ".%s", in);
    }
}

static int ends_with(const char *str, const char *suffix) {
    size_t len = strlen(str);
    size_t slen = strlen(suffix);
    if (slen > len) return 0;
    return strcasecmp(str + len - slen, suffix) == 0;
}

static void filter_and_move(const char *ext_input, const char *dest_dir) {
    char ext[64];
    ensure_dot_ext(ext_input, ext, sizeof(ext));

    mkdir(dest_dir, 0755);

    DIR *dir = opendir(".");
    if (!dir) return;

    struct dirent *entry;
    int moved_count = 0;
    char target_path[2048];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        struct stat st;
        if (stat(entry->d_name, &st) == 0 && S_ISREG(st.st_mode)) {
            if (ends_with(entry->d_name, ext)) {
                snprintf(target_path, sizeof(target_path), "%s/%s", dest_dir, entry->d_name);
                if (rename(entry->d_name, target_path) == 0) {
                    printf(" [MOVED] %s -> %s\n", entry->d_name, target_path);
                    moved_count++;
                }
            }
        }
    }
    closedir(dir);

    printf("======================\n");
    printf("[Done. Moved %d files with extension '%s' to '%s']\n", moved_count, ext, dest_dir);
    printf("======================\n");

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "org: moved %d files (%s -> %s)", moved_count, ext, dest_dir);
    utilipc_write_status(-1, -1, -1, log_msg);
}

static void change_extension(const char *old_ext_in, const char *new_ext_in) {
    char old_ext[64], new_ext[64];
    ensure_dot_ext(old_ext_in, old_ext, sizeof(old_ext));
    ensure_dot_ext(new_ext_in, new_ext, sizeof(new_ext));

    DIR *dir = opendir(".");
    if (!dir) return;

    struct dirent *entry;
    int renamed_count = 0;
    char new_name[1024];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        struct stat st;
        if (stat(entry->d_name, &st) == 0 && S_ISREG(st.st_mode)) {
            if (ends_with(entry->d_name, old_ext)) {
                size_t base_len = strlen(entry->d_name) - strlen(old_ext);
                snprintf(new_name, sizeof(new_name), "%.*s%s", (int)base_len, entry->d_name, new_ext);

                if (rename(entry->d_name, new_name) == 0) {
                    printf(" [RENAMED] %s -> %s\n", entry->d_name, new_name);
                    renamed_count++;
                }
            }
        }
    }
    closedir(dir);

    printf("======================\n");
    printf("[Done. Renamed %d files from '%s' to '%s']\n", renamed_count, old_ext, new_ext);
    printf("======================\n");

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "org: renamed %d files (%s -> %s)", renamed_count, old_ext, new_ext);
    utilipc_write_status(-1, -1, -1, log_msg);
}

static void auto_organize_by_extension(void) {
    DIR *dir = opendir(".");
    if (!dir) return;

    struct dirent *entry;
    int moved_count = 0;
    char target_path[2048];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        struct stat st;
        if (stat(entry->d_name, &st) == 0 && S_ISREG(st.st_mode)) {
            char *dot = strrchr(entry->d_name, '.');
            if (dot && dot != entry->d_name) {
                const char *ext = dot + 1;
                mkdir(ext, 0755);
                snprintf(target_path, sizeof(target_path), "%s/%s", ext, entry->d_name);
                if (rename(entry->d_name, target_path) == 0) {
                    printf(" [AUTO-ORG] %s -> %s/\n", entry->d_name, ext);
                    moved_count++;
                }
            }
        }
    }
    closedir(dir);

    printf("======================\n");
    printf("[Done. Auto-organized %d files by extension]\n", moved_count);
    printf("======================\n");
}

static void clean_empty_dirs(void) {
    DIR *dir = opendir(".");
    if (!dir) return;

    struct dirent *entry;
    int cleaned_count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        struct stat st;
        if (stat(entry->d_name, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (rmdir(entry->d_name) == 0) {
                printf(" [REMOVED EMPTY DIR] %s/\n", entry->d_name);
                cleaned_count++;
            }
        }
    }
    closedir(dir);

    printf("======================\n");
    printf("[Done. Removed %d empty directories]\n", cleaned_count);
    printf("======================\n");
}

int main(int argc, char *argv[]) {
    utilipc_init();

    if (argc < 2) {
        printf("Usage:\n");
        printf("  org -f <.ext> mv to <path>  (Move files by extension)\n");
        printf("  org -c <.ext1> to <.ext2>   (Change extension)\n");
        printf("  org -e | --by-ext            (Auto-organize all files by ext)\n");
        printf("  org -z | --clean             (Remove empty directories)\n");
        return 1;
    }

    if (strcmp(argv[1], "-f") == 0) {
        if (argc < 5) {
            printf("Error: Format is 'org -f <.ext> mv to <destination_path>'\n");
            return 1;
        }
        const char *ext = argv[2];
        const char *dest = argv[argc - 1]; // Pega o último argumento como destino
        filter_and_move(ext, dest);
    }
    else if (strcmp(argv[1], "-c") == 0) {
        if (argc < 4) {
            printf("Error: Format is 'org -c <.old_ext> to <.new_ext>'\n");
            return 1;
        }
        const char *old_ext = argv[2];
        const char *new_ext = argv[argc - 1];
        change_extension(old_ext, new_ext);
    }
    else if (strcmp(argv[1], "-e") == 0 || strcmp(argv[1], "--by-ext") == 0) {
        auto_organize_by_extension();
    }
    else if (strcmp(argv[1], "-z") == 0 || strcmp(argv[1], "--clean") == 0) {
        clean_empty_dirs();
    }
    else {
        printf("Error: Unknown option '%s'\n", argv[1]);
        return 1;
    }

    utilipc_close();
    return 0;
}
