#include "../kernel/core/framebuffer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

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
    desktop_draw(2048);
    static uint32_t menu_frame[640 * 480];
    memcpy(menu_frame, screen, sizeof(screen));
    assert(desktop_animation_step());
    desktop_draw(2048);
    assert(memcmp(menu_frame, screen, sizeof(screen)) != 0);
    for (size_t frame = 1; frame < 8; ++frame) {
        assert(desktop_animation_step());
    }
    desktop_draw(2048);
    assert(!desktop_animation_step());
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
    desktop_draw(2048);
    static uint32_t window_frame[640 * 480];
    memcpy(window_frame, screen, sizeof(screen));
    assert(desktop_animation_step());
    desktop_draw(2048);
    assert(memcmp(window_frame, screen, sizeof(screen)) != 0);
    for (size_t frame = 1; frame < 8; ++frame) {
        assert(desktop_animation_step());
    }
    desktop_draw(2048);
    assert(!desktop_animation_step());
    assert(desktop_pointer_event(80, 140, 1) ==
           DESKTOP_ACTION_AUDIO_TEST_TONE);
    assert(desktop_pointer_event(80, 140, 0) == DESKTOP_ACTION_NONE);
    struct desktop_runtime_status status = {
        .managed_pages = 4096,
        .free_pages = 2048,
        .udp_echoes = 3,
        .tcp_echoes = 2,
        .tcp_segments = 7,
        .network_ready = 1,
        .storage_ready = 1,
        .audio_ready = 1,
    };
    desktop_set_runtime_status(&status);
    assert(desktop_window_open("NETWORK", 50, 50, 300, 180, 0xf29f05));
    desktop_draw(2048);
    static uint32_t online_screen[640 * 480];
    memcpy(online_screen, screen, sizeof(screen));
    status.network_ready = 0;
    status.gateway_reachable = 0;
    desktop_set_runtime_status(&status);
    desktop_draw(2048);
    assert(screen[0] == 0x6c8cff);
    assert(screen[427 * 640] == 0x111827);
    assert(screen[438 * 640 + 16] == 0x202b3e);
    assert(memcmp(online_screen, screen, sizeof(screen)) != 0);
    status.network_ready = 1;
    desktop_set_runtime_status(&status);
    assert(desktop_pointer_event(80, 180, 1) ==
           DESKTOP_ACTION_NETWORK_PING);
    assert(desktop_pointer_event(80, 180, 0) == DESKTOP_ACTION_NONE);
    status.gateway_reachable = 1;
    desktop_set_runtime_status(&status);
    assert(desktop_pointer_event(80, 180, 1) == DESKTOP_ACTION_REDRAW);
    assert(desktop_pointer_event(80, 180, 0) == DESKTOP_ACTION_NONE);
    assert(desktop_window_open("CLOSEME", 200, 100, 180, 120, 0x2b6cb5));
    assert(desktop_pointer_event(208, 108, 1) == DESKTOP_ACTION_REDRAW);
    assert(!desktop_window_is_open("CLOSEME"));
    desktop_draw(2048);
    static uint32_t closing_frame[640 * 480];
    memcpy(closing_frame, screen, sizeof(screen));
    assert(desktop_animation_step());
    desktop_draw(2048);
    assert(memcmp(closing_frame, screen, sizeof(screen)) != 0);
    for (size_t frame = 1; frame < 8; ++frame) {
        assert(desktop_animation_step());
    }
    desktop_draw(2048);
    assert(!desktop_animation_step());
    assert(desktop_pointer_event(208, 108, 0) == DESKTOP_ACTION_NONE);
    assert(desktop_pointer_event(300, 450, 1) == DESKTOP_ACTION_REDRAW);
    assert(desktop_pointer_event(300, 450, 0) == DESKTOP_ACTION_NONE);
    assert(desktop_window_is_open("SETTINGS"));
    desktop_draw(2048);
    for (size_t frame = 0; frame < 8; ++frame) {
        assert(desktop_animation_step());
    }
    desktop_draw(2048);
    assert(desktop_pointer_event(330, 187, 1) ==
           DESKTOP_ACTION_WALLPAPER_BASE + 1);
    assert(desktop_wallpaper_index() == 1);
    assert(desktop_pointer_event(330, 187, 0) == DESKTOP_ACTION_NONE);
    desktop_draw(2048);
    assert(screen[3 * 640] == 0x102422);
    assert(desktop_animations_enabled());
    assert(desktop_pointer_event(500, 390, 1) ==
           DESKTOP_ACTION_ANIMATION_TOGGLE);
    assert(!desktop_animations_enabled());
    assert(desktop_pointer_event(500, 390, 0) == DESKTOP_ACTION_NONE);
    assert(desktop_pointer_event(500, 390, 1) ==
           DESKTOP_ACTION_ANIMATION_TOGGLE);
    assert(desktop_animations_enabled());

    puts("desktop window tests passed");
    return 0;
}
