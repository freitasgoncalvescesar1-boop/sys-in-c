#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_DIR     "\033[1;34m" // Azul
#define COLOR_EXEC    "\033[1;32m" // Verde
#define COLOR_LINK    "\033[1;36m" // Ciano
#define COLOR_ARCHIVE "\033[1;31m" // Vermelho
#define COLOR_SOCK    "\033[1;35m" // Magenta
#define COLOR_INODE   "\033[1;33m" // Amarelo

typedef struct {
    char name[256];
    char full_path[2048];
    struct stat st;
    char link_target[1024];
} FileEntry;

static int opt_l = 0, opt_a = 0, opt_A = 0, opt_h = 1, opt_i = 0, opt_S = 0, opt_t = 0, opt_r = 0, opt_1 = 0;

static void print_help(void) {
    low_print_banner("ls");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./ls [OPTIONS] [FILE/DIR...]\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Colorized directory content lister with Inode discovery and smart sorting.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-l%s                  Use long listing format\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-a, --all%s           Do not ignore entries starting with .\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-A, --almost-all%s    Do not list implied . and ..\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-i, --inode%s         Print the index number of each file (Inode ID)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --human-readable%s Print sizes in human readable format (e.g., 1K 234M 2G)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-S%s                  Sort by file size, largest first\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-t%s                  Sort by modification time, newest first\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-r, --reverse%s       Reverse order while sorting\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-1%s                  List one file per line\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s          Display this formatted help guide and exit\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --version%s       Display version and repository information\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sCOLOR SCHEME:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %sBlue%s      -> Directories\n", COLOR_DIR, COLOR_RESET);
    printf("  • %sGreen%s     -> Executable binaries and scripts\n", COLOR_EXEC, COLOR_RESET);
    printf("  • %sCyan%s      -> Symbolic links (atalho -> alvo)\n", COLOR_LINK, COLOR_RESET);
    printf("  • %sRed%s       -> Archive and compressed packages (.zip, .tar, .gz)\n\n", COLOR_ARCHIVE, COLOR_RESET);
}

static void format_size(off_t size, char *buf, size_t sz) {
    if (!opt_h) {
        snprintf(buf, sz, "%lld", (long long)size);
        return;
    }
    const char *units[] = {"B", "K", "M", "G", "T"};
    int i = 0;
    double d_size = (double)size;
    while (d_size >= 1024.0 && i < 4) {
        d_size /= 1024.0;
        i++;
    }
    if (i == 0) snprintf(buf, sz, "%lldB", (long long)size);
    else snprintf(buf, sz, "%.1f%s", d_size, units[i]);
}

static void mode_to_str(mode_t mode, char *str) {
    strcpy(str, "----------");
    if (S_ISDIR(mode)) str[0] = 'd';
    else if (S_ISLNK(mode)) str[0] = 'l';
    else if (S_ISCHR(mode)) str[0] = 'c';
    else if (S_ISBLK(mode)) str[0] = 'b';
    else if (S_ISFIFO(mode)) str[0] = 'p';
    else if (S_ISSOCK(mode)) str[0] = 's';

    if (mode & S_IRUSR) str[1] = 'r';
    if (mode & S_IWUSR) str[2] = 'w';
    if (mode & S_IXUSR) str[3] = 'x';
    if (mode & S_IRGRP) str[4] = 'r';
    if (mode & S_IWGRP) str[5] = 'w';
    if (mode & S_IXGRP) str[6] = 'x';
    if (mode & S_IROTH) str[7] = 'r';
    if (mode & S_IWOTH) str[8] = 'w';
    if (mode & S_IXOTH) str[9] = 'x';

    if (mode & S_ISUID) str[3] = (mode & S_IXUSR) ? 's' : 'S';
    if (mode & S_ISGID) str[6] = (mode & S_IXGRP) ? 's' : 'S';
    if (mode & S_ISVTX) str[9] = (mode & S_IXOTH) ? 't' : 'T';
}

static const char *get_color(mode_t mode, const char *name) {
    if (S_ISDIR(mode)) return COLOR_DIR;
    if (S_ISLNK(mode)) return COLOR_LINK;
    if (S_ISSOCK(mode) || S_ISFIFO(mode)) return COLOR_SOCK;
    if (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) return COLOR_EXEC;

    const char *ext = strrchr(name, '.');
    if (ext) {
        if (strcasecmp(ext, ".tar") == 0 || strcasecmp(ext, ".gz") == 0 ||
            strcasecmp(ext, ".zip") == 0 || strcasecmp(ext, ".7z") == 0 ||
            strcasecmp(ext, ".iso") == 0) return COLOR_ARCHIVE;
    }
    return COLOR_RESET;
}

static int compare_entries(const void *a, const void *b) {
    const FileEntry *fa = (const FileEntry *)a;
    const FileEntry *fb = (const FileEntry *)b;

    int res = 0;
    if (opt_S) {
        if (fb->st.st_size > fa->st.st_size) res = 1;
        else if (fb->st.st_size < fa->st.st_size) res = -1;
    } else if (opt_t) {
        if (fb->st.st_mtim.tv_sec > fa->st.st_mtim.tv_sec) res = 1;
        else if (fb->st.st_mtim.tv_sec < fa->st.st_mtim.tv_sec) res = -1;
    }

    if (res == 0) {
        res = strcasecmp(fa->name, fb->name);
    }

    return opt_r ? -res : res;
}

static void print_entry_long(const FileEntry *e) {
    char mode_str[12];
    mode_to_str(e->st.st_mode, mode_str);

    struct passwd *pw = getpwuid(e->st.st_uid);
    struct group  *gr = getgrgid(e->st.st_gid);
    const char *uname = pw ? pw->pw_name : "user";
    const char *gname = gr ? gr->gr_name : "group";

    char sz_buf[32];
    format_size(e->st.st_size, sz_buf, sizeof(sz_buf));

    char time_str[64];
    struct tm *tm_info = localtime(&e->st.st_mtim.tv_sec);
    strftime(time_str, sizeof(time_str), "%b %d %H:%M", tm_info);

    if (opt_i) {
        printf("%s%9lu%s ", COLOR_INODE, (unsigned long)e->st.st_ino, COLOR_RESET);
    }

    printf("%s %2u %-8s %-8s %6s %s %s%s%s",
           mode_str, (unsigned int)e->st.st_nlink,
           uname, gname, sz_buf, time_str,
           get_color(e->st.st_mode, e->name), e->name, COLOR_RESET);

    if (S_ISLNK(e->st.st_mode) && strlen(e->link_target) > 0) {
        printf(" -> %s%s%s", COLOR_LINK, e->link_target, COLOR_RESET);
    }
    printf("\n");
}

static int list_directory(const char *dir_path) {
    struct stat st;
    if (lstat(dir_path, &st) < 0) {
        fprintf(stderr, "ls: %s: %s\n", dir_path, strerror(errno));
        return -1;
    }

    // Se for um arquivo regular, apenas lista ele
    if (!S_ISDIR(st.st_mode)) {
        FileEntry single;
        strncpy(single.name, dir_path, sizeof(single.name) - 1);
        single.st = st;
        single.link_target[0] = '\0';
        if (S_ISLNK(st.st_mode)) {
            ssize_t len = readlink(dir_path, single.link_target, sizeof(single.link_target) - 1);
            if (len > 0) single.link_target[len] = '\0';
        }

        if (opt_l) print_entry_long(&single);
        else {
            if (opt_i) printf("%s%lu%s ", COLOR_INODE, (unsigned long)single.st.st_ino, COLOR_RESET);
            printf("%s%s%s\n", get_color(single.st.st_mode, single.name), single.name, COLOR_RESET);
        }
        return 0;
    }

    DIR *dir = opendir(dir_path);
    if (!dir) {
        fprintf(stderr, "ls: %s: %s\n", dir_path, strerror(errno));
        return -1;
    }

    FileEntry *entries = malloc(4096 * sizeof(FileEntry));
    if (!entries) {
        closedir(dir);
        return -1;
    }

    size_t count = 0;
    struct dirent *de;

    while ((de = readdir(dir)) != NULL && count < 4096) {
        if (!opt_a && de->d_name[0] == '.') continue;
        if (opt_A && (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)) continue;

        strncpy(entries[count].name, de->d_name, sizeof(entries[count].name) - 1);
        snprintf(entries[count].full_path, sizeof(entries[count].full_path), "%s/%s", dir_path, de->d_name);

        if (lstat(entries[count].full_path, &entries[count].st) < 0) {
            memset(&entries[count].st, 0, sizeof(struct stat));
        }

        entries[count].link_target[0] = '\0';
        if (S_ISLNK(entries[count].st.st_mode)) {
            ssize_t len = readlink(entries[count].full_path, entries[count].link_target, sizeof(entries[count].link_target) - 1);
            if (len > 0) entries[count].link_target[len] = '\0';
        }
        count++;
    }
    closedir(dir);

    qsort(entries, count, sizeof(FileEntry), compare_entries);

    for (size_t i = 0; i < count; i++) {
        if (opt_l) {
            print_entry_long(&entries[i]);
        } else {
            if (opt_i) printf("%s%lu%s ", COLOR_INODE, (unsigned long)entries[i].st.st_ino, COLOR_RESET);
            printf("%s%-16s%s  ", get_color(entries[i].st.st_mode, entries[i].name), entries[i].name, COLOR_RESET);
            if (opt_1 || (i > 0 && (i + 1) % 4 == 0)) printf("\n");
        }
    }
    if (!opt_l && !opt_1 && count % 4 != 0) printf("\n");

    free(entries);
    return 0;
}

int main(int argc, char *argv[]) {
    const char *targets[256];
    int target_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            print_help();
            return 0;
        }

        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            if (strcmp(argv[i], "--all") == 0) opt_a = 1;
            else if (strcmp(argv[i], "--almost-all") == 0) opt_A = 1;
            else if (strcmp(argv[i], "--inode") == 0) opt_i = 1;
            else if (strcmp(argv[i], "--human-readable") == 0) opt_h = 1;
            else if (strcmp(argv[i], "--reverse") == 0) opt_r = 1;
            else {
                for (size_t j = 1; j < strlen(argv[i]); j++) {
                    if (argv[i][j] == 'l') opt_l = 1;
                    else if (argv[i][j] == 'a') opt_a = 1;
                    else if (argv[i][j] == 'A') opt_A = 1;
                    else if (argv[i][j] == 'i') opt_i = 1;
                    else if (argv[i][j] == 'h') opt_h = 1;
                    else if (argv[i][j] == 'S') opt_S = 1;
                    else if (argv[i][j] == 't') opt_t = 1;
                    else if (argv[i][j] == 'r') opt_r = 1;
                    else if (argv[i][j] == '1') opt_1 = 1;
                    else {
                        fprintf(stderr, "ls: opcao desconhecida '-%c'\n", argv[i][j]);
                        return 1;
                    }
                }
            }
        } else {
            if (target_count < 256) targets[target_count++] = argv[i];
        }
    }

    if (target_count == 0) {
        targets[0] = ".";
        target_count = 1;
    }

    int has_errors = 0;
    for (int i = 0; i < target_count; i++) {
        if (target_count > 1) {
            printf("\n%s%s:%s\n", LOW_COLOR_LABEL, targets[i], LOW_COLOR_RESET);
        }
        if (list_directory(targets[i]) < 0) {
            has_errors = 1;
        }
    }

    return has_errors ? 1 : 0;
}
