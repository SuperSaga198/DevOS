#include "../kernel/core/framebuffer.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    uint32_t storage[18] = { 0 };
    struct limine_framebuffer framebuffer = {
        .address = storage,
        .width = 4,
        .height = 3,
        .pitch = 6 * sizeof(uint32_t),
        .bpp = 32,
        .memory_model = LIMINE_FRAMEBUFFER_RGB,
        .red_mask_size = 8,
        .red_mask_shift = 16,
        .green_mask_size = 8,
        .green_mask_shift = 8,
        .blue_mask_size = 8,
        .blue_mask_shift = 0,
    };

    assert(framebuffer_init(&framebuffer));
    framebuffer_rect(2, 1, 8, 8, 0x123456);

    assert(storage[0] == 0);
    assert(storage[6 + 2] == 0x123456);
    assert(storage[6 + 3] == 0x123456);
    assert(storage[12 + 2] == 0x123456);
    assert(storage[12 + 3] == 0x123456);
    assert(storage[6 + 1] == 0);
    assert(storage[6 + 4] == 0);

    framebuffer_text(0, 0, "A", 0xffffff, 0, 1);
    assert(storage[1] == 0xffffff);
    assert(storage[0] == 0);

    framebuffer_clear(0xabcdef);
    assert(storage[0] == 0xabcdef);
    assert(storage[5] == 0);

    framebuffer.bpp = 24;
    assert(!framebuffer_init(&framebuffer));

    uint32_t screen[128 * 128] = { 0 };
    framebuffer = (struct limine_framebuffer) {
        .address = screen,
        .width = 128,
        .height = 128,
        .pitch = 128 * sizeof(uint32_t),
        .bpp = 32,
        .memory_model = LIMINE_FRAMEBUFFER_RGB,
        .red_mask_size = 8,
        .red_mask_shift = 16,
        .green_mask_size = 8,
        .green_mask_shift = 8,
        .blue_mask_size = 8,
        .blue_mask_shift = 0,
    };
    assert(framebuffer_init(&framebuffer));
    desktop_draw(2048);
    framebuffer_clear(0x334455);
    framebuffer_cursor_set(20, 20);
    framebuffer_cursor_show();
    assert(screen[20 * 128 + 20] == 0x101521);
    assert(screen[22 * 128 + 21] == 0xf3f6fc);
    framebuffer_cursor_hide();
    assert(screen[20 * 128 + 20] == 0x334455);
    assert(screen[22 * 128 + 21] == 0x334455);
    framebuffer_cursor_set(127, 127);
    framebuffer_cursor_show();
    assert(screen[127 * 128 + 127] == 0x101521);
    framebuffer_cursor_hide();
    assert(screen[127 * 128 + 127] == 0x334455);
    framebuffer_terminal_open();
    framebuffer_terminal_write("DevOS terminal\nhelp");
    framebuffer_terminal_backspace();
    framebuffer_terminal_clear();

    puts("framebuffer tests passed");
    return 0;
}
