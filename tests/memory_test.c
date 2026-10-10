#include "../kernel/core/memory.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint8_t physical_memory[8 * PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));

int main(void)
{
    const uint64_t base = 0x100000;
    struct limine_memmap_entry usable = {
        .base = base,
        .length = sizeof(physical_memory),
        .type = LIMINE_MEMMAP_USABLE,
    };
    struct limine_memmap_entry reserved = {
        .base = base,
        .length = sizeof(physical_memory),
        .type = 1,
    };
    struct limine_memmap_entry *entries[] = { &usable, &reserved };
    struct limine_memmap_response response = {
        .entry_count = 2,
        .entries = entries,
    };
    const uint64_t direct_map =
        (uint64_t)(uintptr_t)physical_memory - base;

    assert(memory_init(&response, direct_map));
    assert(physical_to_virtual(base) == physical_memory);

    struct memory_stats before;
    assert(memory_get_stats(&before));
    assert(before.managed_pages == 8);
    assert(before.free_pages == 8);

    uint64_t page;
    assert(page_alloc(&page));
    assert(page == base);
    assert(page_free(page));
    assert(!page_free(page));
    assert(!page_free(page + 1));

    uint8_t *first = heap_alloc(5000);
    assert(first != NULL);
    assert(heap_allocation_size(first) == 5000);
    for (size_t index = 0; index < 5000; ++index) {
        first[index] = (uint8_t)(index & 0xff);
    }

    uint8_t *second = heap_alloc(100);
    assert(second != NULL);
    assert(heap_allocation_size(second) == 100);
    assert(!heap_free(NULL));
    assert(heap_free(first));
    assert(heap_allocation_size(first) == 0);
    assert(heap_free(second));
    assert(heap_alloc(0) == NULL);

    struct memory_stats after;
    assert(memory_get_stats(&after));
    assert(after.free_pages == before.free_pages);

    for (size_t index = 0; index < 8; ++index) {
        assert(page_alloc(&page));
    }
    assert(!page_alloc(&page));
    for (size_t index = 0; index < 8; ++index) {
        assert(page_free(base + index * PAGE_SIZE));
    }
    assert(!page_free(base));

    assert(!memory_init(NULL, direct_map));
    assert(!memory_get_stats(&after));

    puts("memory allocator tests passed");
    return 0;
}
