#include "keyboard.h"

#include <stddef.h>

#define KEY_QUEUE_SIZE 64
#define KEY_QUEUE_MASK (KEY_QUEUE_SIZE - 1)

static volatile uint16_t key_queue[KEY_QUEUE_SIZE];
static volatile uint8_t key_head;
static volatile uint8_t key_tail;
static uint8_t left_shift_down;
static uint8_t right_shift_down;
static uint8_t left_control_down;
static uint8_t right_control_down;
static uint8_t caps_lock;
static uint8_t extended_scancode;

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static const char unshifted[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0a] = '9', [0x0b] = '0', [0x0c] = '-', [0x0d] = '=',
    [0x0e] = '\b', [0x0f] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1a] = '[', [0x1b] = ']',
    [0x1c] = '\n',
    [0x1e] = 'a', [0x1f] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2b] = '\\',
    [0x2c] = 'z', [0x2d] = 'x', [0x2e] = 'c', [0x2f] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
    [0x34] = '.', [0x35] = '/', [0x39] = ' ',
};

static const char shifted[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0a] = '(', [0x0b] = ')', [0x0c] = '_', [0x0d] = '+',
    [0x0e] = '\b', [0x0f] = '\t',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1a] = '{', [0x1b] = '}',
    [0x1c] = '\n',
    [0x1e] = 'A', [0x1f] = 'S', [0x20] = 'D', [0x21] = 'F',
    [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
    [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2b] = '|',
    [0x2c] = 'Z', [0x2d] = 'X', [0x2e] = 'C', [0x2f] = 'V',
    [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
    [0x34] = '>', [0x35] = '?', [0x39] = ' ',
};

static int controller_wait_input_clear(void)
{
    for (uint32_t attempt = 0; attempt < 100000; ++attempt) {
        const uint8_t status = inb(0x64);
        if ((status & 0x02) == 0) {
            return 1;
        }
        __asm__ volatile ("pause");
    }
    return 0;
}

static int enable_scanning(void)
{
    if (!controller_wait_input_clear()) {
        return 0;
    }
    const uint8_t command = 0xf4;
    outb(0x60, command);

    for (uint32_t attempt = 0; attempt < 100000; ++attempt) {
        const uint8_t status = inb(0x64);
        if ((status & 0x01) != 0) {
            return inb(0x60) == 0xfa;
        }
        __asm__ volatile ("pause");
    }
    return 0;
}

static void queue_key(uint16_t key)
{
    const uint8_t next = (uint8_t)((key_head + 1) & KEY_QUEUE_MASK);
    if (next == key_tail) {
        return;
    }
    key_queue[key_head] = key;
    key_head = next;
}

int keyboard_init(void)
{
    key_head = 0;
    key_tail = 0;
    left_shift_down = 0;
    right_shift_down = 0;
    left_control_down = 0;
    right_control_down = 0;
    caps_lock = 0;
    extended_scancode = 0;

    if (controller_wait_input_clear()) {
        const uint8_t command = 0xae;
        outb(0x64, command);
    }
    return enable_scanning();
}

void keyboard_feed_scancode(uint8_t scancode)
{
    if (scancode == 0xe0 || scancode == 0xe1) {
        extended_scancode = 1;
        return;
    }
    if (extended_scancode) {
        extended_scancode = 0;
        if ((scancode & 0x7f) == 0x1d) {
            right_control_down = (scancode & 0x80) == 0;
        }
        return;
    }

    const uint8_t released = (scancode & 0x80) != 0;
    const uint8_t code = scancode & 0x7f;
    if (code == 0x2a) {
        left_shift_down = !released;
        return;
    }
    if (code == 0x36) {
        right_shift_down = !released;
        return;
    }
    if (code == 0x1d) {
        left_control_down = !released;
        return;
    }
    if (code == 0x3a) {
        if (!released) {
            caps_lock = !caps_lock;
        }
        return;
    }
    if (released || code >= sizeof(unshifted)) {
        return;
    }

    const char base = unshifted[code];
    if ((left_control_down || right_control_down) &&
        base >= 'a' && base <= 'z') {
        queue_key((uint16_t)(base - 'a' + 1));
        return;
    }
    if (base == '\n') {
        queue_key(KEYBOARD_KEY_ENTER);
    } else if (base == '\b') {
        queue_key(KEYBOARD_KEY_BACKSPACE);
    } else if (base != '\0') {
        const uint8_t is_letter = base >= 'a' && base <= 'z';
        const uint8_t shift_down = left_shift_down || right_shift_down;
        const uint8_t use_shifted =
            is_letter ? (shift_down != caps_lock) : shift_down;
        queue_key((uint16_t)(use_shifted ? shifted[code] : base));
    }
}

int keyboard_get_key(uint16_t *key)
{
    if (key == NULL || key_tail == key_head) {
        return 0;
    }
    *key = key_queue[key_tail];
    key_tail = (uint8_t)((key_tail + 1) & KEY_QUEUE_MASK);
    return 1;
}

void keyboard_irq_handler(void)
{
    keyboard_feed_scancode(inb(0x60));
    outb(0x20, 0x20);
}
