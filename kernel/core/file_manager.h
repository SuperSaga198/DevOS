#ifndef DEVOS_FILE_MANAGER_H
#define DEVOS_FILE_MANAGER_H

#include <stddef.h>
#include <stdint.h>

#define FILE_MANAGER_ENTRY_LIMIT 7u
#define FILE_MANAGER_NAME_CAPACITY 32u
#define FILE_MANAGER_PATH_CAPACITY 64u
#define FILE_MANAGER_PREVIEW_CAPACITY 256u

struct file_manager_entry {
    char name[FILE_MANAGER_NAME_CAPACITY];
    uint32_t size;
    uint8_t is_directory;
};

void file_manager_init(void);
void file_manager_refresh(void);
void file_manager_parent(void);
int file_manager_open_entry(size_t index);
const char *file_manager_path(void);
const char *file_manager_preview(void);
size_t file_manager_entry_count(void);
const struct file_manager_entry *file_manager_entry_at(size_t index);

#endif
