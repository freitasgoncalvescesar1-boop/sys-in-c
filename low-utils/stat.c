#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET "\033[0m"
#define COLOR_OK    "\033[1;32m"
#define COLOR_ERR   "\033[1;31m"
#define COLOR_FILE  "\033[1;36m"
#define COLOR_TAG   "\033[1;33m"
#define COLOR_ACC   "\033[1;35m"

static void print_help(void) {
    low_print_banner("stat");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./stat <FILE...>\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Display detailed Inode, filesystem allocation, and nanosecond metadata.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-h, --help%s       Display this formatted help guide and exit\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --version%s    Display version and repository information\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./stat /etc/passwd%s             (Inspeciona metadados de arquivo)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./stat /tmp%s                    (Inspeciona metadados de diretorio)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

static const char *get_file_type(mode_t mode) {
    if (S_ISREG(mode)) return "regular file";
    if (S_ISDIR(mode)) return "directory";
    if (S_ISLNK(mode)) return "symbolic link";
    if (S_ISCHR(mode)) return "character special file";
    if (S_ISBLK(mode)) return "block special file";
    if (S_ISFIFO(mode)) return "fifo / named pipe";
    if (S_ISSOCK(mode)) return "socket";
    return "unknown";
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

static void format_time(const struct timespec *ts, char *buf, size_t sz) {
    struct tm *tm_info = localtime(&ts->tv_sec);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    snprintf(buf, sz, "%s.%09ld", time_str, ts->tv_nsec);
}

static int inspect_stat(const char *filepath) {
    struct stat st;
    if (lstat(filepath, &st) < 0) {
        fprintf(stderr, "  %s[ERRO]%s stat '%s': %s\n", COLOR_ERR, COLOR_RESET, filepath, strerror(errno));
        return -1;
    }

    char mode_str[12];
    mode_to_str(st.st_mode, mode_str);

    struct passwd *pw = getpwuid(st.st_uid);
    struct group  *gr = getgrgid(st.st_gid);
    const char *uname = pw ? pw->pw_name : "unknown";
    const char *gname = gr ? gr->gr_name : "unknown";

    char atime_buf[128], mtime_buf[128], ctime_buf[128];
    format_time(&st.st_atim, atime_buf, sizeof(atime_buf));
    format_time(&st.st_mtim, mtime_buf, sizeof(mtime_buf));
    format_time(&st.st_ctim, ctime_buf, sizeof(ctime_buf));

    printf("  %sFile:%s %s%s%s", LOW_COLOR_LABEL, COLOR_RESET, COLOR_FILE, filepath, COLOR_RESET);
    if (S_ISLNK(st.st_mode)) {
        char link_target[1024];
        ssize_t len = readlink(filepath, link_target, sizeof(link_target) - 1);
        if (len > 0) {
            link_target[len] = '\0';
            printf(" %s->%s %s%s%s", COLOR_ACC, COLOR_RESET, COLOR_TAG, link_target, COLOR_RESET);
        }
    }
    printf("\n");

    printf("  %sSize:%s %-12lld %sBlocks:%s %-8lld %sIO Block:%s %-6ld %s%s%s\n",
           LOW_COLOR_LABEL, COLOR_RESET, (long long)st.st_size,
           LOW_COLOR_LABEL, COLOR_RESET, (long long)st.st_blocks,
           LOW_COLOR_LABEL, COLOR_RESET, (long)st.st_blksize,
           COLOR_ACC, get_file_type(st.st_mode), COLOR_RESET);

    printf("  %sDevice:%s %d,%d    %sInode:%s %-11lu %sLinks:%s %-5u\n",
           LOW_COLOR_LABEL, COLOR_RESET, major(st.st_dev), minor(st.st_dev),
           LOW_COLOR_LABEL, COLOR_RESET, (unsigned long)st.st_ino,
           LOW_COLOR_LABEL, COLOR_RESET, (unsigned int)st.st_nlink);

    printf("  %sAccess:%s (%04o/%s)  %sUid:%s (%5u/%8s)  %sGid:%s (%5u/%8s)\n",
           LOW_COLOR_LABEL, COLOR_RESET, (st.st_mode & 07777), mode_str,
           LOW_COLOR_LABEL, COLOR_RESET, st.st_uid, uname,
           LOW_COLOR_LABEL, COLOR_RESET, st.st_gid, gname);

    printf("  %sAccess:%s %s\n", LOW_COLOR_LABEL, COLOR_RESET, atime_buf);
    printf("  %sModify:%s %s\n", LOW_COLOR_LABEL, COLOR_RESET, mtime_buf);
    printf("  %sChange:%s %s\n\n", LOW_COLOR_LABEL, COLOR_RESET, ctime_buf);

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        print_help();
        return (argc < 2) ? 1 : 0;
    }

    int has_errors = 0;
    for (int i = 1; i < argc; i++) {
        if (inspect_stat(argv[i]) < 0) {
            has_errors = 1;
        }
    }

    return has_errors ? 1 : 0;
}
