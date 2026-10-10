#ifndef DEVOS_INTERRUPTS_H
#define DEVOS_INTERRUPTS_H

void interrupts_init(void);
void keyboard_irq_stub(void);
void serial_irq_stub(void);
void exception_halt_stub(void);
void kernel_exception_halt(void);

#endif
