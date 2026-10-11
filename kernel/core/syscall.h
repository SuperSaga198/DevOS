#ifndef DEVOS_SYSCALL_H
#define DEVOS_SYSCALL_H

enum devos_syscall_number {
    DEVOS_SYSCALL_EXIT = 0,
    DEVOS_SYSCALL_WRITE = 1,
    DEVOS_SYSCALL_YIELD = 2,
};

#define DEVOS_SYSCALL_MAX_WRITE 256u

#endif
