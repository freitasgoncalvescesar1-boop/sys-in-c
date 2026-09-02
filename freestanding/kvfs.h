#ifndef KVFS_H
#define KVFS_H

#include <stddef.h>
#include <stdint.h>

#define KVFS_MAX_NODES 64
#define KVFS_MAX_PATH  128

typedef enum {
    KVFS_TYPE_FILE = 0,
    KVFS_TYPE_DIR,
    KVFS_TYPE_BIN,
    KVFS_TYPE_DEV
} kvfs_node_type_t;

typedef struct {
    char path[KVFS_MAX_PATH];
    const uint8_t *data;
    size_t size;
    uint8_t type;
    uint16_t mode;
    int is_used;
    int is_dynamic; /* 1 se alocado via kmalloc */
} kvfs_node_t;

typedef void (*kvfs_ls_callback_t)(const char *name, size_t size, uint8_t type, uint16_t mode);

void kvfs_init(void);
int kvfs_mkdir(const char *path);
int kvfs_create(const char *path, const void *data, size_t size, uint8_t type, uint16_t mode);
int kvfs_write(const char *path, const void *data, size_t size);
int kvfs_delete(const char *path);
const kvfs_node_t *kvfs_open(const char *path);
int kvfs_read(const kvfs_node_t *node, void *buf, size_t offset, size_t count);
void kvfs_list_dir(const char *dir_path, kvfs_ls_callback_t callback);

#endif
