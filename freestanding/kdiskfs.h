#ifndef KDISKFS_H
#define KDISKFS_H

#include <stdint.h>
#include <stddef.h>

#define KDISKFS_MAGIC 0x5346534B /* "KSFS" */
#define KDISKFS_MAX_FILES 32

/* Superbloco no LBA 1 */
typedef struct {
    uint32_t magic;
    uint32_t num_files;
    uint32_t next_free_lba;
    uint32_t block_size;
} __attribute__((packed)) kdiskfs_super_t;

/* Entrada de Arquivo nos LBAs 2 a 5 */
typedef struct {
    char path[64];
    uint32_t size;
    uint32_t start_lba;
    uint32_t sector_count;
    uint8_t type;
    uint8_t is_used;
    uint16_t mode;
} __attribute__((packed)) kdiskfs_entry_t;

/* Funções do Sistema de Arquivos em Disco */
int kdiskfs_format(void);
int kdiskfs_mount(void);
int kdiskfs_save_file(const char *path, const void *data, size_t size, uint8_t type, uint16_t mode);
int kdiskfs_delete_file(const char *path);

#endif
