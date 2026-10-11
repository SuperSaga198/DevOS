#ifndef DEVOS_SCHEDULER_H
#define DEVOS_SCHEDULER_H

#include <stddef.h>
#include <stdint.h>

#define SCHEDULER_MAX_TASKS 8u
#define SCHEDULER_TASK_NAME_CAPACITY 24u

enum scheduler_task_state {
    SCHEDULER_TASK_READY,
    SCHEDULER_TASK_RUNNING,
    SCHEDULER_TASK_EXITED,
};

struct scheduler_task_info {
    char name[SCHEDULER_TASK_NAME_CAPACITY];
    enum scheduler_task_state state;
    uint64_t ticks;
    uint8_t is_user;
};

typedef void (*scheduler_task_entry)(void *argument);

void scheduler_init(void);
void scheduler_set_kernel_address_space(uint64_t address_space);
int scheduler_create(const char *name, scheduler_task_entry entry,
                     void *argument);
int scheduler_create_user(const char *name, uint64_t address_space,
                          uintptr_t entry_point, uintptr_t user_stack);
uintptr_t scheduler_schedule(uintptr_t interrupted_stack);
uintptr_t scheduler_exit_user(uintptr_t interrupted_stack);
uint64_t scheduler_current_address_space(void);
size_t scheduler_task_snapshot(struct scheduler_task_info *tasks,
                               size_t capacity);

#endif
