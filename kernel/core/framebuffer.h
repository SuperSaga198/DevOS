#ifndef DEVOS_FRAMEBUFFER_H
#define DEVOS_FRAMEBUFFER_H

#include "limine.h"

#include <stddef.h>
#include <stdint.h>

#define DESKTOP_WINDOW_MAX 8u
#define DESKTOP_ACTION_NONE 0
#define DESKTOP_ACTION_REDRAW 1
#define DESKTOP_ACTION_OPEN_TERMINAL 2
#define DESKTOP_ACTION_FILE_PARENT 3
#define DESKTOP_ACTION_AUDIO_TEST_TONE 4
#define DESKTOP_ACTION_NETWORK_PING 5
#define DESKTOP_ACTION_FILE_ITEM_BASE 16
#define DESKTOP_ACTION_WALLPAPER_BASE 32
#define DESKTOP_ACTION_ANIMATION_TOGGLE 40
#define DESKTOP_FILE_ITEMS_MAX 7u
#define DESKTOP_FILE_NAME_MAX 32u
#define DESKTOP_FILE_PATH_MAX 64u
#define DESKTOP_FILE_PREVIEW_MAX 256u

struct desktop_file_item {
    char name[DESKTOP_FILE_NAME_MAX];
    uint32_t size;
    uint8_t is_directory;
};

struct desktop_runtime_status {
    uint64_t managed_pages;
    uint64_t free_pages;
    uint64_t udp_echoes;
    uint64_t tcp_echoes;
    uint64_t tcp_segments;
    uint8_t network_ready;
    uint8_t gateway_reachable;
    uint8_t storage_ready;
    uint8_t audio_ready;
};

int framebuffer_init(struct limine_framebuffer *framebuffer);
void framebuffer_clear(uint32_t color);
void framebuffer_rect(uint64_t x, uint64_t y, uint64_t width,
                      uint64_t height, uint32_t color);
void framebuffer_text(uint64_t x, uint64_t y, const char *text,
                      uint32_t color, uint32_t background, uint32_t scale);
void desktop_draw(uint64_t usable_memory_mib);
void desktop_set_identity(const char *username, const char *hostname);
void desktop_set_runtime_status(
    const struct desktop_runtime_status *status);
void framebuffer_setup_draw(uint8_t state, const char *input,
                            const char *message);
void desktop_window_init(void);
int desktop_window_open(const char *name, uint64_t x, uint64_t y,
                       uint64_t width, uint64_t height, uint32_t accent);
int desktop_window_close(const char *name);
int desktop_window_is_open(const char *name);
size_t desktop_window_count(void);
const char *desktop_window_name(size_t index);
void desktop_window_focus(const char *name);
int desktop_pointer_event(uint64_t x, uint64_t y, uint8_t buttons);
int desktop_animation_step(void);
size_t desktop_wallpaper_index(void);
int desktop_animations_enabled(void);
int desktop_file_manager_update(const char *path,
                               const struct desktop_file_item *items,
                               size_t count, const char *preview);
void framebuffer_terminal_open(void);
void framebuffer_terminal_clear(void);
void framebuffer_terminal_write(const char *text);
void framebuffer_terminal_backspace(void);
uint64_t framebuffer_width(void);
uint64_t framebuffer_height(void);
void framebuffer_cursor_set(uint64_t x, uint64_t y);
void framebuffer_cursor_hide(void);
void framebuffer_cursor_show(void);

#endif
