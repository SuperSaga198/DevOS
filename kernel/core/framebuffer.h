#ifndef DEVOS_FRAMEBUFFER_H
#define DEVOS_FRAMEBUFFER_H

#include "limine.h"

int framebuffer_init(struct limine_framebuffer *framebuffer);
void framebuffer_clear(uint32_t color);
void framebuffer_rect(uint64_t x, uint64_t y, uint64_t width,
                      uint64_t height, uint32_t color);
void framebuffer_text(uint64_t x, uint64_t y, const char *text,
                      uint32_t color, uint32_t background, uint32_t scale);
void desktop_draw(uint64_t usable_memory_mib);
void framebuffer_terminal_open(void);
void framebuffer_terminal_clear(void);
void framebuffer_terminal_write(const char *text);
void framebuffer_terminal_backspace(void);

#endif
