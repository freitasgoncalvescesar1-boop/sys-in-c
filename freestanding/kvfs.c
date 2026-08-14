#include "kvfs.h"
#include "kstring.h"

/* VFS file table storage */
static kvfs_file_t vfs_table[KVFS_MAX_FILES];
static size_t vfs_file_count = 0;

/* VFS initialization */
void kvfs_init(void) {
    for (size_t i = 0; i < KVFS_MAX_FILES; i++) {
        vfs_table[i].is_used = 0;
        vfs_table[i].name[0] = '\0';
        vfs_table[i].data = NULL;
        vfs_table[i].size = 0;
        vfs_table[i].mode = 0;
    }
    vfs_file_count = 0;
}

/* Virtual file registration */
int kvfs_create_file(const char *name, const void *data, size_t size, uint16_t mode) {
    if (!name || vfs_file_count >= KVFS_MAX_FILES) return -1;

    for (size_t i = 0; i < KVFS_MAX_FILES; i++) {
        if (!vfs_table[i].is_used) {
            kstrncpy(vfs_table[i].name, name, sizeof(vfs_table[i].name) - 1);
            vfs_table[i].data = (const uint8_t *)data;
            vfs_table[i].size = size;
            vfs_table[i].mode = mode;
            vfs_table[i].is_used = 1;
            vfs_file_count++;
            return 0;
        }
    }
    return -1;
}

/* Virtual file lookup */
const kvfs_file_t *kvfs_open(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < KVFS_MAX_FILES; i++) {
        if (vfs_table[i].is_used && kstrcmp(vfs_table[i].name, name) == 0) {
            return &vfs_table[i];
        }
    }
    return NULL;
}

/* Directory listing traversal */
void kvfs_list(kvfs_ls_callback_t callback) {
    if (!callback) return;
    for (size_t i = 0; i < KVFS_MAX_FILES; i++) {
        if (vfs_table[i].is_used) {
            callback(vfs_table[i].name, vfs_table[i].size, vfs_table[i].mode);
        }
    }
}
