#ifndef DEVOS_ELF64_H
#define DEVOS_ELF64_H

#include <stddef.h>
#include <stdint.h>

#define ELF64_MAX_LOAD_SEGMENTS 16

struct elf64_segment {
    uint64_t virtual_address;
    uint64_t memory_size;
    uint64_t file_size;
    uint64_t file_offset;
    uint32_t flags;
};

struct elf64_info {
    uint64_t entry;
    size_t segment_count;
    struct elf64_segment segments[ELF64_MAX_LOAD_SEGMENTS];
};

int elf64_inspect(const void *image, size_t image_size,
                  struct elf64_info *info);

#endif
