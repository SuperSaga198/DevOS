#ifndef DEVOS_TIMER_H
#define DEVOS_TIMER_H

#include <stdint.h>

#define TIMER_TICKS_PER_SECOND 100

void timer_init(void);
uintptr_t timer_irq_handler(uintptr_t *interrupted_stack);
uint64_t timer_ticks(void);

#endif
