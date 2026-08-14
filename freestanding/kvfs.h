#ifndef KVFS_H
#define KVFS_H

#include <stddef.h>
#include <stdint.h>

#define KVFS_MAX_FILES 32

/* Virtual file descriptor */
typedef struct {
    char name[64];
    const uint8_t *data;
    size_t size;
    uint16_t mode;
    int is_used;
} kvfs_file_t;

/* Directory listing callback */
typedef void (*kvfs_ls_callback_t)(const char *name, size_t size, uint16_t mode);

/* VFS initialization */
void kvfs_init(void);

/* Virtual file registration */
int kvfs_create_file(const char *name, const void *data, size_t size, uint16_t mode);

/* Virtual file lookup */
const kvfs_file_t *kvfs_open(const char *name);

/* Directory listing traversal */
void kvfs_list(kvfs_ls_callback_t callback);

#endif
