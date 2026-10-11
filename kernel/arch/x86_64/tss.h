#ifndef DEVOS_TSS_H
#define DEVOS_TSS_H

#include <stdint.h>

void tss_init(void);
void tss_set_kernel_stack(uintptr_t stack_top);

#endif
