#ifndef DEVOS_INTERRUPTS_H
#define DEVOS_INTERRUPTS_H

#include <stdint.h>

void interrupts_init(void);
void timer_irq_stub(void);
void keyboard_irq_stub(void);
void mouse_irq_stub(void);
void serial_irq_stub(void);
void syscall_interrupt_stub(void);
void exception_common_stub(void);
uintptr_t syscall_interrupt_handler(void *frame);
uintptr_t kernel_exception_report(uint64_t vector, uint64_t error_code,
                                  uint64_t instruction_pointer,
                                  uintptr_t *saved_frame);
#endif
