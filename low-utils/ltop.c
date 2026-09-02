#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/resource.h>
#include <dirent.h>
#include <sys/types.h>
#include <pwd.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include "low.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_HEADER  "\033[1;37;45m"
#define COLOR_FOOTER  "\033[1;37;44m"
#define COLOR_SEL     "\033[1;30;46m" // Linha Selecionada
#define COLOR_PID     "\033[1;33m"
#define COLOR_USER    "\033[0;36m"
#define COLOR_RUN     "\033[1;32m"
#define COLOR_SLEEP   "\033[0;34m"
#define COLOR_ZOMBIE  "\033[1;31m"
#define COLOR_DISK    "\033[1;35m"
#define COLOR_MUTED   "\033[0;90m"
#define COLOR_BAR_CPU "\033[1;32m"
#define COLOR_BAR_RAM "\033[1;36m"
#define COLOR_BAR_SWP "\033[1;33m"

typedef struct {
    pid_t pid;
    pid_t ppid;
    char state;
    char user[32];
    unsigned long long rss_kb;
    unsigned long long vsz_kb;
    int threads;
    int nice_val;
    double cpu_pct;
    unsigned long long utime_stime;
    char cmd[256];
} ProcEntry;

static struct termios orig_termios;
static int sort_mode = 0; // 0 = Memória (RSS), 1 = CPU%, 2 = PID
static int sort_reverse = 0;
static int selected_idx = 0;
static int scroll_offset = 0;
static char status_msg[128] = "";
static char filter_query[64] = "";

// Variáveis para cálculo de CPU Global
static unsigned long long prev_cpu_user = 0, prev_cpu_nice = 0, prev_cpu_sys = 0, prev_cpu_idle = 0;
static unsigned long long prev_cpu_iowait = 0, prev_cpu_irq = 0, prev_cpu_softirq = 0, prev_cpu_steal = 0;
static double global_cpu_load = 0.0;

static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    printf("\033[?1049l\033[?25h\033[0m");
    fflush(stdout);
}

static void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    printf("\033[?1049h\033[?25l\033[H");
    fflush(stdout);
}

static void sig_handler(int sig) {
    (void)sig;
    disable_raw_mode();
    exit(0);
}

static void get_window_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
}

static void update_global_cpu(void) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return;

    char line[256];
    if (fgets(line, sizeof(line), fp)) {
        unsigned long long u, n, s, i, io, ir, so, st;
        if (sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &u, &n, &s, &i, &io, &ir, &so, &st) >= 4) {
            unsigned long long cur_idle = i + io;
            unsigned long long cur_total = u + n + s + i + io + ir + so + st;

            unsigned long long prev_idle = prev_cpu_idle + prev_cpu_iowait;
            unsigned long long prev_total = prev_cpu_user + prev_cpu_nice + prev_cpu_sys + prev_cpu_idle +
                                            prev_cpu_iowait + prev_cpu_irq + prev_cpu_softirq + prev_cpu_steal;

            unsigned long long total_delta = cur_total - prev_total;
            unsigned long long idle_delta = cur_idle - prev_idle;

            prev_cpu_user = u; prev_cpu_nice = n; prev_cpu_sys = s; prev_cpu_idle = i;
            prev_cpu_iowait = io; prev_cpu_irq = ir; prev_cpu_softirq = so; prev_cpu_steal = st;

            if (total_delta > 0) {
                global_cpu_load = (1.0 - ((double)idle_delta / (double)total_delta)) * 100.0;
                if (global_cpu_load < 0.0) global_cpu_load = 0.0;
                if (global_cpu_load > 100.0) global_cpu_load = 100.0;
            }
        }
    }
    fclose(fp);
}

static void get_global_mem(unsigned long long *ram_used, unsigned long long *ram_total,
                           unsigned long long *swp_used, unsigned long long *swp_total) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return;

    long t_ram = 0, f_ram = 0, a_ram = 0, t_swp = 0, f_swp = 0;
    char label[64];
    long val;

    while (fscanf(fp, "%63s %ld kB", label, &val) == 2) {
        if (strcmp(label, "MemTotal:") == 0) t_ram = val;
        else if (strcmp(label, "MemFree:") == 0) f_ram = val;
        else if (strcmp(label, "MemAvailable:") == 0) a_ram = val;
        else if (strcmp(label, "SwapTotal:") == 0) t_swp = val;
        else if (strcmp(label, "SwapFree:") == 0) f_swp = val;
    }
    fclose(fp);

    if (a_ram == 0) a_ram = f_ram;
    long u_ram = (t_ram > a_ram) ? (t_ram - a_ram) : 0;
    long u_swp = (t_swp > f_swp) ? (t_swp - f_swp) : 0;

    *ram_total = (unsigned long long)t_ram;
    *ram_used  = (unsigned long long)u_ram;
    *swp_total = (unsigned long long)t_swp;
    *swp_used  = (unsigned long long)u_swp;
}

static void draw_bar(const char *label, double cur, double max, const char *color, int bar_len, const char *unit_str) {
    int pct = (max > 0.0) ? (int)((cur * 100.0) / max) : 0;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int filled = (pct * bar_len) / 100;

    printf("  %s%s%s %s[", LOW_COLOR_LABEL, label, COLOR_RESET, color);
    for (int i = 0; i < bar_len; i++) {
        if (i < filled) printf("|");
        else printf("%s░%s", COLOR_MUTED, color);
    }
    printf("]%s %5.1f%% %s\033[K\r\n", color, (max == 100.0) ? cur : (double)pct, unit_str);
}

static int compare_procs(const void *a, const void *b) {
    const ProcEntry *pa = (const ProcEntry *)a;
    const ProcEntry *pb = (const ProcEntry *)b;
    int res = 0;

    if (sort_mode == 0) { // Memória
        if (pb->rss_kb > pa->rss_kb) res = 1;
        else if (pb->rss_kb < pa->rss_kb) res = -1;
    } else if (sort_mode == 1) { // CPU
        if (pb->cpu_pct > pa->cpu_pct) res = 1;
        else if (pb->cpu_pct < pa->cpu_pct) res = -1;
    } else { // PID
        res = (pa->pid > pb->pid) ? 1 : -1;
    }

    return sort_reverse ? -res : res;
}

// Extrai valor de chave do /proc/[pid]/status
static void get_proc_status_str(pid_t pid, const char *key, char *out, size_t out_sz) {
    strncpy(out, "N/A", out_sz - 1);
    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *fp = fopen(path, "r");
    if (!fp) return;

    char line[256];
    size_t klen = strlen(key);
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, key, klen) == 0 && line[klen] == ':') {
            char *v = line + klen + 1;
            while (*v == ' ' || *v == '\t') v++;
            size_t vlen = strlen(v);
            if (vlen > 0 && (v[vlen - 1] == '\n' || v[vlen - 1] == '\r')) v[--vlen] = '\0';
            snprintf(out, out_sz, "%s", v);
            break;
        }
    }
    fclose(fp);
}

// --- MODAL DE RAIO-X AVANÇADO DO PROCESSO (ENTER) ---
static void show_process_xray_modal(pid_t pid) {
    int term_rows, term_cols;
    get_window_size(&term_rows, &term_cols);

    char exe_path[512] = "N/A";
    char cwd_path[512] = "N/A";
    char p_path[128];

    snprintf(p_path, sizeof(p_path), "/proc/%d/exe", pid);
    ssize_t len = readlink(p_path, exe_path, sizeof(exe_path) - 1);
    if (len > 0) exe_path[len] = '\0';

    snprintf(p_path, sizeof(p_path), "/proc/%d/cwd", pid);
    len = readlink(p_path, cwd_path, sizeof(cwd_path) - 1);
    if (len > 0) cwd_path[len] = '\0';

    char cmdline[512] = "";
    snprintf(p_path, sizeof(p_path), "/proc/%d/cmdline", pid);
    FILE *fp = fopen(p_path, "r");
    if (fp) {
        size_t n = fread(cmdline, 1, sizeof(cmdline) - 1, fp);
        if (n > 0) {
            for (size_t i = 0; i < n; i++) if (cmdline[i] == '\0') cmdline[i] = ' ';
            cmdline[n] = '\0';
        }
        fclose(fp);
    }
    if (strlen(cmdline) == 0) strcpy(cmdline, "(Processo sem cmdline visivel)");

    // Pega informações do Status
    char proc_name[64], proc_state[64], ppid_str[32], threads_str[32];
    char vm_size[32], vm_rss[32], vm_data[32], vm_stk[32], vm_exe[32];
    char vctx[32], nvctx[32], uid_str[64];

    get_proc_status_str(pid, "Name", proc_name, sizeof(proc_name));
    get_proc_status_str(pid, "State", proc_state, sizeof(proc_state));
    get_proc_status_str(pid, "PPid", ppid_str, sizeof(ppid_str));
    get_proc_status_str(pid, "Threads", threads_str, sizeof(threads_str));
    get_proc_status_str(pid, "VmSize", vm_size, sizeof(vm_size));
    get_proc_status_str(pid, "VmRSS", vm_rss, sizeof(vm_rss));
    get_proc_status_str(pid, "VmData", vm_data, sizeof(vm_data));
    get_proc_status_str(pid, "VmStk", vm_stk, sizeof(vm_stk));
    get_proc_status_str(pid, "VmExe", vm_exe, sizeof(vm_exe));
    get_proc_status_str(pid, "voluntary_ctxt_switches", vctx, sizeof(vctx));
    get_proc_status_str(pid, "nonvoluntary_ctxt_switches", nvctx, sizeof(nvctx));
    get_proc_status_str(pid, "Uid", uid_str, sizeof(uid_str));

    // Descobre nome do processo pai
    pid_t ppid = atoi(ppid_str);
    char ppid_name[64] = "unknown";
    if (ppid > 0) get_proc_status_str(ppid, "Name", ppid_name, sizeof(ppid_name));

    // Conta descritores de arquivos abertos (FDs)
    int fd_count = 0;
    snprintf(p_path, sizeof(p_path), "/proc/%d/fd", pid);
    DIR *fddir = opendir(p_path);
    if (fddir) {
        struct dirent *de;
        while ((de = readdir(fddir)) != NULL) {
            if (de->d_name[0] != '.') fd_count++;
        }
        closedir(fddir);
    }

    // Conta variáveis de ambiente
    int env_count = 0;
    snprintf(p_path, sizeof(p_path), "/proc/%d/environ", pid);
    FILE *efp = fopen(p_path, "r");
    if (efp) {
        char ebuf[4096];
        size_t en = fread(ebuf, 1, sizeof(ebuf), efp);
        for (size_t i = 0; i < en; i++) if (ebuf[i] == '\0') env_count++;
        fclose(efp);
    }

    int modal_w = 74;
    int modal_h = 16;
    if (modal_w > term_cols - 2) modal_w = term_cols - 2;
    int start_x = (term_cols - modal_w) / 2;
    int start_y = (term_rows - modal_h) / 2;
    if (start_y < 1) start_y = 1;

    printf("\033[?25l");

    printf("\033[%d;%dH\033[1;35m╭", start_y, start_x);
    for (int i = 0; i < modal_w - 2; i++) printf("─");
    printf("╮\033[0m\r\n");

    printf("\033[%d;%dH\033[1;35m│\033[0m\033[1;37;45m  🔍 RAIO-X AVANÇADO DO PROCESSO — PID: %-5d (%-15.15s)%-*s\033[0m\033[1;35m│\033[0m\r\n",
           start_y + 1, start_x, pid, proc_name, (int)(modal_w - 47), "");

    printf("\033[%d;%dH\033[1;35m├", start_y + 2, start_x);
    for (int i = 0; i < modal_w - 2; i++) printf("─");
    printf("┤\033[0m\r\n");

    printf("\033[%d;%dH\033[1;35m│\033[0m  \033[1;36m• Executável   :\033[0m %-52.52s \033[1;35m│\033[0m\r\n", start_y + 3, start_x, exe_path);
    printf("\033[%d;%dH\033[1;35m│\033[0m  \033[1;36m• Diretório CWD:\033[0m %-52.52s \033[1;35m│\033[0m\r\n", start_y + 4, start_x, cwd_path);
    printf("\033[%d;%dH\033[1;35m│\033[0m  \033[1;36m• Linha Comando:\033[0m \033[1;33m%-52.52s\033[0m \033[1;35m│\033[0m\r\n", start_y + 5, start_x, cmdline);
    printf("\033[%d;%dH\033[1;35m│\033[0m  \033[1;36m• Processo Pai :\033[0m PID %d (%s)%-*s \033[1;35m│\033[0m\r\n", start_y + 6, start_x, ppid, ppid_name, (int)(38 - snprintf(NULL, 0, "%d%s", ppid, ppid_name)), "");
    printf("\033[%d;%dH\033[1;35m│\033[0m  \033[1;36m• Estado/Prior.:\033[0m \033[1;32m%s\033[0m | Nice: %d | Threads: %s%-*s \033[1;35m│\033[0m\r\n", start_y + 7, start_x, proc_state, getpriority(PRIO_PROCESS, pid), threads_str, (int)(25 - snprintf(NULL, 0, "%s%s", proc_state, threads_str)), "");
    printf("\033[%d;%dH\033[1;35m│\033[0m  \033[1;36m• Arquivos/FDs :\033[0m \033[1;37m%d descritores abertos\033[0m em /proc/%d/fd%-*s \033[1;35m│\033[0m\r\n", start_y + 8, start_x, fd_count, pid, (int)(20 - snprintf(NULL, 0, "%d%d", fd_count, pid)), "");
    printf("\033[%d;%dH\033[1;35m│\033[0m  \033[1;36m• Memória Real :\033[0m \033[1;32mRSS: %s\033[0m | Virtual: %s%-*s \033[1;35m│\033[0m\r\n", start_y + 9, start_x, vm_rss, vm_size, (int)(30 - snprintf(NULL, 0, "%s%s", vm_rss, vm_size)), "");
    printf("\033[%d;%dH\033[1;35m│\033[0m  \033[1;36m• Segmentos RAM:\033[0m Heap: %s | Stack: %s | Code: %s%-*s \033[1;35m│\033[0m\r\n", start_y + 10, start_x, vm_data, vm_stk, vm_exe, (int)(15 - snprintf(NULL, 0, "%s%s%s", vm_data, vm_stk, vm_exe)), "");
    printf("\033[%d;%dH\033[1;35m│\033[0m  \033[1;36m• Trocas de Ctx:\033[0m %s voluntárias | %s involuntárias%-*s \033[1;35m│\033[0m\r\n", start_y + 11, start_x, vctx, nvctx, (int)(20 - snprintf(NULL, 0, "%s%s", vctx, nvctx)), "");
    printf("\033[%d;%dH\033[1;35m│\033[0m  \033[1;36m• Variáveis Env:\033[0m %d variáveis de ambiente carregadas%-*s \033[1;35m│\033[0m\r\n", start_y + 12, start_x, env_count, (int)(25 - snprintf(NULL, 0, "%d", env_count)), "");

    printf("\033[%d;%dH\033[1;35m├", start_y + 13, start_x);
    for (int i = 0; i < modal_w - 2; i++) printf("─");
    printf("┤\033[0m\r\n");

    printf("\033[%d;%dH\033[1;35m│\033[0m  \033[0;90m[ Pressione qualquer tecla ou ESC para fechar este painel ]\033[0m%-*s \033[1;35m│\033[0m\r\n",
           start_y + 14, start_x, (int)(modal_w - 63), "");

    printf("\033[%d;%dH\033[1;35m╰", start_y + 15, start_x);
    for (int i = 0; i < modal_w - 2; i++) printf("─");
    printf("╯\033[0m");
    fflush(stdout);

    char dummy;
    while (read(STDIN_FILENO, &dummy, 1) <= 0);
}

static size_t collect_processes(ProcEntry *procs, size_t max_count, int *out_running, int *out_sleeping, int *out_zombies, int *out_stopped) {
    DIR *dir = opendir("/proc");
    if (!dir) return 0;

    size_t count = 0;
    struct dirent *de;
    *out_running = 0; *out_sleeping = 0; *out_zombies = 0; *out_stopped = 0;

    while ((de = readdir(dir)) != NULL && count < max_count) {
        if (!isdigit(de->d_name[0])) continue;
        pid_t pid = atoi(de->d_name);

        ProcEntry p;
        memset(&p, 0, sizeof(ProcEntry));
        p.pid = pid;
        strcpy(p.user, "unknown");
        strcpy(p.cmd, "unknown");

        char path[128];
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        FILE *fp = fopen(path, "r");
        if (fp) {
            char comm[128] = "";
            long rss_pages = 0;
            unsigned long vsz = 0;
            int ppid = 0, nice_v = 0, threads = 1;
            char state = 'S';
            unsigned long utime = 0, stime = 0;

            if (fscanf(fp, "%*d (%127[^)]) %c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d %*d %d %d %*d %*u %lu %ld",
                       comm, &state, &ppid, &utime, &stime, &nice_v, &threads, &vsz, &rss_pages) >= 5) {
                p.state = state;
                p.ppid = ppid;
                p.nice_val = nice_v;
                p.threads = threads;
                p.vsz_kb = vsz / 1024;
                p.rss_kb = (rss_pages * sysconf(_SC_PAGESIZE)) / 1024;
                p.utime_stime = utime + stime;
                strncpy(p.cmd, comm, sizeof(p.cmd) - 1);

                if (state == 'R') (*out_running)++;
                else if (state == 'Z') (*out_zombies)++;
                else if (state == 'T' || state == 't') (*out_stopped)++;
                else (*out_sleeping)++;
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
                    if (pw) strncpy(p.user, pw->pw_name, sizeof(p.user) - 1);
                    else snprintf(p.user, sizeof(p.user), "%u", (unsigned int)uid);
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
                strncpy(p.cmd, cmdline, sizeof(p.cmd) - 1);
            }
            fclose(fp);
        }

        // Filtro de busca se ativo
        if (strlen(filter_query) > 0) {
            if (strcasestr(p.cmd, filter_query) == NULL && strcasestr(p.user, filter_query) == NULL) {
                continue;
            }
        }

        procs[count++] = p;
    }
    closedir(dir);
    qsort(procs, count, sizeof(ProcEntry), compare_procs);
    return count;
}

static void render_screen(const ProcEntry *procs, size_t count, int running, int sleeping, int zombies, int stopped) {
    int rows, cols;
    get_window_size(&rows, &cols);

    printf("\033[H");

    // 1. Medidores de Hardware no Topo
    update_global_cpu();

    unsigned long long u_ram = 0, t_ram = 0, u_swp = 0, t_swp = 0;
    get_global_mem(&u_ram, &t_ram, &u_swp, &t_swp);

    int bar_w = cols / 3 - 10;
    if (bar_w < 10) bar_w = 10;
    if (bar_w > 25) bar_w = 25;

    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    char cpu_tag[64], ram_tag[64], swp_tag[64];
    snprintf(cpu_tag, sizeof(cpu_tag), "(%ld Cores)", num_cores);
    snprintf(ram_tag, sizeof(ram_tag), "(%.1fG/%.1fG)", (double)u_ram / (1024*1024), (double)t_ram / (1024*1024));
    snprintf(swp_tag, sizeof(swp_tag), "(%.1fG/%.1fG)", (double)u_swp / (1024*1024), (double)t_swp / (1024*1024));

    draw_bar("CPU", global_cpu_load, 100.0, COLOR_BAR_CPU, bar_w, cpu_tag);
    draw_bar("RAM", (double)u_ram, (double)t_ram, COLOR_BAR_RAM, bar_w, ram_tag);
    draw_bar("SWP", (double)u_swp, (double)t_swp, COLOR_BAR_SWP, bar_w, swp_tag);

    // Linha de Resumo de Tarefas e Uptime
    double uptime_s = 0;
    FILE *ufp = fopen("/proc/uptime", "r");
    if (ufp) { if (fscanf(ufp, "%lf", &uptime_s) != 1) uptime_s = 0; fclose(ufp); }
    int udays = (int)uptime_s / 86400, uhours = ((int)uptime_s % 86400) / 3600, umins = ((int)uptime_s % 3600) / 60;

    const char *sort_str = (sort_mode == 0) ? "RAM (RSS)" : (sort_mode == 1) ? "CPU%" : "PID";
    printf("  %sTasks:%s %zu (R:%s%d%s S:%d T:%d Z:%s%d%s) | %sUptime:%s %dd %02dh %02dm | %sSort:%s %s%s\033[K\r\n",
           LOW_COLOR_LABEL, COLOR_RESET, count,
           COLOR_RUN, running, COLOR_RESET, sleeping, stopped,
           COLOR_ZOMBIE, zombies, COLOR_RESET,
           LOW_COLOR_LABEL, COLOR_RESET, udays, uhours, umins,
           LOW_COLOR_LABEL, COLOR_RESET, sort_str, sort_reverse ? " (Rev)" : "");

    // 2. Colunas da Tabela
    printf("%s  %-6s %-12s %-5s %-4s %-8s %-9s %s%s\033[K\r\n",
           COLOR_HEADER, "PID", "USER", "STAT", "THRD", "RSS(MB)", "VSZ(MB)", "COMMAND", COLOR_RESET);

    int visible_rows = rows - 8;
    if (visible_rows < 1) visible_rows = 1;

    if (selected_idx < scroll_offset) scroll_offset = selected_idx;
    if (selected_idx >= scroll_offset + visible_rows) scroll_offset = selected_idx - visible_rows + 1;

    for (int r = 0; r < visible_rows; r++) {
        size_t idx = scroll_offset + r;
        if (idx < count) {
            const ProcEntry *p = &procs[idx];
            int is_sel = ((int)idx == selected_idx);

            const char *st_col = (p->state == 'R') ? COLOR_RUN : (p->state == 'Z') ? COLOR_ZOMBIE : (p->state == 'D') ? COLOR_DISK : COLOR_SLEEP;

            if (is_sel) {
                printf("%s> %-6d %-12.12s [%c]   %-4d %6.1fM %7.1fM %-*.*s%s\033[K\r\n",
                       COLOR_SEL, p->pid, p->user, p->state, p->threads,
                       (double)p->rss_kb / 1024.0, (double)p->vsz_kb / 1024.0,
                       cols - 50, cols - 50, p->cmd, COLOR_RESET);
            } else {
                printf("  %s%-6d%s %s%-12.12s%s %s[%c]%s   %-4d %6.1fM %7.1fM %s%-*.*s%s\033[K\r\n",
                       COLOR_PID, p->pid, COLOR_RESET,
                       COLOR_USER, p->user, COLOR_RESET,
                       st_col, p->state, COLOR_RESET,
                       p->threads,
                       (double)p->rss_kb / 1024.0, (double)p->vsz_kb / 1024.0,
                       COLOR_RESET, cols - 50, cols - 50, p->cmd, COLOR_RESET);
            }
        } else {
            printf("\033[K\r\n");
        }
    }

    // 3. Rodapé de Status com Atalhos
    char bot_bar[512];
    if (strlen(status_msg) > 0) {
        snprintf(bot_bar, sizeof(bot_bar), " %s ", status_msg);
    } else if (strlen(filter_query) > 0) {
        snprintf(bot_bar, sizeof(bot_bar), " [Filtro: '%s'] [ESC p/ limpar] | [Enter] Raio-X | [k] Kill | [q] Sair ", filter_query);
    } else {
        snprintf(bot_bar, sizeof(bot_bar),
                 " [↑/↓] Mover | [Enter] Raio-X | [/] Filtro | [m] RAM | [c] PID | [r] Inverter | [k] Kill ");
    }
    printf("%s%-*.*s%s", COLOR_FOOTER, cols, cols, bot_bar, COLOR_RESET);
    fflush(stdout);
}

// Prompt para Filtro (/)
static void prompt_filter(void) {
    int rows, cols;
    get_window_size(&rows, &cols);

    printf("\033[%d;1H%s Filtro de Processo: %-.*s%*s%s",
           rows, COLOR_FOOTER, 40, filter_query, cols - 24 - (int)strlen(filter_query), "", COLOR_RESET);
    printf("\033[%d;%dH\033[?25h", rows, 23 + (int)strlen(filter_query));
    fflush(stdout);

    char buf[64] = "";
    strncpy(buf, filter_query, sizeof(buf) - 1);
    size_t idx = strlen(buf);

    while (1) {
        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) break;

        if (c == 27 || c == 3) { // ESC limpa
            filter_query[0] = '\0';
            break;
        }

        if (c == '\r' || c == '\n') {
            strncpy(filter_query, buf, sizeof(filter_query) - 1);
            break;
        }

        if (c == 127 || c == '\b') {
            if (idx > 0) buf[--idx] = '\0';
        } else if (isprint((unsigned char)c) && idx < sizeof(buf) - 1) {
            buf[idx++] = c;
            buf[idx] = '\0';
        }

        printf("\033[%d;1H%s Filtro de Processo: %-.*s%*s%s",
               rows, COLOR_FOOTER, 40, buf, cols - 24 - (int)idx, "", COLOR_RESET);
        printf("\033[%d;%dH", rows, 23 + (int)idx);
        fflush(stdout);
    }
}

int main(int argc, char *argv[]) {
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        low_print_banner("ltop");
        printf("USAGE:\n  ./ltop\n\nInteractive real-time process manager, hardware bars, and process X-Ray inspector.\n");
        return 0;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    enable_raw_mode();

    ProcEntry *procs = malloc(4096 * sizeof(ProcEntry));
    if (!procs) return 1;

    while (1) {
        int running = 0, sleeping = 0, zombies = 0, stopped = 0;
        size_t count = collect_processes(procs, 4096, &running, &sleeping, &zombies, &stopped);
        if (selected_idx >= (int)count) selected_idx = count > 0 ? count - 1 : 0;

        render_screen(procs, count, running, sleeping, zombies, stopped);
        status_msg[0] = '\0';

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };

        int sel = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
        if (sel > 0) {
            char buf[32];
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
            if (n <= 0) break;
            buf[n] = '\0';

            char c = buf[0];

            if (c == 'q' || c == 'Q' || (c == 27 && n == 1)) {
                if (strlen(filter_query) > 0) {
                    filter_query[0] = '\0';
                    continue;
                }
                break;
            }

            // ENTER -> Abre o Raio-X Avançado do Processo
            if (c == '\r' || c == '\n') {
                if (count > 0 && selected_idx < (int)count) {
                    show_process_xray_modal(procs[selected_idx].pid);
                }
                continue;
            }

            // / -> Filtro de Processos
            if (c == '/') {
                prompt_filter();
                selected_idx = 0;
                continue;
            }

            // Ordenação
            if (c == 'm' || c == 'M') { sort_mode = 0; selected_idx = 0; }
            else if (c == 'c' || c == 'C') { sort_mode = 2; selected_idx = 0; }
            else if (c == 'r' || c == 'R') { sort_reverse = !sort_reverse; }

            // Sinais
            else if (c == 'k' || c == 'K') {
                if (count > 0 && selected_idx < (int)count) {
                    pid_t target = procs[selected_idx].pid;
                    if (kill(target, SIGTERM) == 0) {
                        snprintf(status_msg, sizeof(status_msg), "[✔] Enviado SIGTERM (15) para PID %d (%s)", target, procs[selected_idx].cmd);
                    } else {
                        snprintf(status_msg, sizeof(status_msg), "[✖] Erro ao enviar sinal: %s", strerror(errno));
                    }
                }
            } else if (c == '9') {
                if (count > 0 && selected_idx < (int)count) {
                    pid_t target = procs[selected_idx].pid;
                    if (kill(target, SIGKILL) == 0) {
                        snprintf(status_msg, sizeof(status_msg), "[✔] Enviado SIGKILL (9) para PID %d", target);
                    } else {
                        snprintf(status_msg, sizeof(status_msg), "[✖] Erro ao enviar SIGKILL: %s", strerror(errno));
                    }
                }
            } else if (c == 'p' || c == 'P') {
                if (count > 0 && selected_idx < (int)count) {
                    pid_t target = procs[selected_idx].pid;
                    int sig = (procs[selected_idx].state == 'T' || procs[selected_idx].state == 't') ? SIGCONT : SIGSTOP;
                    if (kill(target, sig) == 0) {
                        snprintf(status_msg, sizeof(status_msg), "[✔] PID %d %s", target, (sig == SIGSTOP) ? "Pausado (SIGSTOP)" : "Despausado (SIGCONT)");
                    }
                }
            }

            // Setas e Navegação
            else if (c == 27 && n >= 3 && buf[1] == '[') {
                if (buf[2] == 'A') { // Cima
                    if (selected_idx > 0) selected_idx--;
                } else if (buf[2] == 'B') { // Baixo
                    if (selected_idx + 1 < (int)count) selected_idx++;
                } else if (buf[2] == '5' && n >= 4) { // PgUp
                    selected_idx = (selected_idx > 10) ? selected_idx - 10 : 0;
                } else if (buf[2] == '6' && n >= 4) { // PgDn
                    selected_idx = (selected_idx + 10 < (int)count) ? selected_idx + 10 : count - 1;
                }
            }
        }
    }

    free(procs);
    return 0;
}
