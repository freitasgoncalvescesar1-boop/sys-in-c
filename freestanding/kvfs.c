#include "kvfs.h"
#include "kstring.h"
#include "kmem.h"
#include "kbmp.h"

static kvfs_node_t vfs_table[KVFS_MAX_NODES];
static size_t vfs_node_count = 0;

static const char etc_hostname[] = "utils-os\n";
static const char etc_os_release[] = "NAME=\"utils-in-c OS\"\nVERSION=\"2.6\"\nID=utils-os\n";
static char etc_passwd_buf[512] = "root:root:0:0:root:/root:/bin/sh\nuser:1234:1000:1000:User:/home/user:/bin/sh\n";
static const char etc_kernel_config[] = "KERNEL=Multiboot1\nVIDEO=VBE800x600x32\nVFS=KSFS_ATA_PERSISTENT\n";
static const char etc_motd[] = "Welcome to utils-in-c OS v2.6!\nReal Persistent ATA Disk & Multi-User System Active.\n";
static const char readme_root[] = "Welcome to utils-in-c OS VFS Root.\nUse 'help' to see all commands.";

static const char dev_null_info[] = "[DEV] Character Device: Null (/dev/null)\n";
static const char dev_zero_info[] = "[DEV] Character Device: Zero Source (/dev/zero)\n";
static const char dev_fb0_info[] = "[DEV] Video Framebuffer: 800x600x32bpp VBE ARGB at 0xFD000000\n";
static const char dev_ps2kbd_info[] = "[DEV] Input Device: PS/2 Keyboard on Port 0x60 (IRQ 1)\n";
static const char dev_ps2mouse_info[] = "[DEV] Input Device: PS/2 Mouse on Port 0x60 (IRQ 12)\n";
static const char dev_ata0_info[] = "[DEV] Block Device: Primary Master ATA PIO Hard Disk on 0x1F0\n";

static const char bin_ls_help[] = "[BIN] ls [dir] - Lista arquivos\n";
static const char bin_cd_help[] = "[BIN] cd <dir> - Navega entre pastas\n";
static const char bin_pwd_help[] = "[BIN] pwd - Diretorio atual\n";
static const char bin_cat_help[] = "[BIN] cat <path> - Exibe arquivo\n";
static const char bin_view_help[] = "[BIN] view <arquivo.bmp> - Visualizador de imagens BMP\n";
static const char bin_calc_help[] = "[BIN] calc_gui - Calculadora visual flutuante\n";
static const char bin_snake_help[] = "[BIN] snake - Jogo retro Cobrinha\n";
static const char bin_beep_help[] = "[BIN] beep [freq] [ms] - Emite som no PC Speaker\n";
static const char bin_write_help[] = "[BIN] write <path> <texto> - Cria/escreve arquivo persistente no disco\n";
static const char bin_touch_help[] = "[BIN] touch <path> - Cria arquivo vazio\n";
static const char bin_mkdir_help[] = "[BIN] mkdir <path> - Cria diretorio\n";
static const char bin_rm_help[] = "[BIN] rm <path> - Deleta arquivo do disco\n";
static const char bin_whoami_help[] = "[BIN] whoami - Exibe o usuario atual\n";
static const char bin_su_help[] = "[BIN] su <usuario> - Alterna de usuario com senha\n";
static const char bin_adduser_help[] = "[BIN] adduser <usuario> <senha> - Cria novo usuario\n";
static const char bin_mem_help[] = "[BIN] mem - Heap KMEM\n";
static const char bin_clear_help[] = "[BIN] clear - Limpa tela\n";
static const char bin_ata_help[] = "[BIN] ata [info|0] - Testa disco ATA\n";
static const char bin_format_help[] = "[BIN] format_disk - Formata disk.img com KSFS\n";
static const char bin_exit_help[] = "[BIN] exit - Retorna ao GUI\n";

// Gera imagem BMP 32x32 de demonstração na memória
static uint8_t sample_bmp_buffer[sizeof(kbmp_header_t) + (32 * 32 * 3)];

static void generate_sample_bmp(void) {
    kbmp_header_t *hdr = (kbmp_header_t *)sample_bmp_buffer;
    kmemset(sample_bmp_buffer, 0, sizeof(sample_bmp_buffer));

    hdr->type = 0x4D42; // "BM"
    hdr->size = sizeof(sample_bmp_buffer);
    hdr->offset = sizeof(kbmp_header_t);
    hdr->header_size = 40;
    hdr->width = 32;
    hdr->height = 32;
    hdr->planes = 1;
    hdr->bpp = 24;
    hdr->image_size = 32 * 32 * 3;

    uint8_t *px = sample_bmp_buffer + sizeof(kbmp_header_t);
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            int idx = (y * 32 + x) * 3;
            px[idx + 0] = (uint8_t)(x * 8);       // B
            px[idx + 1] = (uint8_t)(y * 8);       // G
            px[idx + 2] = (uint8_t)((x + y) * 4); // R
        }
    }
}

static void sanitize_path(const char *in, char *out, size_t max_len) {
    size_t j = 0;
    if (!in || in[0] == '\0') {
        out[0] = '/'; out[1] = '\0'; return;
    }
    if (in[0] != '/') out[j++] = '/';
    for (size_t i = 0; in[i] != '\0' && j < max_len - 1; i++) {
        if (in[i] == '/' && j > 0 && out[j - 1] == '/') continue;
        out[j++] = in[i];
    }
    while (j > 1 && out[j - 1] == '/') j--;
    out[j] = '\0';
}

void kvfs_init(void) {
    for (size_t i = 0; i < KVFS_MAX_NODES; i++) {
        if (vfs_table[i].is_used && vfs_table[i].is_dynamic && vfs_table[i].data) {
            kfree((void *)vfs_table[i].data);
        }
        vfs_table[i].is_used = 0;
        vfs_table[i].is_dynamic = 0;
        vfs_table[i].path[0] = '\0';
        vfs_table[i].data = NULL;
        vfs_table[i].size = 0;
    }
    vfs_node_count = 0;

    generate_sample_bmp();

    kvfs_mkdir("/");
    kvfs_mkdir("/bin");
    kvfs_mkdir("/etc");
    kvfs_mkdir("/dev");
    kvfs_mkdir("/home");
    kvfs_mkdir("/home/user");

    kvfs_create("/readme.txt", readme_root, sizeof(readme_root) - 1, KVFS_TYPE_FILE, 0644);
    kvfs_create("/home/logo.bmp", sample_bmp_buffer, sizeof(sample_bmp_buffer), KVFS_TYPE_FILE, 0644);

    kvfs_create("/bin/ls", bin_ls_help, sizeof(bin_ls_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/cd", bin_cd_help, sizeof(bin_cd_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/pwd", bin_pwd_help, sizeof(bin_pwd_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/cat", bin_cat_help, sizeof(bin_cat_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/view", bin_view_help, sizeof(bin_view_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/calc_gui", bin_calc_help, sizeof(bin_calc_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/snake", bin_snake_help, sizeof(bin_snake_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/beep", bin_beep_help, sizeof(bin_beep_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/write", bin_write_help, sizeof(bin_write_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/touch", bin_touch_help, sizeof(bin_touch_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/mkdir", bin_mkdir_help, sizeof(bin_mkdir_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/rm", bin_rm_help, sizeof(bin_rm_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/whoami", bin_whoami_help, sizeof(bin_whoami_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/su", bin_su_help, sizeof(bin_su_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/adduser", bin_adduser_help, sizeof(bin_adduser_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/mem", bin_mem_help, sizeof(bin_mem_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/clear", bin_clear_help, sizeof(bin_clear_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/ata", bin_ata_help, sizeof(bin_ata_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/format_disk", bin_format_help, sizeof(bin_format_help) - 1, KVFS_TYPE_BIN, 0755);
    kvfs_create("/bin/exit", bin_exit_help, sizeof(bin_exit_help) - 1, KVFS_TYPE_BIN, 0755);

    kvfs_create("/etc/hostname", etc_hostname, sizeof(etc_hostname) - 1, KVFS_TYPE_FILE, 0644);
    kvfs_create("/etc/os-release", etc_os_release, sizeof(etc_os_release) - 1, KVFS_TYPE_FILE, 0644);
    kvfs_create("/etc/passwd", etc_passwd_buf, sizeof(etc_passwd_buf) - 1, KVFS_TYPE_FILE, 0644);
    kvfs_create("/etc/kernel.config", etc_kernel_config, sizeof(etc_kernel_config) - 1, KVFS_TYPE_FILE, 0644);
    kvfs_create("/etc/motd", etc_motd, sizeof(etc_motd) - 1, KVFS_TYPE_FILE, 0644);

    kvfs_create("/dev/null", dev_null_info, sizeof(dev_null_info) - 1, KVFS_TYPE_DEV, 0666);
    kvfs_create("/dev/zero", dev_zero_info, sizeof(dev_zero_info) - 1, KVFS_TYPE_DEV, 0666);
    kvfs_create("/dev/fb0", dev_fb0_info, sizeof(dev_fb0_info) - 1, KVFS_TYPE_DEV, 0660);
    kvfs_create("/dev/ps2kbd", dev_ps2kbd_info, sizeof(dev_ps2kbd_info) - 1, KVFS_TYPE_DEV, 0660);
    kvfs_create("/dev/ps2mouse", dev_ps2mouse_info, sizeof(dev_ps2mouse_info) - 1, KVFS_TYPE_DEV, 0660);
    kvfs_create("/dev/ata0", dev_ata0_info, sizeof(dev_ata0_info) - 1, KVFS_TYPE_DEV, 0660);
}

int kvfs_mkdir(const char *path) {
    return kvfs_create(path, NULL, 0, KVFS_TYPE_DIR, 0755);
}

int kvfs_create(const char *path, const void *data, size_t size, uint8_t type, uint16_t mode) {
    if (!path || vfs_node_count >= KVFS_MAX_NODES) return -1;

    char clean_path[KVFS_MAX_PATH];
    sanitize_path(path, clean_path, sizeof(clean_path));

    for (size_t i = 0; i < KVFS_MAX_NODES; i++) {
        if (vfs_table[i].is_used && kstrcmp(vfs_table[i].path, clean_path) == 0) {
            if (vfs_table[i].is_dynamic && vfs_table[i].data) kfree((void *)vfs_table[i].data);
            vfs_table[i].data = (const uint8_t *)data;
            vfs_table[i].size = size;
            vfs_table[i].type = type;
            vfs_table[i].mode = mode;
            vfs_table[i].is_dynamic = 0;
            return 0;
        }
    }

    for (size_t i = 0; i < KVFS_MAX_NODES; i++) {
        if (!vfs_table[i].is_used) {
            kstrncpy(vfs_table[i].path, clean_path, sizeof(vfs_table[i].path) - 1);
            vfs_table[i].data = (const uint8_t *)data;
            vfs_table[i].size = size;
            vfs_table[i].type = type;
            vfs_table[i].mode = mode;
            vfs_table[i].is_used = 1;
            vfs_table[i].is_dynamic = 0;
            vfs_node_count++;
            return 0;
        }
    }
    return -1;
}

int kvfs_write(const char *path, const void *data, size_t size) {
    char clean_path[KVFS_MAX_PATH];
    sanitize_path(path, clean_path, sizeof(clean_path));

    uint8_t *heap_copy = kmalloc(size + 1);
    if (!heap_copy) return -1;
    if (data) kmemcpy(heap_copy, data, size);
    heap_copy[size] = '\0';

    for (size_t i = 0; i < KVFS_MAX_NODES; i++) {
        if (vfs_table[i].is_used && kstrcmp(vfs_table[i].path, clean_path) == 0) {
            if (vfs_table[i].is_dynamic && vfs_table[i].data) kfree((void *)vfs_table[i].data);
            vfs_table[i].data = heap_copy;
            vfs_table[i].size = size;
            vfs_table[i].is_dynamic = 1;
            return 0;
        }
    }

    for (size_t i = 0; i < KVFS_MAX_NODES; i++) {
        if (!vfs_table[i].is_used) {
            kstrncpy(vfs_table[i].path, clean_path, sizeof(vfs_table[i].path) - 1);
            vfs_table[i].data = heap_copy;
            vfs_table[i].size = size;
            vfs_table[i].type = KVFS_TYPE_FILE;
            vfs_table[i].mode = 0644;
            vfs_table[i].is_used = 1;
            vfs_table[i].is_dynamic = 1;
            vfs_node_count++;
            return 0;
        }
    }

    kfree(heap_copy);
    return -1;
}

int kvfs_delete(const char *path) {
    char clean_path[KVFS_MAX_PATH];
    sanitize_path(path, clean_path, sizeof(clean_path));

    for (size_t i = 0; i < KVFS_MAX_NODES; i++) {
        if (vfs_table[i].is_used && kstrcmp(vfs_table[i].path, clean_path) == 0) {
            if (vfs_table[i].is_dynamic && vfs_table[i].data) {
                kfree((void *)vfs_table[i].data);
            }
            vfs_table[i].is_used = 0;
            vfs_table[i].path[0] = '\0';
            vfs_table[i].data = NULL;
            if (vfs_node_count > 0) vfs_node_count--;
            return 0;
        }
    }
    return -1;
}

const kvfs_node_t *kvfs_open(const char *path) {
    if (!path) return NULL;
    char clean_path[KVFS_MAX_PATH];
    sanitize_path(path, clean_path, sizeof(clean_path));

    for (size_t i = 0; i < KVFS_MAX_NODES; i++) {
        if (vfs_table[i].is_used && kstrcmp(vfs_table[i].path, clean_path) == 0) {
            return &vfs_table[i];
        }
    }
    return NULL;
}

int kvfs_read(const kvfs_node_t *node, void *buf, size_t offset, size_t count) {
    if (!node || !buf || !node->data || offset >= node->size) return 0;
    size_t to_copy = count;
    if (offset + to_copy > node->size) to_copy = node->size - offset;
    kmemcpy(buf, node->data + offset, to_copy);
    return (int)to_copy;
}

void kvfs_list_dir(const char *dir_path, kvfs_ls_callback_t callback) {
    if (!callback) return;
    char target_dir[KVFS_MAX_PATH];
    sanitize_path(dir_path ? dir_path : "/", target_dir, sizeof(target_dir));

    size_t dir_len = kstrlen(target_dir);
    int is_root = (kstrcmp(target_dir, "/") == 0);

    for (size_t i = 0; i < KVFS_MAX_NODES; i++) {
        if (!vfs_table[i].is_used) continue;
        if (kstrcmp(vfs_table[i].path, target_dir) == 0) continue;

        const char *p = vfs_table[i].path;
        if (is_root) {
            if (p[0] == '/' && p[1] != '\0') {
                const char *sub = kstrchr(p + 1, '/');
                if (!sub) {
                    callback(p + 1, vfs_table[i].size, vfs_table[i].type, vfs_table[i].mode);
                } else if (vfs_table[i].type == KVFS_TYPE_DIR && sub[1] == '\0') {
                    char dname[32];
                    size_t dlen = sub - (p + 1);
                    if (dlen < sizeof(dname)) {
                        kstrncpy(dname, p + 1, dlen);
                        dname[dlen] = '\0';
                        callback(dname, vfs_table[i].size, vfs_table[i].type, vfs_table[i].mode);
                    }
                }
            }
        } else {
            if (kstrncmp(p, target_dir, dir_len) == 0 && p[dir_len] == '/') {
                const char *item_name = p + dir_len + 1;
                const char *next_slash = kstrchr(item_name, '/');
                if (!next_slash) {
                    callback(item_name, vfs_table[i].size, vfs_table[i].type, vfs_table[i].mode);
                }
            }
        }
    }
}
