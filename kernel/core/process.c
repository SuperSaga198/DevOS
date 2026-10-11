#include "process.h"

#include "elf64.h"
#include "memory.h"
#include "scheduler.h"

#include <stdint.h>

#define PROCESS_STACK_BASE 0x7fffe000ULL
#define PROCESS_STACK_TOP (PROCESS_STACK_BASE + PAGE_SIZE)
#define PROCESS_MAX_IMAGE_PAGES 256u
#define ELF_FLAG_WRITE 2u
#define ELF_FLAG_EXECUTE 1u

static void clear_page(void *page)
{
    uint8_t *bytes = page;
    for (size_t index = 0; index < PAGE_SIZE; ++index) {
        bytes[index] = 0;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size)
{
    uint8_t *output = destination;
    const uint8_t *input = source;
    for (size_t index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

static int segment_pages(const struct elf64_segment *segment,
                         uint64_t *first_page, uint64_t *page_count)
{
    const uint64_t page_mask = PAGE_SIZE - 1;
    if (segment->memory_size == 0 ||
        segment->virtual_address > UINT64_MAX - segment->memory_size) {
        return 0;
    }
    const uint64_t end = segment->virtual_address + segment->memory_size;
    *first_page = segment->virtual_address & ~page_mask;
    const uint64_t rounded_end =
        end > UINT64_MAX - page_mask ? 0 : (end + page_mask) & ~page_mask;
    if (rounded_end == 0 || rounded_end <= *first_page) {
        return 0;
    }
    *page_count = (rounded_end - *first_page) / PAGE_SIZE;
    return 1;
}

static int load_segment(uint64_t address_space, const uint8_t *image,
                        const struct elf64_segment *segment,
                        size_t *total_pages)
{
    if ((segment->flags & (ELF_FLAG_WRITE | ELF_FLAG_EXECUTE)) ==
        (ELF_FLAG_WRITE | ELF_FLAG_EXECUTE)) {
        return 0;
    }
    uint64_t page_address;
    uint64_t page_count;
    if (!segment_pages(segment, &page_address, &page_count) ||
        page_count > PROCESS_MAX_IMAGE_PAGES - *total_pages) {
        return 0;
    }

    const uint64_t file_end = segment->virtual_address + segment->file_size;
    for (uint64_t page_index = 0; page_index < page_count; ++page_index) {
        const uint64_t virtual_page = page_address + page_index * PAGE_SIZE;
        uint64_t physical_page = 0;
        if (!page_alloc(&physical_page)) {
            return 0;
        }
        void *page = physical_to_virtual(physical_page);
        if (page == NULL) {
            (void)page_free(physical_page);
            return 0;
        }
        clear_page(page);

        const uint64_t copy_start = virtual_page > segment->virtual_address
            ? virtual_page : segment->virtual_address;
        const uint64_t page_end = virtual_page + PAGE_SIZE;
        const uint64_t copy_end = page_end < file_end ? page_end : file_end;
        if (copy_start < copy_end) {
            const uint64_t input_offset =
                segment->file_offset + copy_start - segment->virtual_address;
            copy_bytes((uint8_t *)page + (copy_start - virtual_page),
                       image + input_offset, (size_t)(copy_end - copy_start));
        }

        const int writable = (segment->flags & ELF_FLAG_WRITE) != 0;
        const int executable = (segment->flags & ELF_FLAG_EXECUTE) != 0;
        if (!memory_user_map_page(address_space, virtual_page, physical_page,
                                  writable, executable)) {
            (void)page_free(physical_page);
            return 0;
        }
        ++*total_pages;
    }
    return 1;
}

int process_exec_elf(const char *name, const void *image, size_t image_size)
{
    if (name == NULL || image == NULL || image_size == 0 ||
        !memory_enable_nx()) {
        return 0;
    }

    struct elf64_info executable;
    if (!elf64_inspect(image, image_size, &executable)) {
        return 0;
    }

    uint64_t address_space = 0;
    if (!memory_create_user_address_space(&address_space)) {
        return 0;
    }
    size_t mapped_pages = 0;
    const uint8_t *bytes = image;
    for (size_t index = 0; index < executable.segment_count; ++index) {
        if (!load_segment(address_space, bytes, &executable.segments[index],
                          &mapped_pages)) {
            (void)memory_destroy_user_address_space(address_space);
            return 0;
        }
    }

    uint64_t stack_page = 0;
    if (!page_alloc(&stack_page)) {
        (void)memory_destroy_user_address_space(address_space);
        return 0;
    }
    void *stack_memory = physical_to_virtual(stack_page);
    if (stack_memory == NULL) {
        (void)page_free(stack_page);
        (void)memory_destroy_user_address_space(address_space);
        return 0;
    }
    clear_page(stack_memory);
    if (!memory_user_map_page(address_space, PROCESS_STACK_BASE, stack_page,
                              1, 0)) {
        (void)page_free(stack_page);
        (void)memory_destroy_user_address_space(address_space);
        return 0;
    }

    if (!scheduler_create_user(name, address_space,
                               executable.entry, PROCESS_STACK_TOP - 8)) {
        (void)memory_destroy_user_address_space(address_space);
        return 0;
    }
    return 1;
}
