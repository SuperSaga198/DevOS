#include "ata_pio.h"

#include <stddef.h>
#include <stdint.h>

#define ATA_DATA 0x1f0
#define ATA_ERROR 0x1f1
#define ATA_SECTOR_COUNT 0x1f2
#define ATA_LBA_LOW 0x1f3
#define ATA_LBA_MID 0x1f4
#define ATA_LBA_HIGH 0x1f5
#define ATA_DRIVE 0x1f6
#define ATA_STATUS 0x1f7
#define ATA_COMMAND 0x1f7
#define ATA_CONTROL 0x3f6

#define ATA_STATUS_ERROR 0x01
#define ATA_STATUS_DEVICE_FAULT 0x20
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_BUSY 0x80
#define ATA_COMMAND_IDENTIFY 0xec
#define ATA_COMMAND_READ_SECTORS 0x20
#define ATA_COMMAND_WRITE_SECTORS 0x30
#define ATA_COMMAND_CACHE_FLUSH 0xe7
#define ATA_LBA28_MAX 0x0fffffffU
#define ATA_POLL_LIMIT 1000000U

static uint32_t sectors;
static uint8_t initialized;

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint8_t wait_status(uint8_t require_data)
{
    for (uint32_t attempt = 0; attempt < ATA_POLL_LIMIT; ++attempt) {
        const uint8_t status = inb(ATA_STATUS);
        if (status == 0 || status == 0xff) {
            return 0;
        }
        if ((status & ATA_STATUS_BUSY) != 0) {
            continue;
        }
        if ((status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) != 0) {
            (void)inb(ATA_ERROR);
            return 0;
        }
        if (!require_data || (status & ATA_STATUS_DRQ) != 0) {
            return status;
        }
    }
    return 0;
}

static void transfer_words(const void *source, void *destination,
                           uint8_t write)
{
    for (size_t index = 0; index < 256; ++index) {
        if (write) {
            const uint8_t *bytes = source;
            const uint16_t word = (uint16_t)bytes[index * 2] |
                (uint16_t)((uint16_t)bytes[index * 2 + 1] << 8);
            outw(ATA_DATA, word);
        } else {
            uint8_t *bytes = destination;
            const uint16_t word = inw(ATA_DATA);
            bytes[index * 2] = (uint8_t)word;
            bytes[index * 2 + 1] = (uint8_t)(word >> 8);
        }
    }
}

static int transfer_sector(uint32_t lba, const void *source,
                           void *destination, uint8_t write)
{
    if (!initialized || (write ? source == NULL : destination == NULL) ||
        lba >= sectors ||
        lba > ATA_LBA28_MAX) {
        return 0;
    }

    if (wait_status(0) == 0) {
        return 0;
    }
    outb(ATA_DRIVE, (uint8_t)(0xe0U | ((lba >> 24) & 0x0fU)));
    (void)inb(ATA_CONTROL);
    outb(ATA_SECTOR_COUNT, 1);
    outb(ATA_LBA_LOW, (uint8_t)lba);
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND, write ? ATA_COMMAND_WRITE_SECTORS :
         ATA_COMMAND_READ_SECTORS);

    if (wait_status(1) == 0) {
        return 0;
    }
    transfer_words(source, destination, write);
    if (wait_status(0) == 0) {
        return 0;
    }
    if (write) {
        outb(ATA_COMMAND, ATA_COMMAND_CACHE_FLUSH);
        if (wait_status(0) == 0) {
            return 0;
        }
    }
    return 1;
}

int ata_pio_init(void)
{
    initialized = 0;
    sectors = 0;

    if (inb(ATA_STATUS) == 0xff) {
        return 0;
    }
    outb(ATA_DRIVE, 0xa0);
    (void)inb(ATA_CONTROL);
    outb(ATA_SECTOR_COUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, ATA_COMMAND_IDENTIFY);
    if (inb(ATA_STATUS) == 0 || wait_status(1) == 0 ||
        inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HIGH) != 0) {
        return 0;
    }

    uint32_t identified_sectors = 0;
    for (size_t index = 0; index < 256; ++index) {
        const uint16_t word = inw(ATA_DATA);
        if (index == 60) {
            identified_sectors = word;
        } else if (index == 61) {
            identified_sectors |= (uint32_t)word << 16;
        }
    }
    if (wait_status(0) == 0 || identified_sectors == 0) {
        return 0;
    }

    sectors = identified_sectors > ATA_LBA28_MAX + 1U ?
        ATA_LBA28_MAX + 1U : identified_sectors;
    initialized = 1;
    return 1;
}

uint32_t ata_pio_sector_count(void)
{
    return initialized ? sectors : 0;
}

int ata_pio_read_sector(uint32_t lba, void *buffer)
{
    return transfer_sector(lba, NULL, buffer, 0);
}

int ata_pio_write_sector(uint32_t lba, const void *buffer)
{
    return transfer_sector(lba, buffer, NULL, 1);
}
