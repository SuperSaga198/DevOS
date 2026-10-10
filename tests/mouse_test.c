#include "../kernel/arch/x86_64/mouse.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void feed_packet(uint8_t status, uint8_t x, uint8_t y)
{
    mouse_feed_byte(status);
    mouse_feed_byte(x);
    mouse_feed_byte(y);
}

int main(void)
{
    struct mouse_event event;
    assert(!mouse_get_event(&event));

    mouse_feed_byte(0x00);
    mouse_feed_byte(0x03);
    assert(mouse_packet_count() == 0);
    assert(!mouse_get_event(&event));

    feed_packet(0x0d, 0x05, 0xfe);
    assert(mouse_packet_count() == 1);
    assert(mouse_get_event(&event));
    assert(event.delta_x == 5);
    assert(event.delta_y == 2);
    assert(event.buttons == 5);
    assert(!mouse_get_event(&event));

    feed_packet(0x08, 0xfb, 0x04);
    assert(mouse_get_event(&event));
    assert(event.delta_x == -5);
    assert(event.delta_y == -4);
    assert(event.buttons == 0);

    feed_packet(0x48, 0x7f, 0x7f);
    assert(mouse_packet_count() == 2);
    assert(!mouse_get_event(&event));

    for (uint8_t index = 0; index < 40; ++index) {
        feed_packet(0x08, index, 0);
    }
    assert(mouse_packet_count() == 42);
    size_t queued = 0;
    while (mouse_get_event(&event)) {
        assert(event.delta_x >= 0);
        assert(event.delta_y == 0);
        ++queued;
    }
    assert(queued == 31);
    assert(!mouse_get_event(&event));

    puts("PS/2 mouse packet tests passed");
    return 0;
}
