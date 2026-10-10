#ifndef DEVOS_TIMER_H
#define DEVOS_TIMER_H

#include <stdint.h>

#define TIMER_TICKS_PER_SECOND 100

void timer_init(void);
void timer_irq_handler(void);
uint64_t timer_ticks(void);

#endif
