#ifndef DEVOS_KEYBOARD_H
#define DEVOS_KEYBOARD_H

#include <stdint.h>

#define KEYBOARD_KEY_ENTER 0x100
#define KEYBOARD_KEY_BACKSPACE 0x101

int keyboard_init(void);
void keyboard_feed_scancode(uint8_t scancode);
int keyboard_get_key(uint16_t *key);
void keyboard_irq_handler(void);

#endif
