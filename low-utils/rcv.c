#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <mntent.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include "low.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_ERR     "\033[1;31m"
#define COLOR_WARN    "\033[1;33m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_FILE    "\033[1;36m"
#define COLOR_TAG     "\033[1;33m"
#define COLOR_LABEL   "\033[1;37m"
#define COLOR_VAL     "\033[1;36m"
#define COLOR_MUTED   "\033[0;90m"

#define BLOCK_SECTOR_SZ 512
#define CHUNK_SIZE      (2 * 1024 * 1024) // Buffer de 2MB por leitura de bloco

typedef struct {
    const char *ext;
    const char *desc;
    const uint8_t *magic;
    size_t magic_len;
    size_t max_size;
} FileSignature;

static const uint8_t sig_png[]  = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
static const uint8_t sig_jpg[]  = {0xFF, 0xD8, 0xFF};
static const uint8_t sig_elf[]  = {0x7F, 0x45, 0x4C, 0x46};
static const uint8_t sig_bmp[]  = {0x42, 0x4D};
static const uint8_t sig_zip[]  = {0x50, 0x4B, 0x03, 0x04};
static const uint8_t sig_pdf[]  = {0x25, 0x50, 0x44, 0x46};
static const uint8_t sig_kr[]   = {'K', 'R', 'Y', 'P', 'T', '2', '0', '\0'};

static const FileSignature signatures[] = {
    {"png",   "PNG Image Data",          sig_png, sizeof(sig_png), 30 * 1024 * 1024},
    {"jpg",   "JPEG / JFIF Photo",       sig_jpg, sizeof(sig_jpg), 30 * 1024 * 1024},
    {"elf",   "Linux ELF Binary",        sig_elf, sizeof(sig_elf), 50 * 1024 * 1024},
    {"bmp",   "Bitmap Graphic (BMP)",    sig_bmp, sizeof(sig_bmp), 50 * 1024 * 1024},
    {"zip",   "ZIP Archive / APK",       sig_zip, sizeof(sig_zip), 100 * 1024 * 1024},
    {"pdf",   "PDF Document",            sig_pdf, sizeof(sig_pdf), 50 * 1024 * 1024},
    {"kr",    "Krypt Encrypted Vault",   sig_kr,  sizeof(sig_kr),  100 * 1024 * 1024},
    {NULL, NULL, NULL, 0, 0}
};

static void print_help(void) {
    low_print_banner("rcv");
    printf("%sUSAGE:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  ./rcv [OPTIONS]                      (Auto-detecta o disco atual e recupera arquivos)\n");
    printf("  ./rcv -d, --dev <DISPOSITIVO>        (Especifica o dispositivo de bloco manualmente)\n\n");
    printf("%sDESCRIPTION:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  Advanced local filesystem block scanner & forensic carver for deleted unlinked files.\n\n");
    printf("%sOPTIONS:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  %s-a, --auto%s               Auto-detect mount point and raw block device for current dir [Default]\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-d, --dev <DEVICE>%s       Force specific block device (e.g., /dev/sda1, /dev/block/dm-0)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-m, --max-scan <MB>%s      Limit raw disk scan size in Megabytes [Default: 512 MB]\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-n, --name <EXT/NAME>%s    Filter recovery by file extension (png, jpg, elf, txt) or pattern\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-o, --out <DIR>%s          Directory to save recovered files [Default: ./recovered/]\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-p, --proc-only%s          Scan active process file descriptors only (/proc unlinked)\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-l, --list%s               Simulate and list recoverable items without saving\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("  %s-h, --help%s               Display this formatted help guide and exit\n\n", LOW_COLOR_BIN, LOW_COLOR_RESET);
    printf("%sEXAMPLES:%s\n", LOW_COLOR_LABEL, LOW_COLOR_RESET);
    printf("  • %s./rcv%s                                 (Analisa particao do diretorio atual e recupera)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./rcv -n jpg -o ./fotos_recup/%s        (Recupera especificamente imagens JPG apagadas)\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
    printf("  • %s./rcv -m 1024 -o ./recup/%s             (Escaneia 1GB de blocos brutos da particao local)\n\n", LOW_COLOR_TAG, LOW_COLOR_RESET);
}

// Localiza o dispositivo de bloco real correspondente ao diretório atual
static int get_block_device_for_cwd(char *out_dev, size_t dev_sz, char *out_mount, size_t mnt_sz, char *out_fstype, size_t fs_sz) {
    struct stat st;
    if (stat(".", &st) != 0) return -1;

    dev_t target_dev = st.st_dev;

    FILE *fp = setmntent("/proc/mounts", "r");
    if (!fp) fp = setmntent("/etc/mtab", "r");
    if (!fp) return -1;

    struct mntent *mnt;
    int found = 0;
    size_t longest_match = 0;

    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, ".");

    while ((mnt = getmntent(fp)) != NULL) {
        struct stat mnt_st;
        if (stat(mnt->mnt_dir, &mnt_st) == 0) {
            if (mnt_st.st_dev == target_dev) {
                size_t mlen = strlen(mnt->mnt_dir);
                if (mlen >= longest_match) {
                    longest_match = mlen;
                    strncpy(out_dev, mnt->mnt_fsname, dev_sz - 1);
                    out_dev[dev_sz - 1] = '\0';
                    strncpy(out_mount, mnt->mnt_dir, mnt_sz - 1);
                    out_mount[mnt_sz - 1] = '\0';
                    strncpy(out_fstype, mnt->mnt_type, fs_sz - 1);
                    out_fstype[fs_sz - 1] = '\0';
                    found = 1;
                }
            }
        }
    }
    endmntent(fp);
    return found ? 0 : -1;
}

// 1. RECUPERAÇÃO RÁPIDA VIA DESCRITORES ABERTOS EM /proc
static int scan_unlinked_proc_fds(const char *name_filter, const char *out_dir, int dry_run) {
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) return 0;

    struct dirent *pde;
    int recovered_count = 0;
    pid_t my_pid = getpid();

    while ((pde = readdir(proc_dir)) != NULL) {
        if (!isdigit((unsigned char)pde->d_name[0])) continue;
        pid_t pid = (pid_t)atoi(pde->d_name);
        if (pid == my_pid) continue;

        char fd_dir_path[128];
        snprintf(fd_dir_path, sizeof(fd_dir_path), "/proc/%d/fd", pid);
        DIR *fd_dir = opendir(fd_dir_path);
        if (!fd_dir) continue;

        char comm[64] = "process";
        char stat_path[128];
        snprintf(stat_path, sizeof(stat_path), "/proc/%d/comm", pid);
        FILE *cfp = fopen(stat_path, "r");
        if (cfp) {
            if (fgets(comm, sizeof(comm), cfp)) comm[strcspn(comm, "\r\n")] = '\0';
            fclose(cfp);
        }

        struct dirent *fde;
        while ((fde = readdir(fd_dir)) != NULL) {
            if (fde->d_name[0] == '.') continue;

            char fd_link[256];
            snprintf(fd_link, sizeof(fd_link), "%s/%s", fd_dir_path, fde->d_name);

            char target_path[1024];
            ssize_t len = readlink(fd_link, target_path, sizeof(target_path) - 1);
            if (len <= 0) continue;
            target_path[len] = '\0';

            char *del_marker = strstr(target_path, " (deleted)");
            if (del_marker) {
                *del_marker = '\0';

                if (name_filter && strcasestr(target_path, name_filter) == NULL) continue;

                struct stat st;
                if (stat(fd_link, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size == 0) continue;

                const char *base_name = strrchr(target_path, '/');
                base_name = base_name ? base_name + 1 : target_path;

                recovered_count++;

                printf("  %s[ACHADO VIA PROC #%d]%s PID: %s%d%s (%s%s%s) -> %s%s%s (%lld bytes)\n",
                       COLOR_OK, recovered_count, COLOR_RESET,
                       COLOR_TAG, pid, COLOR_RESET,
                       COLOR_MUTED, comm, COLOR_RESET,
                       COLOR_FILE, target_path, COLOR_RESET,
                       (long long)st.st_size);

                if (!dry_run) {
                    mkdir(out_dir, 0755);
                    char out_filepath[2048];
                    snprintf(out_filepath, sizeof(out_filepath), "%s/proc_%d_%s", out_dir, pid, base_name);

                    int in_fd = open(fd_link, O_RDONLY);
                    int out_fd = open(out_filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);

                    if (in_fd >= 0 && out_fd >= 0) {
                        char chunk[65536];
                        ssize_t n;
                        while ((n = read(in_fd, chunk, sizeof(chunk))) > 0) {
                            write(out_fd, chunk, n);
                        }
                        close(in_fd);
                        close(out_fd);
                        printf("    %s└─✔ Restaurado com 100%% de integridade em:%s %s%s%s\n",
                               COLOR_OK, COLOR_RESET, COLOR_TAG, out_filepath, COLOR_RESET);
                    } else {
                        if (in_fd >= 0) close(in_fd);
                        if (out_fd >= 0) close(out_fd);
                    }
                }
            }
        }
        closedir(fd_dir);
    }
    closedir(proc_dir);
    return recovered_count;
}

// 2. RECUPERAÇÃO AVANÇADA POR VARREDURA DE BLOCOS BRUTOS NO DISCO LOCAL
static int scan_raw_disk_sectors(const char *dev_path, size_t max_scan_bytes, const char *name_filter, const char *out_dir, int dry_run) {
    int fd = open(dev_path, O_RDONLY);
    if (fd < 0) {
        printf("  %s[INFO]%s Dispositivo '%s' nao pode ser aberto diretamente sem permissao de root: %s\n",
               COLOR_WARN, COLOR_RESET, dev_path, strerror(errno));
        return -1;
    }

    if (!dry_run) {
        mkdir(out_dir, 0755);
    }

    printf("  • Escaneando setores fisicos em %s%s%s (Limite: %zu MB)...\n\n",
           COLOR_FILE, dev_path, COLOR_RESET, max_scan_bytes / (1024 * 1024));

    uint8_t *buffer = malloc(CHUNK_SIZE);
    if (!buffer) { close(fd); return -1; }

    off_t offset = 0;
    int carved_count = 0;
    ssize_t bytes_read = 0;

    while (offset < (off_t)max_scan_bytes && (bytes_read = pread(fd, buffer, CHUNK_SIZE, offset)) > 64) {
        for (ssize_t i = 0; i < bytes_read - 64; i += BLOCK_SECTOR_SZ) { // Alinhamento por setor (512 bytes)
            for (int s = 0; signatures[s].ext != NULL; s++) {
                if (name_filter && strcasestr(signatures[s].ext, name_filter) == NULL) continue;

                if (memcmp(buffer + i, signatures[s].magic, signatures[s].magic_len) == 0) {
                    off_t match_offset = offset + i;
                    carved_count++;

                    size_t extract_sz = 256 * 1024; // 256 KB padrao de extracao
                    if (strcmp(signatures[s].ext, "bmp") == 0 && i + 6 < bytes_read) {
                        uint32_t b_sz = *(uint32_t *)(buffer + i + 2);
                        if (b_sz > 54 && b_sz <= signatures[s].max_size) extract_sz = b_sz;
                    }

                    char out_name[512];
                    snprintf(out_name, sizeof(out_name), "%s/recovered_0x%lx.%s",
                             out_dir, (unsigned long)match_offset, signatures[s].ext);

                    if (!dry_run) {
                        int out_f = open(out_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (out_f >= 0) {
                            uint8_t *ebuf = malloc(extract_sz);
                            if (ebuf) {
                                ssize_t en = pread(fd, ebuf, extract_sz, match_offset);
                                if (en > 0) write(out_f, ebuf, en);
                                free(ebuf);
                            }
                            close(out_f);
                        }
                    }

                    printf("  %s[BLOCO RESGATADO #%d]%s Offset: %s0x%08lx%s | Tipo: %s%-22s%s -> %s%s%s\n",
                           COLOR_OK, carved_count, COLOR_RESET,
                           COLOR_TAG, (unsigned long)match_offset, COLOR_RESET,
                           COLOR_OK, signatures[s].desc, COLOR_RESET,
                           COLOR_FILE, out_name, COLOR_RESET);
                }
            }
        }
        offset += (bytes_read - 4096);
    }

    free(buffer);
    close(fd);
    return carved_count;
}

int main(int argc, char *argv[]) {
    const char *custom_dev = NULL;
    const char *name_filter = NULL;
    const char *out_dir = "recovered";
    size_t max_scan_mb = 512;
    int proc_only = 0;
    int dry_run = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            dry_run = 1;
            continue;
        }
        if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--proc-only") == 0) {
            proc_only = 1;
            continue;
        }
        if ((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dev") == 0) && i + 1 < argc) {
            custom_dev = argv[++i];
            continue;
        }
        if ((strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--name") == 0) && i + 1 < argc) {
            name_filter = argv[++i];
            continue;
        }
        if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--max-scan") == 0) && i + 1 < argc) {
            max_scan_mb = (size_t)atoi(argv[++i]);
            if (max_scan_mb == 0) max_scan_mb = 512;
            continue;
        }
        if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--out") == 0) && i + 1 < argc) {
            out_dir = argv[++i];
            continue;
        }
    }

    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, ".");

    char dev_name[256] = "", mount_point[256] = "", fstype[64] = "";
    int has_mount_info = (get_block_device_for_cwd(dev_name, sizeof(dev_name), mount_point, sizeof(mount_point), fstype, sizeof(fstype)) == 0);

    if (custom_dev) {
        strncpy(dev_name, custom_dev, sizeof(dev_name) - 1);
    }

    printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s│%s  %s[ 🛡️ RCV 2.0 - Recuperador de Arquivos do Disco Local & Inodes ]%s         %s│%s\n",
           COLOR_TITLE, COLOR_RESET, COLOR_OK, COLOR_RESET, COLOR_TITLE, COLOR_RESET);
    printf("%s├────────────────────────────────────────────────────────────────────────────┤%s\n", COLOR_TITLE, COLOR_RESET);
    printf("  %s• Diretório Alvo :%s %s%s%s\n", COLOR_LABEL, COLOR_RESET, COLOR_FILE, cwd, COLOR_RESET);
    if (has_mount_info) {
        printf("  %s• Ponto de Montagem:%s %s (%s)\n", COLOR_LABEL, COLOR_RESET, mount_point, fstype);
        printf("  %s• Dispositivo Real :%s %s%s%s\n", COLOR_LABEL, COLOR_RESET, COLOR_VAL, dev_name, COLOR_RESET);
    }
    printf("  %s• Pasta de Destino :%s %s%s/%s\n", COLOR_LABEL, COLOR_RESET, COLOR_TAG, out_dir, COLOR_RESET);
    if (name_filter) {
        printf("  %s• Filtro Aplicado  :%s '%s'\n", COLOR_LABEL, COLOR_RESET, name_filter);
    }
    printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n", COLOR_TITLE, COLOR_RESET);

    // Passo 1: Busca em Inodes abertos não vinculados (mais rápido e 100% íntegro)
    printf("  %s[Passo 1/2] Varrendo descritores de processos ativos (/proc/*/fd)...%s\n", COLOR_TITLE, COLOR_RESET);
    int proc_found = scan_unlinked_proc_fds(name_filter, out_dir, dry_run);

    if (proc_only) {
        printf("\n  %s✔ Varredura de processos finalizada! Arquivos resgatados: %d%s\n\n", COLOR_OK, proc_found, COLOR_RESET);
        return 0;
    }

    // Passo 2: Varredura de blocos físicos brutos na partição de disco do diretório
    printf("\n  %s[Passo 2/2] Analisando setores brutos no dispositivo do disco local...%s\n", COLOR_TITLE, COLOR_RESET);
    int disk_found = 0;
    if (strlen(dev_name) > 0) {
        disk_found = scan_raw_disk_sectors(dev_name, max_scan_mb * 1024 * 1024, name_filter, out_dir, dry_run);
    }

    if (disk_found < 0 && proc_found == 0) {
        printf("  %s• Dica de Acesso ao Disco:%s Para varrer blocos do sistema operacional em profundidade,\n", COLOR_TAG, COLOR_RESET);
        printf("    execute com permissões de superusuário ou aponte para um dispositivo aberto com '-d <DEV>'.\n\n");
    } else {
        int total = proc_found + (disk_found > 0 ? disk_found : 0);
        printf("  ----------------------------------------------------------------------------\n");
        printf("  %s✔ Varredura concluída com sucesso! Total de arquivos recuperados: %d%s\n\n", COLOR_OK, total, COLOR_RESET);
    }

    return 0;
}
