#ifndef DEVOS_SERIAL_H
#define DEVOS_SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_enable_interrupts(void);
void serial_write(const char *text);
void serial_irq_handler(void);
int serial_get_char(uint8_t *character);

#endif
