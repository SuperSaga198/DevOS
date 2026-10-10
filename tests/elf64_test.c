#include "../kernel/core/elf64.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define IMAGE_SIZE 4096
#define HEADER_SIZE 64
#define PROGRAM_HEADER_SIZE 56
#define PROGRAM_TABLE_OFFSET HEADER_SIZE
#define FIRST_SEGMENT_OFFSET \
    (PROGRAM_TABLE_OFFSET + 2 * PROGRAM_HEADER_SIZE)

static uint8_t image[IMAGE_SIZE];

static void write_u16(size_t offset, uint16_t value)
{
    image[offset] = (uint8_t)value;
    image[offset + 1] = (uint8_t)(value >> 8);
}

static void write_u32(size_t offset, uint32_t value)
{
    for (size_t byte = 0; byte < 4; ++byte) {
        image[offset + byte] = (uint8_t)(value >> (byte * 8));
    }
}

static void write_u64(size_t offset, uint64_t value)
{
    for (size_t byte = 0; byte < 8; ++byte) {
        image[offset + byte] = (uint8_t)(value >> (byte * 8));
    }
}

static void make_program_header(size_t offset, uint32_t type, uint32_t flags,
                                uint64_t file_offset, uint64_t virtual_address,
                                uint64_t file_size, uint64_t memory_size,
                                uint64_t alignment)
{
    write_u32(offset, type);
    write_u32(offset + 4, flags);
    write_u64(offset + 8, file_offset);
    write_u64(offset + 16, virtual_address);
    write_u64(offset + 24, virtual_address);
    write_u64(offset + 32, file_size);
    write_u64(offset + 40, memory_size);
    write_u64(offset + 48, alignment);
}

static void make_valid_image(void)
{
    for (size_t index = 0; index < sizeof(image); ++index) {
        image[index] = 0;
    }
    image[0] = 0x7f;
    image[1] = 'E';
    image[2] = 'L';
    image[3] = 'F';
    image[4] = 2;
    image[5] = 1;
    image[6] = 1;
    write_u16(16, 2);
    write_u16(18, 62);
    write_u32(20, 1);
    write_u64(24, 0x400000);
    write_u64(32, PROGRAM_TABLE_OFFSET);
    write_u16(52, HEADER_SIZE);
    write_u16(54, PROGRAM_HEADER_SIZE);
    write_u16(56, 2);

    make_program_header(PROGRAM_TABLE_OFFSET, 1, 5,
                        FIRST_SEGMENT_OFFSET, 0x400000, 4, 0x1000, 1);
    make_program_header(PROGRAM_TABLE_OFFSET + PROGRAM_HEADER_SIZE,
                        1, 6, FIRST_SEGMENT_OFFSET + 4,
                        0x401000, 0, 0x1000, 1);
    image[FIRST_SEGMENT_OFFSET] = 0x90;
    image[FIRST_SEGMENT_OFFSET + 1] = 0xc3;
}

int main(void)
{
    struct elf64_info info;
    make_valid_image();
    assert(elf64_inspect(image, sizeof(image), &info));
    assert(info.entry == 0x400000);
    assert(info.segment_count == 2);
    assert(info.segments[0].virtual_address == 0x400000);
    assert(info.segments[0].file_size == 4);
    assert(info.segments[0].memory_size == 0x1000);
    assert((info.segments[0].flags & 1) != 0);
    assert(info.segments[1].file_size == 0);
    assert(info.segments[1].memory_size == 0x1000);

    assert(!elf64_inspect(NULL, sizeof(image), &info));
    assert(!elf64_inspect(image, HEADER_SIZE - 1, &info));

    make_valid_image();
    image[4] = 1;
    assert(!elf64_inspect(image, sizeof(image), &info));

    make_valid_image();
    write_u16(18, 3);
    assert(!elf64_inspect(image, sizeof(image), &info));

    make_valid_image();
    write_u64(32, sizeof(image) - 1);
    assert(!elf64_inspect(image, sizeof(image), &info));

    make_valid_image();
    write_u64(PROGRAM_TABLE_OFFSET + 32, UINT64_MAX);
    assert(!elf64_inspect(image, sizeof(image), &info));

    make_valid_image();
    write_u64(PROGRAM_TABLE_OFFSET + 40, UINT64_MAX);
    assert(!elf64_inspect(image, sizeof(image), &info));

    make_valid_image();
    write_u64(PROGRAM_TABLE_OFFSET + 16, 0x0000800000000000ULL);
    assert(!elf64_inspect(image, sizeof(image), &info));

    make_valid_image();
    write_u64(PROGRAM_TABLE_OFFSET + 16, 0x401000);
    assert(!elf64_inspect(image, sizeof(image), &info));

    make_valid_image();
    write_u64(PROGRAM_TABLE_OFFSET + 48, 3);
    assert(!elf64_inspect(image, sizeof(image), &info));

    make_valid_image();
    write_u64(PROGRAM_TABLE_OFFSET + PROGRAM_HEADER_SIZE + 16, 0x400800);
    assert(!elf64_inspect(image, sizeof(image), &info));

    make_valid_image();
    write_u16(56, ELF64_MAX_LOAD_SEGMENTS + 1);
    for (size_t index = 0; index <= ELF64_MAX_LOAD_SEGMENTS; ++index) {
        make_program_header(PROGRAM_TABLE_OFFSET +
                                index * PROGRAM_HEADER_SIZE,
                            1, index == 0 ? 5 : 6,
                            PROGRAM_TABLE_OFFSET +
                                (ELF64_MAX_LOAD_SEGMENTS + 1) *
                                    PROGRAM_HEADER_SIZE,
                            0x400000 + index * 0x1000, 0, 0x1000, 1);
    }
    assert(!elf64_inspect(image, sizeof(image), &info));

    uint32_t random_state = 0x9e3779b9U;
    for (size_t trial = 0; trial < 1000; ++trial) {
        for (size_t index = 0; index < sizeof(image); ++index) {
            random_state = random_state * 1664525U + 1013904223U;
            image[index] = (uint8_t)(random_state >> 24);
        }
        const size_t size = trial % (sizeof(image) + 1);
        (void)elf64_inspect(image, size, &info);
    }

    puts("ELF64 inspection tests passed");
    return 0;
}
