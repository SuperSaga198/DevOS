#ifndef DEVOS_LIMINE_H
#define DEVOS_LIMINE_H

#include <stdint.h>

#define LIMINE_COMMON_MAGIC \
    0xc7b1dd30df4c8b88, 0x0a82e883a194f07b

#define LIMINE_FRAMEBUFFER_REQUEST_ID \
    0x9d5827dcd881dd75, 0xa3148604f6fab11b
#define LIMINE_MEMMAP_REQUEST_ID \
    0x67cf3d9d378a806f, 0xe304acdfc50c3c62
#define LIMINE_HHDM_REQUEST_ID \
    0x48dcf1cb8ad2b852, 0x63984e959a98244b

#define LIMINE_MEMMAP_USABLE 0
#define LIMINE_FRAMEBUFFER_RGB 1

struct limine_framebuffer {
    void *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint8_t unused[7];
    uint64_t edid_size;
    void *edid;
    uint64_t mode_count;
    void **modes;
};

struct limine_framebuffer_response {
    uint64_t revision;
    uint64_t framebuffer_count;
    struct limine_framebuffer **framebuffers;
};

struct limine_framebuffer_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_framebuffer_response *response;
};

struct limine_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

struct limine_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    struct limine_memmap_entry **entries;
};

struct limine_memmap_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_memmap_response *response;
};

struct limine_hhdm_response {
    uint64_t revision;
    uint64_t offset;
};

struct limine_hhdm_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_hhdm_response *response;
};

#endif
