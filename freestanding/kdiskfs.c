#ifndef KDISKFS_C_INCLUDED
#define KDISKFS_C_INCLUDED

#include "kdiskfs.h"
#include "kata.h"
#include "kvfs.h"
#include "kmem.h"
#include "kstring.h"
#include "kprintf.h"

#define KDISKFS_SUPER_LBA   1
#define KDISKFS_TABLE_LBA   2
#define KDISKFS_DATA_LBA    6

static kdiskfs_super_t super_block = {0};
static kdiskfs_entry_t disk_entries[KDISKFS_MAX_FILES] = {0};

int kdiskfs_format(void) {
    const kata_drive_info_t *d = kata_get_info();
    if (!d || !d->drive_present) return -1;

    super_block.magic = KDISKFS_MAGIC;
    super_block.num_files = 0;
    super_block.next_free_lba = KDISKFS_DATA_LBA;
    super_block.block_size = ATA_SECTOR_SIZE;

    uint8_t sector_buf[ATA_SECTOR_SIZE];
    kmemset(sector_buf, 0, sizeof(sector_buf));
    kmemcpy(sector_buf, &super_block, sizeof(super_block));

    if (kata_write_sector(KDISKFS_SUPER_LBA, sector_buf) < 0) return -1;

    kmemset(disk_entries, 0, sizeof(disk_entries));
    for (int i = 0; i < 4; i++) {
        kmemcpy(sector_buf, ((uint8_t *)disk_entries) + (i * ATA_SECTOR_SIZE), ATA_SECTOR_SIZE);
        kata_write_sector(KDISKFS_TABLE_LBA + i, sector_buf);
    }

    return 0;
}

int kdiskfs_mount(void) {
    const kata_drive_info_t *d = kata_get_info();
    if (!d || !d->drive_present) return -1;

    uint8_t sector_buf[ATA_SECTOR_SIZE];
    if (kata_read_sector(KDISKFS_SUPER_LBA, sector_buf) < 0) return -1;

    kmemcpy(&super_block, sector_buf, sizeof(super_block));
    if (super_block.magic != KDISKFS_MAGIC) {
        // Formata apenas na primeira vez que o disco virgem for inserido
        return kdiskfs_format();
    }

    for (int i = 0; i < 4; i++) {
        kata_read_sector(KDISKFS_TABLE_LBA + i, sector_buf);
        kmemcpy(((uint8_t *)disk_entries) + (i * ATA_SECTOR_SIZE), sector_buf, ATA_SECTOR_SIZE);
    }

    // Carrega todos os arquivos persistidos do disco para o VFS
    for (int i = 0; i < KDISKFS_MAX_FILES; i++) {
        if (disk_entries[i].is_used) {
            if (disk_entries[i].type == KVFS_TYPE_DIR) {
                kvfs_mkdir(disk_entries[i].path);
            } else if (disk_entries[i].size > 0) {
                uint8_t *file_data = kmalloc(disk_entries[i].sector_count * ATA_SECTOR_SIZE + 1);
                if (file_data) {
                    for (uint32_t s = 0; s < disk_entries[i].sector_count; s++) {
                        kata_read_sector(disk_entries[i].start_lba + s, file_data + (s * ATA_SECTOR_SIZE));
                    }
                    file_data[disk_entries[i].size] = '\0';
                    kvfs_write(disk_entries[i].path, file_data, disk_entries[i].size);
                }
            } else {
                kvfs_create(disk_entries[i].path, "", 0, disk_entries[i].type, disk_entries[i].mode);
            }
        }
    }

    return 0;
}

static void sync_table_to_disk(void) {
    uint8_t sector_buf[ATA_SECTOR_SIZE];
    for (int i = 0; i < 4; i++) {
        kmemcpy(sector_buf, ((uint8_t *)disk_entries) + (i * ATA_SECTOR_SIZE), ATA_SECTOR_SIZE);
        kata_write_sector(KDISKFS_TABLE_LBA + i, sector_buf);
    }

    kmemset(sector_buf, 0, sizeof(sector_buf));
    kmemcpy(sector_buf, &super_block, sizeof(super_block));
    kata_write_sector(KDISKFS_SUPER_LBA, sector_buf);
}

int kdiskfs_save_file(const char *path, const void *data, size_t size, uint8_t type, uint16_t mode) {
    const kata_drive_info_t *d = kata_get_info();
    if (!d || !d->drive_present) return -1;

    int entry_idx = -1;
    for (int i = 0; i < KDISKFS_MAX_FILES; i++) {
        if (disk_entries[i].is_used && kstrcmp(disk_entries[i].path, path) == 0) {
            entry_idx = i;
            break;
        }
    }

    if (entry_idx == -1) {
        for (int i = 0; i < KDISKFS_MAX_FILES; i++) {
            if (!disk_entries[i].is_used) {
                entry_idx = i;
                break;
            }
        }
    }

    if (entry_idx == -1) return -1;

    uint32_t sectors_needed = (size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
    if (sectors_needed == 0) sectors_needed = 1;

    uint32_t start_lba = disk_entries[entry_idx].is_used ? disk_entries[entry_idx].start_lba : super_block.next_free_lba;
    if (!disk_entries[entry_idx].is_used) {
        super_block.next_free_lba += sectors_needed;
        super_block.num_files++;
    }

    uint8_t sector_buf[ATA_SECTOR_SIZE];
    const uint8_t *src = (const uint8_t *)data;

    for (uint32_t s = 0; s < sectors_needed; s++) {
        kmemset(sector_buf, 0, sizeof(sector_buf));
        size_t bytes_to_copy = (size > s * ATA_SECTOR_SIZE) ? (size - (s * ATA_SECTOR_SIZE)) : 0;
        if (bytes_to_copy > ATA_SECTOR_SIZE) bytes_to_copy = ATA_SECTOR_SIZE;
        if (src && bytes_to_copy > 0) kmemcpy(sector_buf, src + (s * ATA_SECTOR_SIZE), bytes_to_copy);

        kata_write_sector(start_lba + s, sector_buf);
    }

    kstrncpy(disk_entries[entry_idx].path, path, sizeof(disk_entries[entry_idx].path) - 1);
    disk_entries[entry_idx].size = size;
    disk_entries[entry_idx].start_lba = start_lba;
    disk_entries[entry_idx].sector_count = sectors_needed;
    disk_entries[entry_idx].type = type;
    disk_entries[entry_idx].is_used = 1;
    disk_entries[entry_idx].mode = mode;

    sync_table_to_disk();
    return 0;
}

int kdiskfs_delete_file(const char *path) {
    for (int i = 0; i < KDISKFS_MAX_FILES; i++) {
        if (disk_entries[i].is_used && kstrcmp(disk_entries[i].path, path) == 0) {
            disk_entries[i].is_used = 0;
            if (super_block.num_files > 0) super_block.num_files--;
            sync_table_to_disk();
            return 0;
        }
    }
    return -1;
}

#endif
