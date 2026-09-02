#ifndef KATA_H
#define KATA_H

#include <stdint.h>
#include <stddef.h>

#define ATA_SECTOR_SIZE 512

/* Portas do Barramento Primário ATA (Master) */
#define ATA_PRIMARY_IO_BASE    0x1F0
#define ATA_PRIMARY_CTRL_BASE  0x3F6

#define ATA_REG_DATA          0x00
#define ATA_REG_ERROR         0x01
#define ATA_REG_FEATURES      0x01
#define ATA_REG_SEC_COUNT     0x02
#define ATA_REG_LBA_LOW       0x03
#define ATA_REG_LBA_MID       0x04
#define ATA_REG_LBA_HIGH      0x05
#define ATA_REG_DRIVE_HEAD    0x06
#define ATA_REG_STATUS        0x07
#define ATA_REG_COMMAND       0x07

/* Comandos ATA */
#define ATA_CMD_READ_PIO      0x20
#define ATA_CMD_WRITE_PIO     0x30
#define ATA_CMD_IDENTIFY      0xEC

/* Status Bits */
#define ATA_SR_ERR            0x01
#define ATA_SR_DRQ            0x08
#define ATA_SR_SRV            0x10
#define ATA_SR_DF             0x20
#define ATA_SR_RDY            0x40
#define ATA_SR_BSY            0x80

typedef struct {
    int drive_present;
    char model[41];
    char serial[21];
    uint32_t total_sectors;
    uint32_t size_mb;
} kata_drive_info_t;

/* Inicialização e Detecção */
int kata_init(void);
const kata_drive_info_t *kata_get_info(void);

/* Leitura e Escrita de Setores LBA28 */
int kata_read_sector(uint32_t lba, uint8_t *buffer);
int kata_write_sector(uint32_t lba, const uint8_t *buffer);
int kata_read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer);

#endif
