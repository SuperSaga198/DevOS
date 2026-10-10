#include "memory.h"

#include <stdint.h>

#define MAX_PHYSICAL_ADDRESS (4ULL * 1024 * 1024 * 1024)
#define MAX_PHYSICAL_PAGES (MAX_PHYSICAL_ADDRESS / PAGE_SIZE)
#define BITMAP_WORDS (MAX_PHYSICAL_PAGES / 64)
#define HEAP_MAGIC 0x4445564f53484541ULL

static uint64_t managed_bitmap[BITMAP_WORDS];
static uint64_t allocated_bitmap[BITMAP_WORDS];
static uint64_t direct_map_offset;
static struct memory_stats allocator_stats;
static int allocator_ready;

struct heap_header {
    uint64_t magic;
    uint64_t physical_address;
    size_t pages;
    size_t requested_size;
};

static int bitmap_test(const uint64_t *bitmap, uint64_t page)
{
    return (bitmap[page / 64] & (1ULL << (page % 64))) != 0;
}

static void bitmap_set(uint64_t *bitmap, uint64_t page)
{
    bitmap[page / 64] |= 1ULL << (page % 64);
}

static void bitmap_clear(uint64_t *bitmap, uint64_t page)
{
    bitmap[page / 64] &= ~(1ULL << (page % 64));
}

static int add_range(uint64_t base, uint64_t length)
{
    if (length == 0 || base > UINT64_MAX - length) {
        return 0;
    }

    const uint64_t end = base + length;
    if (base >= MAX_PHYSICAL_ADDRESS) {
        allocator_stats.ignored_pages += length / PAGE_SIZE;
        return 1;
    }

    const uint64_t clipped_end =
        end < MAX_PHYSICAL_ADDRESS ? end : MAX_PHYSICAL_ADDRESS;
    const uint64_t first_page =
        base / PAGE_SIZE + (base % PAGE_SIZE != 0);
    const uint64_t last_page = clipped_end / PAGE_SIZE;

    for (uint64_t page = first_page; page < last_page; ++page) {
        const uint64_t physical_address = page * PAGE_SIZE;
        if (physical_address < 0x100000) {
            continue;
        }
        if (!bitmap_test(managed_bitmap, page)) {
            bitmap_set(managed_bitmap, page);
            bitmap_clear(allocated_bitmap, page);
            ++allocator_stats.managed_pages;
            ++allocator_stats.free_pages;
        }
    }

    if (end > MAX_PHYSICAL_ADDRESS) {
        const uint64_t ignored_start =
            base > MAX_PHYSICAL_ADDRESS ? base : MAX_PHYSICAL_ADDRESS;
        allocator_stats.ignored_pages += (end - ignored_start) / PAGE_SIZE;
    }
    return 1;
}

int memory_init(const struct limine_memmap_response *response,
                uint64_t hhdm_offset)
{
    allocator_ready = 0;
    allocator_stats = (struct memory_stats) { 0, 0, 0 };
    direct_map_offset = hhdm_offset;
    if (response == NULL || response->entries == NULL ||
        response->entry_count == 0 || hhdm_offset == 0) {
        return 0;
    }

    for (uint64_t word = 0; word < BITMAP_WORDS; ++word) {
        managed_bitmap[word] = 0;
        allocated_bitmap[word] = UINT64_MAX;
    }

    for (uint64_t index = 0; index < response->entry_count; ++index) {
        const struct limine_memmap_entry *entry = response->entries[index];
        if (entry == NULL) {
            return 0;
        }
        if (entry->type == LIMINE_MEMMAP_USABLE &&
            !add_range(entry->base, entry->length)) {
            return 0;
        }
    }

    if (allocator_stats.free_pages == 0) {
        return 0;
    }
    allocator_ready = 1;
    return 1;
}

int page_alloc(uint64_t *physical_address)
{
    if (!allocator_ready || physical_address == NULL ||
        allocator_stats.free_pages == 0) {
        return 0;
    }

    for (uint64_t word = 0; word < BITMAP_WORDS; ++word) {
        if (allocated_bitmap[word] == UINT64_MAX) {
            continue;
        }

        const uint64_t candidates = ~allocated_bitmap[word];
        const uint64_t bit = (uint64_t)__builtin_ctzll(candidates);
        const uint64_t page = word * 64 + bit;
        if (!bitmap_test(managed_bitmap, page)) {
            continue;
        }

        bitmap_set(allocated_bitmap, page);
        --allocator_stats.free_pages;
        *physical_address = page * PAGE_SIZE;
        return 1;
    }
    return 0;
}

int page_free(uint64_t physical_address)
{
    if (!allocator_ready || physical_address % PAGE_SIZE != 0 ||
        physical_address >= MAX_PHYSICAL_ADDRESS) {
        return 0;
    }

    const uint64_t page = physical_address / PAGE_SIZE;
    if (!bitmap_test(managed_bitmap, page) ||
        !bitmap_test(allocated_bitmap, page)) {
        return 0;
    }

    bitmap_clear(allocated_bitmap, page);
    ++allocator_stats.free_pages;
    return 1;
}

void *physical_to_virtual(uint64_t physical_address)
{
    if (!allocator_ready || physical_address >= MAX_PHYSICAL_ADDRESS ||
        direct_map_offset > UINTPTR_MAX - physical_address) {
        return NULL;
    }
    return (void *)(uintptr_t)(direct_map_offset + physical_address);
}

int memory_get_stats(struct memory_stats *stats)
{
    if (!allocator_ready || stats == NULL) {
        return 0;
    }
    *stats = allocator_stats;
    return 1;
}

void *heap_alloc(size_t size)
{
    if (!allocator_ready || size == 0 ||
        size > SIZE_MAX - sizeof(struct heap_header)) {
        return NULL;
    }

    const size_t total = size + sizeof(struct heap_header);
    const size_t pages = total / PAGE_SIZE + (total % PAGE_SIZE != 0);
    if (pages > UINT64_MAX / PAGE_SIZE) {
        return NULL;
    }

    uint64_t physical_address = 0;
    uint64_t candidate_start = 0;
    size_t candidate_pages = 0;
    for (uint64_t page = 0; page < MAX_PHYSICAL_PAGES; ++page) {
        if (bitmap_test(managed_bitmap, page) &&
            !bitmap_test(allocated_bitmap, page)) {
            if (candidate_pages == 0) {
                candidate_start = page * PAGE_SIZE;
            }
            ++candidate_pages;
            if (candidate_pages == pages) {
                physical_address = candidate_start;
                break;
            }
        } else {
            candidate_pages = 0;
        }
    }
    if (candidate_pages != pages) {
        return NULL;
    }
    for (size_t index = 0; index < pages; ++index) {
        bitmap_set(allocated_bitmap, physical_address / PAGE_SIZE + index);
        --allocator_stats.free_pages;
    }

    struct heap_header *header = physical_to_virtual(physical_address);
    if (header == NULL) {
        for (size_t index = 0; index < pages; ++index) {
            (void)page_free(physical_address + index * PAGE_SIZE);
        }
        return NULL;
    }
    header->magic = HEAP_MAGIC;
    header->physical_address = physical_address;
    header->pages = pages;
    header->requested_size = size;
    return header + 1;
}

size_t heap_allocation_size(const void *pointer)
{
    if (pointer == NULL) {
        return 0;
    }
    const struct heap_header *header =
        (const struct heap_header *)pointer - 1;
    return header->magic == HEAP_MAGIC ? header->requested_size : 0;
}

int heap_free(void *pointer)
{
    if (pointer == NULL) {
        return 0;
    }

    struct heap_header *header = (struct heap_header *)pointer - 1;
    if (header->magic != HEAP_MAGIC || header->pages == 0) {
        return 0;
    }

    const uint64_t physical_address = header->physical_address;
    const size_t pages = header->pages;
    header->magic = 0;
    for (size_t index = 0; index < pages; ++index) {
        if (!page_free(physical_address + index * PAGE_SIZE)) {
            return 0;
        }
    }
    return 1;
}
