#ifndef DEVOS_ATA_PIO_H
#define DEVOS_ATA_PIO_H

#include <stdint.h>

int ata_pio_init(void);
uint32_t ata_pio_sector_count(void);
int ata_pio_read_sector(uint32_t lba, void *buffer);
int ata_pio_write_sector(uint32_t lba, const void *buffer);

#endif
