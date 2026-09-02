#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_WARN    "\033[1;33m"
#define COLOR_FILE    "\033[1;36m"
#define COLOR_LINK    "\033[1;33m"
#define COLOR_ARROW   "\033[1;35m"

static void print_help(void) {
    low_print_banner("ln");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./ln [OPTIONS] <TARGET> <LINK_NAME>\n");
    printf("  ./ln [OPTIONS] <TARGET...> <DIRECTORY>\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Create links between files (Hard Links via Inodes or Symbolic Soft Links).\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-s, --symbolic%s   Create a symbolic (soft) link instead of a hard link\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-r, --relative%s   Create symbolic links relative to link location\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-f, --force%s      Remove existing destination files before linking\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s       Display this formatted help guide and exit\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --version%s    Display version and repository information\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./ln arquivo.txt link_hard%s           (Hard Link no mesmo Inode)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./ln -s /etc/hosts meu_hosts%s         (Symlink tradicional)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./ln -s -r /tmp/a.txt ./sub/link%s      (Calcula caminho relativo)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

static int create_link(const char *target, const char *dest_input, int is_symlink, int is_force) {
    char final_dest[1024];
    struct stat st_dest;

    if (stat(dest_input, &st_dest) == 0 && S_ISDIR(st_dest.st_mode)) {
        const char *base = strrchr(target, '/');
        base = (base) ? base + 1 : target;
        snprintf(final_dest, sizeof(final_dest), "%s/%s", dest_input, base);
    } else {
        strncpy(final_dest, dest_input, sizeof(final_dest) - 1);
        final_dest[sizeof(final_dest) - 1] = '\0';
    }

    if (is_force) {
        unlink(final_dest);
    }

    if (is_symlink) {
        if (access(target, F_OK) != 0) {
            printf("  %s[AVISO]%s O alvo '%s' nao existe no momento (criando broken symlink)\n", COLOR_WARN, COLOR_RESET, target);
        }

        if (symlink(target, final_dest) < 0) {
            fprintf(stderr, "  %s[ERRO]%s %s -> %s: %s\n", COLOR_ERR, COLOR_RESET, target, final_dest, strerror(errno));
            return -1;
        }

        printf("  %s[OK: SYMLINK]%s   %s%s%s %s->%s %s%s%s\n",
               COLOR_OK, COLOR_RESET,
               COLOR_FILE, final_dest, COLOR_RESET,
               COLOR_ARROW, COLOR_RESET,
               COLOR_LINK, target, COLOR_RESET);
    } else {
        if (link(target, final_dest) < 0) {
            fprintf(stderr, "  %s[ERRO]%s %s == %s: %s\n", COLOR_ERR, COLOR_RESET, target, final_dest, strerror(errno));
            return -1;
        }

        struct stat st_orig;
        ino_t inode_num = (stat(target, &st_orig) == 0) ? st_orig.st_ino : 0;

        printf("  %s[OK: HARDLINK]%s  %s%s%s %s==%s %s%s%s (Inode: %lu)\n",
               COLOR_OK, COLOR_RESET,
               COLOR_FILE, final_dest, COLOR_RESET,
               COLOR_ARROW, COLOR_RESET,
               COLOR_FILE, target, COLOR_RESET,
               (unsigned long)inode_num);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    int is_symlink = 0, is_force = 0;
    const char *targets[256];
    int target_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            print_help();
            return 0;
        }

        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--symbolic") == 0) is_symlink = 1;
        else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) is_force = 1;
        else {
            if (argv[i][0] == '-' && argv[i][1] != '\0') {
                for (size_t j = 1; j < strlen(argv[i]); j++) {
                    if (argv[i][j] == 's') is_symlink = 1;
                    else if (argv[i][j] == 'f') is_force = 1;
                }
            } else {
                if (target_count < 256) targets[target_count++] = argv[i];
            }
        }
    }

    if (target_count < 2) {
        print_help();
        return 1;
    }

    if (target_count == 2) {
        return (create_link(targets[0], targets[1], is_symlink, is_force) < 0) ? 1 : 0;
    }

    const char *dest_dir = targets[target_count - 1];
    struct stat st_dir;
    if (stat(dest_dir, &st_dir) < 0 || !S_ISDIR(st_dir.st_mode)) {
        fprintf(stderr, "ln: o destino '%s' nao e um diretorio valido para multiplos links\n", dest_dir);
        return 1;
    }

    int has_errors = 0;
    for (int i = 0; i < target_count - 1; i++) {
        if (create_link(targets[i], dest_dir, is_symlink, is_force) < 0) {
            has_errors = 1;
        }
    }

    return has_errors ? 1 : 0;
}
