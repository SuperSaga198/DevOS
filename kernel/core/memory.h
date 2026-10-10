#ifndef DEVOS_MEMORY_H
#define DEVOS_MEMORY_H

#include "limine.h"

#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096ULL

struct memory_stats {
    uint64_t managed_pages;
    uint64_t free_pages;
    uint64_t ignored_pages;
};

int memory_init(const struct limine_memmap_response *response,
                uint64_t hhdm_offset);
int page_alloc(uint64_t *physical_address);
int page_free(uint64_t physical_address);
void *physical_to_virtual(uint64_t physical_address);
int memory_get_stats(struct memory_stats *stats);
void *heap_alloc(size_t size);
int heap_free(void *pointer);
size_t heap_allocation_size(const void *pointer);

#endif
