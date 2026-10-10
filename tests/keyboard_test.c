#include "../kernel/arch/x86_64/keyboard.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint16_t next_key(void)
{
    uint16_t key = 0;
    assert(keyboard_get_key(&key));
    return key;
}

int main(void)
{
    uint16_t key;

    keyboard_feed_scancode(0x1e);
    assert(next_key() == 'a');

    keyboard_feed_scancode(0x2a);
    keyboard_feed_scancode(0x36);
    keyboard_feed_scancode(0xaa);
    keyboard_feed_scancode(0x1e);
    keyboard_feed_scancode(0xb6);
    assert(next_key() == 'A');

    keyboard_feed_scancode(0x3a);
    keyboard_feed_scancode(0x1e);
    assert(next_key() == 'A');
    keyboard_feed_scancode(0x2a);
    keyboard_feed_scancode(0x1e);
    keyboard_feed_scancode(0xaa);
    assert(next_key() == 'a');
    keyboard_feed_scancode(0x3a);

    keyboard_feed_scancode(0x02);
    keyboard_feed_scancode(0x2a);
    keyboard_feed_scancode(0x02);
    keyboard_feed_scancode(0xaa);
    assert(next_key() == '1');
    assert(next_key() == '!');

    keyboard_feed_scancode(0x1c);
    keyboard_feed_scancode(0x0e);
    assert(next_key() == KEYBOARD_KEY_ENTER);
    assert(next_key() == KEYBOARD_KEY_BACKSPACE);

    keyboard_feed_scancode(0xe0);
    keyboard_feed_scancode(0x48);
    assert(!keyboard_get_key(&key));

    for (uint32_t index = 0; index < 64; ++index) {
        keyboard_feed_scancode(0x1e);
    }
    for (uint32_t index = 0; index < 63; ++index) {
        assert(next_key() == 'a');
    }
    assert(!keyboard_get_key(&key));

    puts("keyboard tests passed");
    return 0;
}
