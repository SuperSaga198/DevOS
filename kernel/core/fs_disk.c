#include "fs_disk.h"

#include "ata_pio.h"
#include "fs.h"

#include <stddef.h>
#include <stdint.h>

#define FS_DISK_SECTOR_SIZE 512U
#define FS_DISK_HEADER_VERSION 1U
#define FS_DISK_HEADER_MAGIC "DEVOSFS1"
#define FS_DISK_HEADER_MAGIC_SIZE 8U
#define FS_DISK_HEADER_VERSION_OFFSET 8U
#define FS_DISK_HEADER_SEQUENCE_OFFSET 12U
#define FS_DISK_HEADER_SIZE_OFFSET 20U
#define FS_DISK_HEADER_CHECKSUM_OFFSET 24U
#define FS_DISK_HEADER_HEADER_CHECKSUM_OFFSET 28U
#define FS_DISK_HEADER_DATA_OFFSET 32U

struct fs_disk_candidate {
    uint64_t sequence;
    uint32_t checksum;
    uint32_t slot;
    uint8_t valid;
};

static uint8_t snapshot[FS_SNAPSHOT_SIZE];
static uint8_t sector[FS_DISK_SECTOR_SIZE];
static uint64_t active_sequence;
static int32_t active_slot = -1;
static uint8_t disk_ready;

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static uint64_t read_u64(const uint8_t *bytes)
{
    return (uint64_t)read_u32(bytes) |
        ((uint64_t)read_u32(bytes + 4) << 32);
}

static int bytes_equal(const uint8_t *left, const char *right, size_t size)
{
    for (size_t index = 0; index < size; ++index) {
        if (left[index] != (uint8_t)right[index]) {
            return 0;
        }
    }
    return 1;
}

static void write_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void write_u64(uint8_t *bytes, uint64_t value)
{
    write_u32(bytes, (uint32_t)value);
    write_u32(bytes + 4, (uint32_t)(value >> 32));
}

static uint32_t checksum(const uint8_t *bytes, size_t size)
{
    uint32_t value = 0xffffffffU;
    for (size_t index = 0; index < size; ++index) {
        value ^= bytes[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint32_t mask = 0U - (value & 1U);
            value = (value >> 1) ^ (0xedb88320U & mask);
        }
    }
    return ~value;
}

static uint32_t slot_start(uint32_t slot)
{
    return FS_DISK_FIRST_LBA + slot * FS_DISK_SLOT_SECTORS;
}

static int read_candidate(uint32_t slot, struct fs_disk_candidate *candidate,
                          uint8_t *blank)
{
    const uint32_t header_lba = slot_start(slot);
    if (!ata_pio_read_sector(header_lba, sector)) {
        return 0;
    }

    uint8_t nonzero = 0;
    for (size_t index = 0; index < sizeof(sector); ++index) {
        nonzero |= sector[index];
    }
    if (nonzero != 0) {
        *blank = 0;
    }
    if (nonzero == 0 ||
        !bytes_equal(sector, FS_DISK_HEADER_MAGIC,
                     FS_DISK_HEADER_MAGIC_SIZE) ||
        read_u32(sector + FS_DISK_HEADER_VERSION_OFFSET) !=
            FS_DISK_HEADER_VERSION ||
        read_u32(sector + FS_DISK_HEADER_SIZE_OFFSET) != FS_SNAPSHOT_SIZE ||
        read_u32(sector + FS_DISK_HEADER_DATA_OFFSET) !=
            FS_DISK_PAYLOAD_SECTORS ||
        read_u32(sector + FS_DISK_HEADER_HEADER_CHECKSUM_OFFSET) !=
            checksum(sector, FS_DISK_HEADER_HEADER_CHECKSUM_OFFSET)) {
        return 1;
    }

    const uint64_t sequence =
        read_u64(sector + FS_DISK_HEADER_SEQUENCE_OFFSET);
    if (sequence == 0) {
        return 1;
    }
    candidate->slot = slot;
    candidate->sequence = sequence;
    candidate->checksum =
        read_u32(sector + FS_DISK_HEADER_CHECKSUM_OFFSET);
    candidate->valid = 1;
    return 1;
}

static int read_snapshot(const struct fs_disk_candidate *candidate)
{
    size_t offset = 0;
    const uint32_t first_payload_lba = slot_start(candidate->slot) + 1;
    for (uint32_t sector_index = 0;
         sector_index < FS_DISK_PAYLOAD_SECTORS; ++sector_index) {
        if (!ata_pio_read_sector(first_payload_lba + sector_index, sector)) {
            return 0;
        }
        size_t copy_size = FS_SNAPSHOT_SIZE - offset;
        if (copy_size > sizeof(sector)) {
            copy_size = sizeof(sector);
        }
        for (size_t byte = 0; byte < copy_size; ++byte) {
            snapshot[offset + byte] = sector[byte];
        }
        offset += copy_size;
    }
    return offset == FS_SNAPSHOT_SIZE &&
        checksum(snapshot, sizeof(snapshot)) == candidate->checksum &&
        fs_snapshot_import(snapshot, sizeof(snapshot));
}

int fs_disk_load(void)
{
    disk_ready = 0;
    active_slot = -1;
    active_sequence = 0;
    if (ata_pio_sector_count() < FS_DISK_REQUIRED_SECTORS) {
        return FS_DISK_LOAD_ERROR;
    }

    struct fs_disk_candidate candidates[2] = { 0 };
    uint8_t blank = 1;
    for (uint32_t slot = 0; slot < 2; ++slot) {
        candidates[slot].slot = slot;
        if (!read_candidate(slot, &candidates[slot], &blank)) {
            return FS_DISK_LOAD_ERROR;
        }
    }

    int32_t newest = -1;
    if (candidates[0].valid && candidates[1].valid) {
        newest = candidates[1].sequence > candidates[0].sequence ? 1 : 0;
    } else if (candidates[0].valid) {
        newest = 0;
    } else if (candidates[1].valid) {
        newest = 1;
    }

    if (newest >= 0) {
        const int32_t older = newest ^ 1;
        int32_t loaded_slot = -1;
        if (read_snapshot(&candidates[newest])) {
            loaded_slot = newest;
        } else if (candidates[older].valid &&
                   read_snapshot(&candidates[older])) {
            loaded_slot = older;
        }
        if (loaded_slot >= 0) {
            active_slot = loaded_slot;
            active_sequence = candidates[loaded_slot].sequence;
            disk_ready = 1;
            return FS_DISK_LOAD_OK;
        }
        return FS_DISK_LOAD_ERROR;
    }

    if (blank) {
        disk_ready = 1;
        return FS_DISK_LOAD_EMPTY;
    }
    return FS_DISK_LOAD_ERROR;
}

int fs_disk_save(void)
{
    if (!disk_ready || ata_pio_sector_count() < FS_DISK_REQUIRED_SECTORS ||
        !fs_snapshot_export(snapshot, sizeof(snapshot))) {
        return 0;
    }

    const uint32_t target_slot =
        active_slot < 0 ? 0U : (uint32_t)(active_slot ^ 1);
    const uint32_t first_payload_lba = slot_start(target_slot) + 1;
    size_t offset = 0;
    for (uint32_t sector_index = 0;
         sector_index < FS_DISK_PAYLOAD_SECTORS; ++sector_index) {
        for (size_t byte = 0; byte < sizeof(sector); ++byte) {
            sector[byte] = 0;
        }
        size_t copy_size = FS_SNAPSHOT_SIZE - offset;
        if (copy_size > sizeof(sector)) {
            copy_size = sizeof(sector);
        }
        for (size_t byte = 0; byte < copy_size; ++byte) {
            sector[byte] = snapshot[offset + byte];
        }
        if (!ata_pio_write_sector(first_payload_lba + sector_index, sector)) {
            return 0;
        }
        offset += copy_size;
    }
    if (offset != FS_SNAPSHOT_SIZE) {
        return 0;
    }

    for (size_t byte = 0; byte < sizeof(sector); ++byte) {
        sector[byte] = 0;
    }
    for (size_t byte = 0; byte < FS_DISK_HEADER_MAGIC_SIZE; ++byte) {
        sector[byte] = (uint8_t)FS_DISK_HEADER_MAGIC[byte];
    }
    const uint64_t sequence = active_sequence + 1;
    if (sequence == 0) {
        return 0;
    }
    write_u32(sector + FS_DISK_HEADER_VERSION_OFFSET,
              FS_DISK_HEADER_VERSION);
    write_u64(sector + FS_DISK_HEADER_SEQUENCE_OFFSET, sequence);
    write_u32(sector + FS_DISK_HEADER_SIZE_OFFSET, FS_SNAPSHOT_SIZE);
    write_u32(sector + FS_DISK_HEADER_CHECKSUM_OFFSET,
              checksum(snapshot, sizeof(snapshot)));
    write_u32(sector + FS_DISK_HEADER_DATA_OFFSET,
              FS_DISK_PAYLOAD_SECTORS);
    write_u32(sector + FS_DISK_HEADER_HEADER_CHECKSUM_OFFSET,
              checksum(sector, FS_DISK_HEADER_HEADER_CHECKSUM_OFFSET));
    if (!ata_pio_write_sector(slot_start(target_slot), sector)) {
        return 0;
    }

    active_slot = (int32_t)target_slot;
    active_sequence = sequence;
    return 1;
}
