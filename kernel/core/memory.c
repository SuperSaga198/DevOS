#include "memory.h"

#include <stdint.h>

#define MAX_PHYSICAL_ADDRESS (4ULL * 1024 * 1024 * 1024)
#define MAX_PHYSICAL_PAGES (MAX_PHYSICAL_ADDRESS / PAGE_SIZE)
#define BITMAP_WORDS (MAX_PHYSICAL_PAGES / 64)
#define HEAP_MAGIC 0x4445564f53484541ULL
#define MMIO_VIRTUAL_BASE 0xffffc00000000000ULL
#define PAGE_PRESENT 0x001ULL
#define PAGE_WRITABLE 0x002ULL
#define PAGE_WRITE_THROUGH 0x008ULL
#define PAGE_CACHE_DISABLE 0x010ULL
#define PAGE_USER 0x004ULL
#define PAGE_NO_EXECUTE (1ULL << 63)
#define PAGE_ADDRESS_MASK 0x000ffffffffff000ULL

static uint64_t managed_bitmap[BITMAP_WORDS];
static uint64_t allocated_bitmap[BITMAP_WORDS];
static uint64_t direct_map_offset;
static struct memory_stats allocator_stats;
static int allocator_ready;
static int nx_enabled;

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

static uint64_t *page_table_from_entry(uint64_t entry)
{
    return physical_to_virtual(entry & PAGE_ADDRESS_MASK);
}

static uint64_t *allocate_page_table(void)
{
    uint64_t physical_address = 0;
    if (!page_alloc(&physical_address)) {
        return NULL;
    }
    uint64_t *table = physical_to_virtual(physical_address);
    if (table == NULL) {
        (void)page_free(physical_address);
        return NULL;
    }
    for (size_t index = 0; index < PAGE_SIZE / sizeof(*table); ++index) {
        table[index] = 0;
    }
    return table;
}

void *memory_map_mmio(uint64_t physical_address, size_t size)
{
    if (!allocator_ready || size == 0 ||
        physical_address > UINT64_MAX - size) {
        return NULL;
    }

    const uint64_t physical_base =
        physical_address & ~(PAGE_SIZE - 1);
    const uint64_t physical_end = physical_address + size;
    if (physical_end > UINT64_MAX - (PAGE_SIZE - 1)) {
        return NULL;
    }
    const uint64_t mapped_end =
        (physical_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (mapped_end < physical_end) {
        return NULL;
    }
    const uint64_t page_count = (mapped_end - physical_base) / PAGE_SIZE;
    const uintptr_t virtual_base = (uintptr_t)MMIO_VIRTUAL_BASE;
    uint64_t root_physical;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(root_physical));
    uint64_t *root = page_table_from_entry(root_physical);
    if (root == NULL) {
        return NULL;
    }

    for (uint64_t page = 0; page < page_count; ++page) {
        const uintptr_t address =
            virtual_base + page * (uintptr_t)PAGE_SIZE;
        const uint64_t pml4_index = (address >> 39) & 0x1ff;
        const uint64_t pdpt_index = (address >> 30) & 0x1ff;
        const uint64_t pd_index = (address >> 21) & 0x1ff;
        const uint64_t pt_index = (address >> 12) & 0x1ff;

        if ((root[pml4_index] & PAGE_PRESENT) == 0) {
            uint64_t *table = allocate_page_table();
            if (table == NULL) {
                return NULL;
            }
            root[pml4_index] =
                ((uintptr_t)table - direct_map_offset) |
                PAGE_PRESENT | PAGE_WRITABLE;
        }
        uint64_t *pdpt = page_table_from_entry(root[pml4_index]);
        if (pdpt == NULL) {
            return NULL;
        }
        if ((pdpt[pdpt_index] & PAGE_PRESENT) == 0) {
            uint64_t *table = allocate_page_table();
            if (table == NULL) {
                return NULL;
            }
            pdpt[pdpt_index] =
                ((uintptr_t)table - direct_map_offset) |
                PAGE_PRESENT | PAGE_WRITABLE;
        } else if ((pdpt[pdpt_index] & 0x080) != 0) {
            return NULL;
        }
        uint64_t *pd = page_table_from_entry(pdpt[pdpt_index]);
        if (pd == NULL) {
            return NULL;
        }
        if ((pd[pd_index] & PAGE_PRESENT) == 0) {
            uint64_t *table = allocate_page_table();
            if (table == NULL) {
                return NULL;
            }
            pd[pd_index] =
                ((uintptr_t)table - direct_map_offset) |
                PAGE_PRESENT | PAGE_WRITABLE;
        } else if ((pd[pd_index] & 0x080) != 0) {
            return NULL;
        }
        uint64_t *pt = page_table_from_entry(pd[pd_index]);
        if (pt == NULL || (pt[pt_index] & PAGE_PRESENT) != 0) {
            return NULL;
        }
        pt[pt_index] =
            (physical_base + page * PAGE_SIZE) |
            PAGE_PRESENT | PAGE_WRITABLE | PAGE_WRITE_THROUGH |
            PAGE_CACHE_DISABLE;
        __asm__ volatile ("invlpg (%0)" : : "r"(address) : "memory");
    }

    return (void *)(virtual_base + (physical_address - physical_base));
}

int memory_create_user_address_space(uint64_t *root_physical)
{
    if (!allocator_ready || root_physical == NULL) {
        return 0;
    }

    uint64_t current_root_physical;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(current_root_physical));
    uint64_t *current_root = page_table_from_entry(current_root_physical);
    uint64_t *new_root = allocate_page_table();
    if (current_root == NULL || new_root == NULL) {
        if (new_root != NULL) {
            const uint64_t address =
                (uintptr_t)new_root - direct_map_offset;
            (void)page_free(address);
        }
        return 0;
    }

    for (size_t index = 256; index < 512; ++index) {
        new_root[index] = current_root[index];
    }
    *root_physical = (uintptr_t)new_root - direct_map_offset;
    return 1;
}

int memory_destroy_user_address_space(uint64_t root_physical)
{
    if (!allocator_ready || (root_physical & (PAGE_SIZE - 1)) != 0) {
        return 0;
    }
    uint64_t current_root;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(current_root));
    if ((current_root & PAGE_ADDRESS_MASK) == root_physical) {
        return 0;
    }
    uint64_t *root = physical_to_virtual(root_physical);
    if (root == NULL) {
        return 0;
    }
    for (size_t pml4_index = 0; pml4_index < 256; ++pml4_index) {
        const uint64_t pml4_entry = root[pml4_index];
        if ((pml4_entry & (PAGE_PRESENT | PAGE_USER)) !=
            (PAGE_PRESENT | PAGE_USER)) {
            continue;
        }
        uint64_t *pdpt = page_table_from_entry(pml4_entry);
        if (pdpt == NULL) {
            return 0;
        }
        for (size_t pdpt_index = 0; pdpt_index < 512; ++pdpt_index) {
            const uint64_t pdpt_entry = pdpt[pdpt_index];
            if ((pdpt_entry & (PAGE_PRESENT | PAGE_USER)) !=
                (PAGE_PRESENT | PAGE_USER)) {
                continue;
            }
            uint64_t *pd = page_table_from_entry(pdpt_entry);
            if (pd == NULL) {
                return 0;
            }
            for (size_t pd_index = 0; pd_index < 512; ++pd_index) {
                const uint64_t pd_entry = pd[pd_index];
                if ((pd_entry & (PAGE_PRESENT | PAGE_USER)) !=
                    (PAGE_PRESENT | PAGE_USER)) {
                    continue;
                }
                uint64_t *pt = page_table_from_entry(pd_entry);
                if (pt == NULL) {
                    return 0;
                }
                for (size_t pt_index = 0; pt_index < 512; ++pt_index) {
                    const uint64_t pt_entry = pt[pt_index];
                    if ((pt_entry & (PAGE_PRESENT | PAGE_USER)) ==
                        (PAGE_PRESENT | PAGE_USER)) {
                        (void)page_free(pt_entry & PAGE_ADDRESS_MASK);
                    }
                }
                (void)page_free(pd_entry & PAGE_ADDRESS_MASK);
            }
            (void)page_free(pdpt_entry & PAGE_ADDRESS_MASK);
        }
        (void)page_free(pml4_entry & PAGE_ADDRESS_MASK);
    }
    return page_free(root_physical);
}

int memory_enable_nx(void)
{
    if (nx_enabled) {
        return 1;
    }
    uint32_t maximum_extended_leaf;
    uint32_t unused_b, unused_c, unused_d;
    __asm__ volatile ("cpuid"
                      : "=a"(maximum_extended_leaf), "=b"(unused_b),
                        "=c"(unused_c), "=d"(unused_d)
                      : "a"(0x80000000u), "c"(0));
    if (maximum_extended_leaf < 0x80000001u) {
        return 0;
    }
    uint32_t features;
    uint32_t unused_feature_a, unused_feature_b, unused_feature_c;
    __asm__ volatile ("cpuid"
                      : "=a"(unused_feature_a), "=b"(unused_feature_b),
                        "=c"(unused_feature_c), "=d"(features)
                      : "a"(0x80000001u), "c"(0));
    if ((features & (1u << 20)) == 0) {
        return 0;
    }

    uint32_t efer_low;
    uint32_t efer_high;
    __asm__ volatile ("rdmsr" : "=a"(efer_low), "=d"(efer_high) : "c"(0xc0000080u));
    efer_low |= 1u << 11;
    __asm__ volatile ("wrmsr" : : "a"(efer_low), "d"(efer_high),
                      "c"(0xc0000080u) : "memory");
    nx_enabled = 1;
    return 1;
}

int memory_user_map_page(uint64_t root_physical, uint64_t virtual_address,
                         uint64_t page_physical, int writable,
                         int executable)
{
    if (!allocator_ready || (virtual_address & (PAGE_SIZE - 1)) != 0 ||
        (page_physical & (PAGE_SIZE - 1)) != 0 ||
        virtual_address < PAGE_SIZE ||
        virtual_address >= 0x0000800000000000ULL ||
        page_physical >= MAX_PHYSICAL_ADDRESS) {
        return 0;
    }

    uint64_t *root = page_table_from_entry(root_physical);
    if (root == NULL) {
        return 0;
    }
    const uint64_t pml4_index = (virtual_address >> 39) & 0x1ff;
    const uint64_t pdpt_index = (virtual_address >> 30) & 0x1ff;
    const uint64_t pd_index = (virtual_address >> 21) & 0x1ff;
    const uint64_t pt_index = (virtual_address >> 12) & 0x1ff;
    if (pml4_index >= 256) {
        return 0;
    }

    if ((root[pml4_index] & PAGE_PRESENT) == 0) {
        uint64_t *table = allocate_page_table();
        if (table == NULL) {
            return 0;
        }
        root[pml4_index] =
            ((uintptr_t)table - direct_map_offset) |
            PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }
    uint64_t *pdpt = page_table_from_entry(root[pml4_index]);
    if (pdpt == NULL) {
        return 0;
    }
    if ((pdpt[pdpt_index] & PAGE_PRESENT) == 0) {
        uint64_t *table = allocate_page_table();
        if (table == NULL) {
            return 0;
        }
        pdpt[pdpt_index] =
            ((uintptr_t)table - direct_map_offset) |
            PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }
    uint64_t *pd = page_table_from_entry(pdpt[pdpt_index]);
    if (pd == NULL) {
        return 0;
    }
    if ((pd[pd_index] & PAGE_PRESENT) == 0) {
        uint64_t *table = allocate_page_table();
        if (table == NULL) {
            return 0;
        }
        pd[pd_index] =
            ((uintptr_t)table - direct_map_offset) |
            PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }
    uint64_t *pt = page_table_from_entry(pd[pd_index]);
    if (pt == NULL || (pt[pt_index] & PAGE_PRESENT) != 0) {
        return 0;
    }

    uint64_t flags = PAGE_PRESENT | PAGE_USER;
    if (writable) {
        flags |= PAGE_WRITABLE;
    }
    if (!executable) {
        if (!nx_enabled && !memory_enable_nx()) {
            return 0;
        }
        flags |= PAGE_NO_EXECUTE;
    }
    pt[pt_index] = page_physical | flags;
    return 1;
}

int memory_user_range_readable(uint64_t root_physical,
                               uint64_t virtual_address, size_t size)
{
    if (!allocator_ready || size == 0 ||
        virtual_address < PAGE_SIZE ||
        virtual_address >= 0x0000800000000000ULL ||
        size > 0x0000800000000000ULL - virtual_address) {
        return 0;
    }

    const uint64_t end = virtual_address + size;
    uint64_t current = virtual_address;
    while (current < end) {
        const uint64_t pml4_index = (current >> 39) & 0x1ff;
        const uint64_t pdpt_index = (current >> 30) & 0x1ff;
        const uint64_t pd_index = (current >> 21) & 0x1ff;
        const uint64_t pt_index = (current >> 12) & 0x1ff;
        uint64_t *root = page_table_from_entry(root_physical);
        if (root == NULL ||
            (root[pml4_index] & (PAGE_PRESENT | PAGE_USER)) !=
                (PAGE_PRESENT | PAGE_USER)) {
            return 0;
        }
        uint64_t *pdpt = page_table_from_entry(root[pml4_index]);
        if (pdpt == NULL ||
            (pdpt[pdpt_index] & (PAGE_PRESENT | PAGE_USER)) !=
                (PAGE_PRESENT | PAGE_USER)) {
            return 0;
        }
        uint64_t *pd = page_table_from_entry(pdpt[pdpt_index]);
        if (pd == NULL ||
            (pd[pd_index] & (PAGE_PRESENT | PAGE_USER)) !=
                (PAGE_PRESENT | PAGE_USER)) {
            return 0;
        }
        uint64_t *pt = page_table_from_entry(pd[pd_index]);
        if (pt == NULL ||
            (pt[pt_index] & (PAGE_PRESENT | PAGE_USER)) !=
                (PAGE_PRESENT | PAGE_USER)) {
            return 0;
        }
        const uint64_t next_page =
            (current & ~(PAGE_SIZE - 1)) + PAGE_SIZE;
        current = next_page < end ? next_page : end;
    }
    return 1;
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
