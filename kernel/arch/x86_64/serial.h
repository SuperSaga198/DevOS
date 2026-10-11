#ifndef DEVOS_SERIAL_H
#define DEVOS_SERIAL_H

#include <stdint.h>
#include <stddef.h>

void serial_init(void);
void serial_enable_interrupts(void);
void serial_write(const char *text);
void serial_write_n(const uint8_t *bytes, size_t length);
void serial_irq_handler(void);
int serial_get_char(uint8_t *character);

#endif
