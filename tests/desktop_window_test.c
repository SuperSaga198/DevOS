#include "../kernel/core/framebuffer.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    static uint32_t screen[640 * 480];
    struct limine_framebuffer framebuffer = {
        .address = screen,
        .width = 640,
        .height = 480,
        .pitch = 640 * sizeof(uint32_t),
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
    desktop_window_init();
    assert(desktop_window_count() == 2);
    assert(desktop_window_is_open("SYSTEM"));
    assert(desktop_window_open("TERMINAL", 20, 20, 200, 140, 0x2b6cb5));
    assert(desktop_window_is_open("TERMINAL"));
    struct desktop_file_item items[2] = {
        { .name = "projects", .is_directory = 1 },
        { .name = "readme.txt", .size = 10, .is_directory = 0 },
    };
    assert(desktop_file_manager_update("/", items, 2, "Choose a file"));
    desktop_window_focus("TERMINAL");
    assert(desktop_window_close("TERMINAL"));
    assert(!desktop_window_is_open("TERMINAL"));
    assert(desktop_pointer_event(20, 450, 1) == DESKTOP_ACTION_REDRAW);
    assert(desktop_pointer_event(20, 450, 0) == DESKTOP_ACTION_NONE);
    assert(desktop_pointer_event(35, 220, 1) == DESKTOP_ACTION_REDRAW);
    assert(desktop_window_is_open("FILES"));
    assert(desktop_pointer_event(35, 220, 0) == DESKTOP_ACTION_NONE);
    assert(desktop_pointer_event(420, 164, 1) ==
           DESKTOP_ACTION_FILE_ITEM_BASE);
    assert(desktop_pointer_event(420, 164, 0) == DESKTOP_ACTION_NONE);
    assert(desktop_pointer_event(420, 180, 1) ==
           DESKTOP_ACTION_FILE_ITEM_BASE + 1);
    assert(desktop_pointer_event(420, 180, 0) == DESKTOP_ACTION_NONE);
    assert(desktop_pointer_event(140, 450, 1) ==
           DESKTOP_ACTION_OPEN_TERMINAL);
    assert(desktop_pointer_event(140, 450, 0) == DESKTOP_ACTION_NONE);
    assert(desktop_window_open("AUDIO", 50, 50, 300, 180, 0x2f8cff));
    assert(desktop_pointer_event(80, 140, 1) ==
           DESKTOP_ACTION_AUDIO_TEST_TONE);
    assert(desktop_pointer_event(80, 140, 0) == DESKTOP_ACTION_NONE);
    assert(desktop_window_open("CLOSEME", 200, 100, 180, 120, 0x2b6cb5));
    assert(desktop_pointer_event(208, 108, 1) == DESKTOP_ACTION_REDRAW);
    assert(!desktop_window_is_open("CLOSEME"));
    desktop_draw(2048);

    puts("desktop window tests passed");
    return 0;
}
