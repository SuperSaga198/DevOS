#include "serial.h"

#include <stddef.h>

#define SERIAL_QUEUE_SIZE 128
#define SERIAL_QUEUE_MASK (SERIAL_QUEUE_SIZE - 1)

static volatile uint8_t receive_queue[SERIAL_QUEUE_SIZE];
static volatile uint8_t receive_head;
static volatile uint8_t receive_tail;

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void serial_init(void)
{
    receive_head = 0;
    receive_tail = 0;

    outb(0x3f9, 0x00);
    outb(0x3fb, 0x80);
    outb(0x3f8, 0x01);
    outb(0x3f9, 0x00);
    outb(0x3fb, 0x03);
    outb(0x3fa, 0xc7);
    outb(0x3fc, 0x0b);
}

void serial_enable_interrupts(void)
{
    outb(0x3f9, 0x01);
}

void serial_write(const char *text)
{
    if (text == NULL) {
        return;
    }
    while (*text != '\0') {
        while ((inb(0x3fd) & 0x20) == 0) {
            __asm__ volatile ("pause");
        }
        outb(0x3f8, (uint8_t)*text++);
    }
}

void serial_irq_handler(void)
{
    while ((inb(0x3fd) & 0x01) != 0) {
        const uint8_t next = (uint8_t)((receive_head + 1) & SERIAL_QUEUE_MASK);
        const uint8_t value = inb(0x3f8);
        if (next != receive_tail) {
            receive_queue[receive_head] = value;
            receive_head = next;
        }
    }
    outb(0x20, 0x20);
}

int serial_get_char(uint8_t *character)
{
    if (character == NULL || receive_tail == receive_head) {
        return 0;
    }
    *character = receive_queue[receive_tail];
    receive_tail = (uint8_t)((receive_tail + 1) & SERIAL_QUEUE_MASK);
    return 1;
}
