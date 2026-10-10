#include "timer.h"

static volatile uint64_t ticks;

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

void timer_init(void)
{
    ticks = 0;
    const uint16_t divisor = (uint16_t)(1193182 / TIMER_TICKS_PER_SECOND);
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xff));
    outb(0x40, (uint8_t)(divisor >> 8));
}

void timer_irq_handler(void)
{
    ++ticks;
    outb(0x20, 0x20);
}

uint64_t timer_ticks(void)
{
    return ticks;
}
