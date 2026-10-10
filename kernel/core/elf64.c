#include "elf64.h"

#include <stdint.h>

#define ELF_HEADER_SIZE 64U
#define ELF_PROGRAM_HEADER_SIZE 56U
#define ELF_TYPE_EXECUTABLE 2U
#define ELF_MACHINE_X86_64 62U
#define ELF_VERSION_CURRENT 1U
#define ELF_PROGRAM_LOAD 1U
#define ELF_FLAG_EXECUTABLE 1U
#define ELF_USER_ADDRESS_LIMIT 0x0000800000000000ULL

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

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

static int valid_alignment(uint64_t alignment, uint64_t offset,
                           uint64_t address)
{
    if (alignment <= 1) {
        return 1;
    }
    return (alignment & (alignment - 1)) == 0 &&
        offset % alignment == address % alignment;
}

int elf64_inspect(const void *image, size_t image_size,
                  struct elf64_info *info)
{
    if (image == NULL || info == NULL || image_size < ELF_HEADER_SIZE) {
        return 0;
    }

    const uint8_t *bytes = image;
    if (bytes[0] != 0x7f || bytes[1] != 'E' || bytes[2] != 'L' ||
        bytes[3] != 'F' || bytes[4] != 2 || bytes[5] != 1 ||
        bytes[6] != ELF_VERSION_CURRENT ||
        read_u16(bytes + 16) != ELF_TYPE_EXECUTABLE ||
        read_u16(bytes + 18) != ELF_MACHINE_X86_64 ||
        read_u32(bytes + 20) != ELF_VERSION_CURRENT ||
        read_u16(bytes + 52) != ELF_HEADER_SIZE ||
        read_u16(bytes + 54) != ELF_PROGRAM_HEADER_SIZE) {
        return 0;
    }

    const uint64_t entry = read_u64(bytes + 24);
    const uint64_t program_offset = read_u64(bytes + 32);
    const uint16_t program_count = read_u16(bytes + 56);
    if (program_count == 0 || program_count > 128 ||
        program_offset > image_size ||
        (uint64_t)program_count >
            ((uint64_t)image_size - program_offset) /
                ELF_PROGRAM_HEADER_SIZE) {
        return 0;
    }

    struct elf64_info result = { .entry = entry, .segment_count = 0 };
    int entry_is_executable = 0;
    for (uint16_t index = 0; index < program_count; ++index) {
        const uint64_t header_offset = program_offset +
            (uint64_t)index * ELF_PROGRAM_HEADER_SIZE;
        const uint8_t *header = bytes + (size_t)header_offset;
        if (read_u32(header) != ELF_PROGRAM_LOAD) {
            continue;
        }
        if (result.segment_count == ELF64_MAX_LOAD_SEGMENTS) {
            return 0;
        }

        const uint32_t flags = read_u32(header + 4);
        const uint64_t file_offset = read_u64(header + 8);
        const uint64_t virtual_address = read_u64(header + 16);
        const uint64_t file_size = read_u64(header + 32);
        const uint64_t memory_size = read_u64(header + 40);
        const uint64_t alignment = read_u64(header + 48);
        if (memory_size == 0 || file_size > memory_size ||
            file_offset > image_size ||
            file_size > (uint64_t)image_size - file_offset ||
            virtual_address < 0x1000 ||
            virtual_address >= ELF_USER_ADDRESS_LIMIT ||
            memory_size > ELF_USER_ADDRESS_LIMIT - virtual_address ||
            !valid_alignment(alignment, file_offset, virtual_address)) {
            return 0;
        }

        const uint64_t segment_end = virtual_address + memory_size;
        for (size_t prior = 0; prior < result.segment_count; ++prior) {
            const struct elf64_segment *segment = &result.segments[prior];
            const uint64_t prior_end =
                segment->virtual_address + segment->memory_size;
            if (virtual_address < prior_end &&
                segment->virtual_address < segment_end) {
                return 0;
            }
        }

        result.segments[result.segment_count++] = (struct elf64_segment) {
            .virtual_address = virtual_address,
            .memory_size = memory_size,
            .file_size = file_size,
            .file_offset = file_offset,
            .flags = flags,
        };
        if ((flags & ELF_FLAG_EXECUTABLE) != 0 &&
            entry >= virtual_address && entry < segment_end) {
            entry_is_executable = 1;
        }
    }

    if (result.segment_count == 0 || !entry_is_executable) {
        return 0;
    }
    *info = result;
    return 1;
}
