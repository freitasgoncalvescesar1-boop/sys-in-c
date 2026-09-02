#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <pwd.h>
#include <ctype.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_PID     "\033[1;33m"
#define COLOR_RUN     "\033[1;32m" // Verde (R)
#define COLOR_SLEEP   "\033[0;34m" // Azul (S)
#define COLOR_ZOMBIE  "\033[1;31m" // Vermelho (Z)
#define COLOR_DISK    "\033[1;35m" // Magenta (D)
#define COLOR_USER    "\033[0;36m"
#define COLOR_CMD     "\033[1;37m"

typedef struct {
    pid_t pid;
    pid_t ppid;
    char state;
    char user[32];
    unsigned long long rss_kb;
    unsigned long long vsz_kb;
    int threads;
    int nice_val;
    char cmd[256];
} ProcInfo;

static int opt_all = 0, opt_sort_mem = 0;
static const char *filter_user = NULL;
static pid_t filter_pid = 0;

static void print_help(void) {
    low_print_banner("ps");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./ps [OPTIONS]\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Inspect running processes, memory footprint, states, and threads via /proc.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-a, --all%s            Show processes from all users\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-m, --mem%s            Sort processes by physical memory usage (RSS)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-u <USER>%s            Filter processes by username\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-p <PID>%s             Inspect a specific PID\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s           Display this formatted help guide and exit\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-v, --version%s        Display version and repository information\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./ps%s                           (Lista processos do usuario atual)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./ps -m -a%s                     (Lista todos ordenados por uso de RAM)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./ps -p 1%s                      (Inspeciona o processo init/PID 1)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

static int compare_procs(const void *a, const void *b) {
    const ProcInfo *pa = (const ProcInfo *)a;
    const ProcInfo *pb = (const ProcInfo *)b;
    if (opt_sort_mem) {
        if (pb->rss_kb > pa->rss_kb) return 1;
        if (pb->rss_kb < pa->rss_kb) return -1;
    }
    return (pa->pid > pb->pid) ? 1 : -1;
}

static void parse_proc(pid_t pid, ProcInfo *p) {
    memset(p, 0, sizeof(ProcInfo));
    p->pid = pid;
    strcpy(p->user, "unknown");
    strcpy(p->cmd, "unknown");

    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *fp = fopen(path, "r");
    if (fp) {
        char comm[128] = "";
        long rss_pages = 0;
        unsigned long vsz = 0;
        int ppid = 0, nice_v = 0, threads = 1;
        char state = 'S';

        if (fscanf(fp, "%*d (%127[^)]) %c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*d %*d %d %d %*d %*u %lu %ld",
                   comm, &state, &ppid, &nice_v, &threads, &vsz, &rss_pages) >= 5) {
            p->state = state;
            p->ppid = ppid;
            p->nice_val = nice_v;
            p->threads = threads;
            p->vsz_kb = vsz / 1024;
            p->rss_kb = (rss_pages * sysconf(_SC_PAGESIZE)) / 1024;
            strncpy(p->cmd, comm, sizeof(p->cmd) - 1);
        }
        fclose(fp);
    }

    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    fp = fopen(path, "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "Uid:", 4) == 0) {
                uid_t uid = 0;
                sscanf(line + 4, "%u", &uid);
                struct passwd *pw = getpwuid(uid);
                if (pw) strncpy(p->user, pw->pw_name, sizeof(p->user) - 1);
                else snprintf(p->user, sizeof(p->user), "%u", (unsigned int)uid);
                break;
            }
        }
        fclose(fp);
    }

    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    fp = fopen(path, "r");
    if (fp) {
        char cmdline[256];
        size_t n = fread(cmdline, 1, sizeof(cmdline) - 1, fp);
        if (n > 0) {
            for (size_t i = 0; i < n; i++) if (cmdline[i] == '\0') cmdline[i] = ' ';
            cmdline[n] = '\0';
            strncpy(p->cmd, cmdline, sizeof(p->cmd) - 1);
        }
        fclose(fp);
    }
}

static const char *get_state_color(char s) {
    if (s == 'R') return COLOR_RUN;
    if (s == 'Z') return COLOR_ZOMBIE;
    if (s == 'D') return COLOR_DISK;
    return COLOR_SLEEP;
}

int main(int argc, char *argv[]) {
    uid_t my_uid = geteuid();
    struct passwd *mypw = getpwuid(my_uid);
    const char *my_name = mypw ? mypw->pw_name : "unknown";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            print_help();
            return 0;
        }

        if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0) opt_all = 1;
        else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--mem") == 0) opt_sort_mem = 1;
        else if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) filter_user = argv[++i];
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) filter_pid = atoi(argv[++i]);
    }

    DIR *dir = opendir("/proc");
    if (!dir) {
        fprintf(stderr, "ps: nao foi possivel abrir /proc: %s\n", strerror(errno));
        return 1;
    }

    ProcInfo *procs = malloc(4096 * sizeof(ProcInfo));
    size_t count = 0;
    struct dirent *de;

    while ((de = readdir(dir)) != NULL && count < 4096) {
        if (!isdigit(de->d_name[0])) continue;
        pid_t pid = atoi(de->d_name);

        if (filter_pid > 0 && pid != filter_pid) continue;

        ProcInfo p;
        parse_proc(pid, &p);

        if (!opt_all && !filter_user && filter_pid == 0) {
            if (strcmp(p.user, my_name) != 0) continue;
        }

        if (filter_user && strcmp(p.user, filter_user) != 0) continue;

        procs[count++] = p;
    }
    closedir(dir);

    qsort(procs, count, sizeof(ProcInfo), compare_procs);

    printf("\n%s  %-6s %-12s %-5s %-4s %-8s %-9s %s%s\n",
           LOW_COLOR_LABEL, "PID", "USER", "STAT", "THRD", "RSS(MB)", "VSZ(MB)", "COMMAND", COLOR_RESET);
    printf("  ----------------------------------------------------------------------\n");

    for (size_t i = 0; i < count; i++) {
        printf("  %s%-6d%s %s%-12.12s%s %s[%c]%s   %-4d %6.1fM %7.1fM %s%-40.40s%s\n",
               COLOR_PID, procs[i].pid, COLOR_RESET,
               COLOR_USER, procs[i].user, COLOR_RESET,
               get_state_color(procs[i].state), procs[i].state, COLOR_RESET,
               procs[i].threads,
               (double)procs[i].rss_kb / 1024.0,
               (double)procs[i].vsz_kb / 1024.0,
               COLOR_CMD, procs[i].cmd, COLOR_RESET);
    }
    printf("\n  Total de processos listados: %zu\n\n", count);

    free(procs);
    return 0;
}
