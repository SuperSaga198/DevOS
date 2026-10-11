#include "timer.h"

#include "scheduler.h"

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

uintptr_t timer_irq_handler(uintptr_t *interrupted_stack)
{
    ++ticks;
    outb(0x20, 0x20);
    return scheduler_schedule((uintptr_t)interrupted_stack);
}

uint64_t timer_ticks(void)
{
    return ticks;
}
