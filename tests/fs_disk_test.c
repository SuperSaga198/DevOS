#include "../kernel/core/fs.h"
#include "../kernel/core/fs_disk.h"
#include "../kernel/drivers/ata_pio.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_DISK_SECTORS 1024

static uint8_t disk[TEST_DISK_SECTORS][512];
static uint32_t failed_write_lba = UINT32_MAX;

int ata_pio_init(void)
{
    return 1;
}

uint32_t ata_pio_sector_count(void)
{
    return TEST_DISK_SECTORS;
}

int ata_pio_read_sector(uint32_t lba, void *buffer)
{
    if (lba >= TEST_DISK_SECTORS || buffer == NULL) {
        return 0;
    }
    uint8_t *destination = buffer;
    for (size_t byte = 0; byte < sizeof(disk[lba]); ++byte) {
        destination[byte] = disk[lba][byte];
    }
    return 1;
}

int ata_pio_write_sector(uint32_t lba, const void *buffer)
{
    if (lba >= TEST_DISK_SECTORS || buffer == NULL ||
        lba == failed_write_lba) {
        return 0;
    }
    const uint8_t *source = buffer;
    for (size_t byte = 0; byte < sizeof(disk[lba]); ++byte) {
        disk[lba][byte] = source[byte];
    }
    return 1;
}

static void expect_file_contents(const char *expected)
{
    char buffer[32] = { 0 };
    size_t size = 0;
    assert(fs_read("/notes", buffer, sizeof(buffer), &size));
    size_t expected_size = 0;
    while (expected[expected_size] != '\0') {
        ++expected_size;
    }
    assert(size == expected_size);
    for (size_t index = 0; index < size; ++index) {
        assert(buffer[index] == expected[index]);
    }
}

int main(void)
{
    fs_init();
    assert(fs_disk_load() == FS_DISK_LOAD_EMPTY);
    assert(fs_create("/notes"));
    assert(fs_write("/notes", "older", 5));
    assert(fs_disk_save());

    assert(fs_write("/notes", "newer", 5));
    assert(fs_disk_save());
    const uint32_t second_slot_data_lba =
        FS_DISK_FIRST_LBA + FS_DISK_SLOT_SECTORS + 1;
    disk[second_slot_data_lba][0] ^= 0xff;

    fs_init();
    assert(fs_disk_load() == FS_DISK_LOAD_OK);
    expect_file_contents("older");

    assert(fs_write("/notes", "failed", 6));
    failed_write_lba = second_slot_data_lba;
    assert(!fs_disk_save());
    failed_write_lba = UINT32_MAX;

    fs_init();
    assert(fs_disk_load() == FS_DISK_LOAD_OK);
    expect_file_contents("older");

    assert(fs_write("/notes", "saved", 5));
    assert(fs_disk_save());
    fs_init();
    assert(fs_disk_load() == FS_DISK_LOAD_OK);
    expect_file_contents("saved");

    static uint8_t snapshot[FS_SNAPSHOT_SIZE];
    assert(fs_snapshot_export(snapshot, sizeof(snapshot)));
    snapshot[2087 + 2] = 0xff;
    snapshot[2087 + 3] = 0xff;
    assert(!fs_snapshot_import(snapshot, sizeof(snapshot)));
    expect_file_contents("saved");
    assert(fs_snapshot_export(snapshot, sizeof(snapshot)));
    snapshot[0] = 0;
    assert(!fs_snapshot_import(snapshot, sizeof(snapshot)));
    expect_file_contents("saved");

    puts("persistent filesystem tests passed");
    return 0;
}
