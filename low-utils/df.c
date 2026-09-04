#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <mntent.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET "\033[0m"
#define COLOR_OK    "\033[1;32m"
#define COLOR_WARN  "\033[1;33m"
#define COLOR_ERR   "\033[1;31m"
#define COLOR_FS    "\033[1;36m"
#define COLOR_MNT   "\033[1;34m"

static int opt_h = 1, opt_T = 0, opt_i = 0, opt_a = 0;

static void print_help(void) {
    low_print_banner("df");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./df [OPTIONS] [MOUNT/FILE...]\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Report file system disk space usage with visual meters and combined flags.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-h, --human-readable%s Print sizes in human readable format (1K 234M 2G) [Default]\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-T, --print-type%s     Print file system type (ext4, f2fs, vfat, etc.)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-i, --inodes%s         List inode information instead of block usage\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-a, --all%s            Include pseudo and duplicate file systems\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-k%s                   Print sizes in 1K blocks\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s           Display this help guide and exit\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sCOMBINED SHORT FLAGS EXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./df -Th%s                            (Print Type + Human-readable)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./df -iah%s                           (Inodes + All + Human-readable)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

static void format_size(unsigned long long bytes, char *buf, size_t sz) {
    if (!opt_h) {
        snprintf(buf, sz, "%llu", bytes / 1024);
        return;
    }
    const char *units[] = {"B", "K", "M", "G", "T", "P"};
    int i = 0;
    double d_size = (double)bytes;
    while (d_size >= 1024.0 && i < 5) {
        d_size /= 1024.0;
        i++;
    }
    if (i == 0) snprintf(buf, sz, "%lluB", bytes);
    else snprintf(buf, sz, "%.1f%s", d_size, units[i]);
}

static int is_pseudo_fs(const char *type, const char *fsname) {
    if (strcmp(type, "proc") == 0 || strcmp(type, "sysfs") == 0 ||
        strcmp(type, "devpts") == 0 || strcmp(type, "cgroup") == 0 ||
        strcmp(type, "cgroup2") == 0 || strcmp(type, "pstore") == 0 ||
        strcmp(type, "bpf") == 0 || strcmp(type, "selinuxfs") == 0 ||
        strcmp(type, "tracefs") == 0 || strcmp(type, "debugfs") == 0 ||
        strcmp(type, "securityfs") == 0 || strcmp(type, "configfs") == 0 ||
        strcmp(type, "fusectl") == 0 || strcmp(type, "mqueue") == 0 ||
        strcmp(type, "hugetlbfs") == 0 || strcmp(type, "devtmpfs") == 0 ||
        strcmp(type, "autofs") == 0 || strncmp(fsname, "none", 4) == 0) {
        return 1;
    }
    return 0;
}

static void print_meter_bar(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int filled = pct / 10;
    const char *col = (pct >= 90) ? COLOR_ERR : (pct >= 75) ? COLOR_WARN : COLOR_OK;

    printf("%s[", col);
    for (int i = 0; i < 10; i++) {
        if (i < filled) printf("■");
        else printf("░");
    }
    printf("] %3d%%%s", pct, COLOR_RESET);
}

static void display_mount(const char *fsname, const char *dir, const char *type) {
    struct statvfs vfs;
    if (statvfs(dir, &vfs) < 0) return;
    if (vfs.f_blocks == 0 && !opt_a) return;

    if (opt_i) {
        unsigned long long total_inodes = vfs.f_files;
        unsigned long long free_inodes = vfs.f_ffree;
        unsigned long long used_inodes = (total_inodes >= free_inodes) ? (total_inodes - free_inodes) : 0;
        int pct_i = (total_inodes > 0) ? (int)((used_inodes * 100) / total_inodes) : 0;

        char s_tot[32], s_use[32], s_fre[32];
        snprintf(s_tot, sizeof(s_tot), "%llu", total_inodes);
        snprintf(s_use, sizeof(s_use), "%llu", used_inodes);
        snprintf(s_fre, sizeof(s_fre), "%llu", free_inodes);

        printf("  %s%-20s%s ", COLOR_FS, fsname, COLOR_RESET);
        if (opt_T) printf("%-8s ", type);
        printf("%9s %9s %9s ", s_tot, s_use, s_fre);
        print_meter_bar(pct_i);
        printf("  %s%s%s\n", COLOR_MNT, dir, COLOR_RESET);
    } else {
        unsigned long long block_size = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
        unsigned long long total_bytes = vfs.f_blocks * block_size;
        unsigned long long free_bytes  = vfs.f_bfree * block_size;
        unsigned long long avail_bytes = vfs.f_bavail * block_size;
        unsigned long long used_bytes  = (total_bytes >= free_bytes) ? (total_bytes - free_bytes) : 0;

        int pct = 0;
        if (used_bytes + avail_bytes > 0) {
            pct = (int)((used_bytes * 100) / (used_bytes + avail_bytes));
        }

        char s_tot[32], s_use[32], s_fre[32];
        format_size(total_bytes, s_tot, sizeof(s_tot));
        format_size(used_bytes, s_use, sizeof(s_use));
        format_size(avail_bytes, s_fre, sizeof(s_fre));

        printf("  %s%-20s%s ", COLOR_FS, fsname, COLOR_RESET);
        if (opt_T) printf("%-8s ", type);
        printf("%7s %7s %7s ", s_tot, s_use, s_fre);
        print_meter_bar(pct);
        printf("  %s%s%s\n", COLOR_MNT, dir, COLOR_RESET);
    }
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(argv[i], "--print-type") == 0) { opt_T = 1; continue; }
        if (strcmp(argv[i], "--inodes") == 0) { opt_i = 1; continue; }
        if (strcmp(argv[i], "--all") == 0) { opt_a = 1; continue; }
        if (strcmp(argv[i], "--human-readable") == 0) { opt_h = 1; continue; }

        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            size_t flen = strlen(argv[i]);
            for (size_t j = 1; j < flen; j++) {
                char opt = argv[i][j];
                if (opt == 'T') opt_T = 1;
                else if (opt == 'i') opt_i = 1;
                else if (opt == 'a') opt_a = 1;
                else if (opt == 'h') opt_h = 1;
                else if (opt == 'k') opt_h = 0;
                else if (opt == 'H') { print_help(); return 0; }
            }
        }
    }

    FILE *fp = setmntent("/proc/mounts", "r");
    if (!fp) fp = setmntent("/etc/mtab", "r");
    if (!fp) {
        fprintf(stderr, "df: nao foi possivel ler os pontos de montagem: %s\n", strerror(errno));
        return 1;
    }

    printf("\n%s  %-20s ", LOW_COLOR_LABEL, "Filesystem");
    if (opt_T) printf("%-8s ", "Type");
    if (opt_i) {
        printf("%9s %9s %9s %-16s  %s%s\n", "Inodes", "IUsed", "IFree", "IUse%", "Mounted on", COLOR_RESET);
    } else {
        printf("%7s %7s %7s %-16s  %s%s\n", "Size", "Used", "Avail", "Use%", "Mounted on", COLOR_RESET);
    }
    printf("  ---------------------------------------------------------------------------------\n");

    struct mntent *mnt;
    while ((mnt = getmntent(fp)) != NULL) {
        if (!opt_a && is_pseudo_fs(mnt->mnt_type, mnt->mnt_fsname)) continue;
        display_mount(mnt->mnt_fsname, mnt->mnt_dir, mnt->mnt_type);
    }

    endmntent(fp);
    printf("\n");
    return 0;
}
