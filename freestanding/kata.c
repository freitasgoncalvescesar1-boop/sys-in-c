#ifndef KATA_C_INCLUDED
#define KATA_C_INCLUDED

#include "kata.h"
#include "kstring.h"

#if defined(__x86_64__) || defined(__i386__)
static inline void ata_outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__ ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t ata_inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__ ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void ata_insw(uint16_t port, void *addr, uint32_t count) {
    __asm__ __volatile__ ("cld; rep insw" : "+D"(addr), "+c"(count) : "d"(port) : "memory");
}
static inline void ata_outsw(uint16_t port, const void *addr, uint32_t count) {
    __asm__ __volatile__ ("cld; rep outsw" : "+S"(addr), "+c"(count) : "d"(port) : "memory");
}
#else
static inline void ata_outb(uint16_t port, uint8_t val) { (void)port; (void)val; }
static inline uint8_t ata_inb(uint16_t port) { (void)port; return 0x40; }
static inline void ata_insw(uint16_t port, void *addr, uint32_t count) { (void)port; (void)addr; (void)count; }
static inline void ata_outsw(uint16_t port, const void *addr, uint32_t count) { (void)port; (void)addr; (void)count; }
#endif

static kata_drive_info_t primary_master = {0};

static void kata_wait_400ns(void) {
    ata_inb(ATA_PRIMARY_CTRL_BASE);
    ata_inb(ATA_PRIMARY_CTRL_BASE);
    ata_inb(ATA_PRIMARY_CTRL_BASE);
    ata_inb(ATA_PRIMARY_CTRL_BASE);
}

static int kata_wait_ready(void) {
    kata_wait_400ns();
    int timeout = 100000;
    while (timeout--) {
        uint8_t status = ata_inb(ATA_PRIMARY_IO_BASE + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) {
            if (status & ATA_SR_ERR) return -1;
            return 0;
        }
    }
    return -1;
}

static int kata_wait_drq(void) {
    kata_wait_400ns();
    int timeout = 100000;
    while (timeout--) {
        uint8_t status = ata_inb(ATA_PRIMARY_IO_BASE + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) return -1;
        if ((status & ATA_SR_DRQ) && !(status & ATA_SR_BSY)) return 0;
    }
    return -1;
}

int kata_init(void) {
    primary_master.drive_present = 0;

    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_DRIVE_HEAD, 0xA0);
    kata_wait_400ns();

    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_SEC_COUNT, 0);
    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_LBA_LOW, 0);
    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_LBA_MID, 0);
    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_LBA_HIGH, 0);

    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    kata_wait_400ns();

    uint8_t status = ata_inb(ATA_PRIMARY_IO_BASE + ATA_REG_STATUS);
    if (status == 0) return -1;

    while (status & ATA_SR_BSY) {
        status = ata_inb(ATA_PRIMARY_IO_BASE + ATA_REG_STATUS);
    }

    uint8_t mid = ata_inb(ATA_PRIMARY_IO_BASE + ATA_REG_LBA_MID);
    uint8_t high = ata_inb(ATA_PRIMARY_IO_BASE + ATA_REG_LBA_HIGH);
    if (mid != 0 || high != 0) return -1;

    if (kata_wait_drq() < 0) return -1;

    uint16_t id_buf[256];
    ata_insw(ATA_PRIMARY_IO_BASE + ATA_REG_DATA, id_buf, 256);

    primary_master.drive_present = 1;
    primary_master.total_sectors = ((uint32_t)id_buf[61] << 16) | id_buf[60];
    primary_master.size_mb = (primary_master.total_sectors / 2048);

    int idx = 0;
    for (int i = 27; i <= 46; i++) {
        primary_master.model[idx++] = (char)(id_buf[i] >> 8);
        primary_master.model[idx++] = (char)(id_buf[i] & 0xFF);
    }
    primary_master.model[idx] = '\0';

    idx = 0;
    for (int i = 10; i <= 19; i++) {
        primary_master.serial[idx++] = (char)(id_buf[i] >> 8);
        primary_master.serial[idx++] = (char)(id_buf[i] & 0xFF);
    }
    primary_master.serial[idx] = '\0';

    return 0;
}

const kata_drive_info_t *kata_get_info(void) {
    return &primary_master;
}

int kata_read_sector(uint32_t lba, uint8_t *buffer) {
    if (!buffer) return -1;
    if (kata_wait_ready() < 0) return -1;

    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_SEC_COUNT, 1);
    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_LBA_LOW, (uint8_t)lba);
    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    if (kata_wait_drq() < 0) return -1;

    ata_insw(ATA_PRIMARY_IO_BASE + ATA_REG_DATA, buffer, 256);
    return 0;
}

int kata_write_sector(uint32_t lba, const uint8_t *buffer) {
    if (!buffer) return -1;
    if (kata_wait_ready() < 0) return -1;

    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_SEC_COUNT, 1);
    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_LBA_LOW, (uint8_t)lba);
    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    if (kata_wait_drq() < 0) return -1;

    ata_outsw(ATA_PRIMARY_IO_BASE + ATA_REG_DATA, buffer, 256);
    ata_outb(ATA_PRIMARY_IO_BASE + ATA_REG_COMMAND, 0xE7);
    kata_wait_ready();
    return 0;
}

int kata_read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer) {
    for (uint8_t i = 0; i < count; i++) {
        if (kata_read_sector(lba + i, buffer + (i * ATA_SECTOR_SIZE)) < 0) return -1;
    }
    return 0;
}

#endif
