#ifndef DEVOS_FS_DISK_H
#define DEVOS_FS_DISK_H

#include <stdint.h>

#define FS_DISK_FIRST_LBA 1U
#define FS_DISK_PAYLOAD_SECTORS ((FS_SNAPSHOT_SIZE + 511U) / 512U)
#define FS_DISK_SLOT_SECTORS (1U + FS_DISK_PAYLOAD_SECTORS)
#define FS_DISK_REQUIRED_SECTORS \
    (FS_DISK_FIRST_LBA + 2U * FS_DISK_SLOT_SECTORS)

enum fs_disk_load_result {
    FS_DISK_LOAD_ERROR = -1,
    FS_DISK_LOAD_EMPTY = 0,
    FS_DISK_LOAD_OK = 1,
};

int fs_disk_load(void);
int fs_disk_save(void);

#endif
