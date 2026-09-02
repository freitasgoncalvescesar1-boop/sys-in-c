#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_DIR     "\033[1;34m" // Azul
#define COLOR_EXEC    "\033[1;32m" // Verde
#define COLOR_LINK    "\033[1;36m" // Ciano
#define COLOR_ARCHIVE "\033[1;31m" // Vermelho
#define COLOR_CODE    "\033[1;33m" // Amarelo
#define COLOR_BRANCH  "\033[0;90m" // Cinza escuro para os ramos
#define COLOR_PERM    "\033[0;33m"
#define COLOR_SIZE    "\033[0;32m"
#define COLOR_MUTED   "\033[0;90m"

#define MAX_DEPTH 64
#define MAX_ENTRIES 4096

typedef struct {
    char name[256];
    char full_path[2048];
    struct stat st;
    char link_target[1024];
    int is_dir;
} TreeEntry;

static int opt_a = 0;             // Todos (inclui ocultos)
static int opt_d = 0;             // Apenas diretórios
static int opt_L = 0;             // Profundidade máxima (0 = infinita)
static int opt_h = 0;             // Tamanho human-readable
static int opt_p = 0;             // Permissões
static int opt_u = 0;             // Dono
static int opt_g = 0;             // Grupo
static int opt_dirs_first = 0;    // Pastas primeiro

static unsigned long total_dirs = 0;
static unsigned long total_files = 0;
static unsigned long long total_size_bytes = 0;

static int is_last_stack[MAX_DEPTH] = {0};

static void print_help(void) {
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ tree - Visual Directory Tree & Metadata Branch Drawer ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  tree [OPTIONS] [DIRECTORY...]\n\n");
    printf("Options:\n");
    printf("  -L <LEVEL>          Max display depth of the directory tree\n");
    printf("  -a                  All files (do not ignore entries starting with .)\n");
    printf("  -d                  List directories only\n");
    printf("  -h, --human         Print size in human readable format (e.g. [ 1.4K])\n");
    printf("  -p                  Print file permissions [drwxr-xr-x]\n");
    printf("  -u                  Print owner username\n");
    printf("  -g                  Print group name\n");
    printf("  --dirs-first        List directories before files\n");
    printf("  --help              Display this formatted help guide\n\n");
    printf("Exemplos:\n");
    printf("  tree\n");
    printf("  tree -L 2\n");
    printf("  tree -h -p --dirs-first\n");
    printf("  tree low-utils/ -L 1\n");
    printf("%s========================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

static void format_size_human(off_t size, char *buf, size_t sz) {
    const char *units[] = {"B", "K", "M", "G", "T"};
    int i = 0;
    double d = (double)size;
    while (d >= 1024.0 && i < 4) {
        d /= 1024.0;
        i++;
    }
    if (i == 0) snprintf(buf, sz, "%4lldB", (long long)size);
    else snprintf(buf, sz, "%4.1f%s", d, units[i]);
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
}

static const char *get_entry_color(mode_t mode, const char *name) {
    if (S_ISDIR(mode)) return COLOR_DIR;
    if (S_ISLNK(mode)) return COLOR_LINK;
    if (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) return COLOR_EXEC;

    const char *ext = strrchr(name, '.');
    if (ext) {
        if (strcasecmp(ext, ".tar") == 0 || strcasecmp(ext, ".gz") == 0 ||
            strcasecmp(ext, ".zip") == 0 || strcasecmp(ext, ".7z") == 0 ||
            strcasecmp(ext, ".iso") == 0 || strcasecmp(ext, ".apk") == 0) return COLOR_ARCHIVE;

        if (strcasecmp(ext, ".c") == 0 || strcasecmp(ext, ".h") == 0 ||
            strcasecmp(ext, ".cpp") == 0 || strcasecmp(ext, ".py") == 0 ||
            strcasecmp(ext, ".js") == 0 || strcasecmp(ext, ".html") == 0 ||
            strcasecmp(ext, ".sh") == 0) return COLOR_CODE;
    }
    return COLOR_RESET;
}

static int compare_tree_entries(const void *a, const void *b) {
    const TreeEntry *ea = (const TreeEntry *)a;
    const TreeEntry *eb = (const TreeEntry *)b;

    if (opt_dirs_first) {
        if (ea->is_dir && !eb->is_dir) return -1;
        if (!ea->is_dir && eb->is_dir) return 1;
    }
    return strcasecmp(ea->name, eb->name);
}

static void render_tree_node(const char *dir_path, int depth) {
    if (opt_L > 0 && depth >= opt_L) return;
    if (depth >= MAX_DEPTH - 1) return;

    DIR *dir = opendir(dir_path);
    if (!dir) {
        return;
    }

    TreeEntry *entries = malloc(MAX_ENTRIES * sizeof(TreeEntry));
    if (!entries) { closedir(dir); return; }

    size_t count = 0;
    struct dirent *de;

    while ((de = readdir(dir)) != NULL && count < MAX_ENTRIES) {
        if (!opt_a && de->d_name[0] == '.') continue;
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        strncpy(entries[count].name, de->d_name, sizeof(entries[count].name) - 1);
        snprintf(entries[count].full_path, sizeof(entries[count].full_path), "%s/%s", dir_path, de->d_name);

        if (lstat(entries[count].full_path, &entries[count].st) < 0) {
            memset(&entries[count].st, 0, sizeof(struct stat));
        }

        entries[count].is_dir = S_ISDIR(entries[count].st.st_mode);
        if (opt_d && !entries[count].is_dir) continue;

        entries[count].link_target[0] = '\0';
        if (S_ISLNK(entries[count].st.st_mode)) {
            ssize_t len = readlink(entries[count].full_path, entries[count].link_target, sizeof(entries[count].link_target) - 1);
            if (len > 0) entries[count].link_target[len] = '\0';
        }

        count++;
    }
    closedir(dir);

    qsort(entries, count, sizeof(TreeEntry), compare_tree_entries);

    for (size_t i = 0; i < count; i++) {
        int is_last = (i == count - 1);
        is_last_stack[depth] = is_last;

        if (entries[i].is_dir) total_dirs++;
        else {
            total_files++;
            total_size_bytes += entries[i].st.st_size;
        }

        // 1. Desenha os Ramos de Conexão (Branches)
        printf("%s", COLOR_BRANCH);
        for (int d = 0; d < depth; d++) {
            printf("%s", is_last_stack[d] ? "    " : "│   ");
        }
        printf("%s%s", is_last ? "└── " : "├── ", COLOR_RESET);

        // 2. Metadados Opcionais (Permissões, Dono, Grupo, Tamanho)
        if (opt_p) {
            char p_buf[12];
            mode_to_str(entries[i].st.st_mode, p_buf);
            printf("%s[%s]%s ", COLOR_PERM, p_buf, COLOR_RESET);
        }

        if (opt_u || opt_g) {
            struct passwd *pw = opt_u ? getpwuid(entries[i].st.st_uid) : NULL;
            struct group  *gr = opt_g ? getgrgid(entries[i].st.st_gid) : NULL;
            printf("%s[", COLOR_MUTED);
            if (opt_u) printf("%s", pw ? pw->pw_name : "user");
            if (opt_u && opt_g) printf(" ");
            if (opt_g) printf("%s", gr ? gr->gr_name : "group");
            printf("]%s ", COLOR_RESET);
        }

        if (opt_h) {
            char sz_str[16];
            format_size_human(entries[i].st.st_size, sz_str, sizeof(sz_str));
            printf("%s[%s]%s ", COLOR_SIZE, sz_str, COLOR_RESET);
        }

        // 3. Nome do Arquivo com Cor e Alvo de Symlink
        const char *col = get_entry_color(entries[i].st.st_mode, entries[i].name);
        printf("%s%s%s", col, entries[i].name, COLOR_RESET);

        if (S_ISLNK(entries[i].st.st_mode) && strlen(entries[i].link_target) > 0) {
            printf(" %s->%s %s%s%s", COLOR_BRANCH, COLOR_RESET, COLOR_LINK, entries[i].link_target, COLOR_RESET);
        }
        printf("\n");

        // 4. Chamada Recursiva para Subdiretórios
        if (entries[i].is_dir && !S_ISLNK(entries[i].st.st_mode)) {
            render_tree_node(entries[i].full_path, depth + 1);
        }
    }

    free(entries);
}

int main(int argc, char *argv[]) {
    utilipc_init();

    const char *start_dirs[256];
    int dir_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-help") == 0) {
            print_help();
            utilipc_close();
            return 0;
        }

        if (strcmp(argv[i], "-L") == 0 && i + 1 < argc) {
            opt_L = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-a") == 0) {
            opt_a = 1;
        } else if (strcmp(argv[i], "-d") == 0) {
            opt_d = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--human") == 0) {
            opt_h = 1;
        } else if (strcmp(argv[i], "-p") == 0) {
            opt_p = 1;
        } else if (strcmp(argv[i], "-u") == 0) {
            opt_u = 1;
        } else if (strcmp(argv[i], "-g") == 0) {
            opt_g = 1;
        } else if (strcmp(argv[i], "--dirs-first") == 0) {
            opt_dirs_first = 1;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (size_t j = 1; j < strlen(argv[i]); j++) {
                if (argv[i][j] == 'a') opt_a = 1;
                else if (argv[i][j] == 'd') opt_d = 1;
                else if (argv[i][j] == 'h') opt_h = 1;
                else if (argv[i][j] == 'p') opt_p = 1;
                else if (argv[i][j] == 'u') opt_u = 1;
                else if (argv[i][j] == 'g') opt_g = 1;
            }
        } else {
            if (dir_count < 256) start_dirs[dir_count++] = argv[i];
        }
    }

    if (dir_count == 0) {
        start_dirs[0] = ".";
        dir_count = 1;
    }

    for (int i = 0; i < dir_count; i++) {
        total_dirs = 0;
        total_files = 0;
        total_size_bytes = 0;

        printf("%s%s%s\n", COLOR_DIR, start_dirs[i], COLOR_RESET);
        render_tree_node(start_dirs[i], 0);

        char sz_total[32];
        format_size_human((off_t)total_size_bytes, sz_total, sizeof(sz_total));

        printf("\n  %s%lu directories, %lu files%s %s(Total: %s)%s\n\n",
               COLOR_TITLE, total_dirs, total_files, COLOR_RESET,
               COLOR_MUTED, sz_total, COLOR_RESET);
    }

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "tree: listed %lu dirs, %lu files", total_dirs, total_files);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
