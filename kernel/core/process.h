#ifndef DEVOS_PROCESS_H
#define DEVOS_PROCESS_H

#include <stddef.h>

int process_exec_elf(const char *name, const void *image, size_t image_size);

#endif
