#include "scheduler.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void task_entry(void *argument)
{
    (void)argument;
}

int main(void)
{
    scheduler_init();
    assert(!scheduler_create(NULL, task_entry, NULL));
    assert(!scheduler_create("invalid", NULL, NULL));
    assert(scheduler_create("worker-a", task_entry, NULL));
    assert(scheduler_create("worker-b", task_entry, NULL));
    assert(scheduler_create("worker-c", task_entry, NULL));

    struct scheduler_task_info tasks[SCHEDULER_MAX_TASKS];
    assert(scheduler_task_snapshot(tasks, SCHEDULER_MAX_TASKS) == 3);
    assert(strcmp(tasks[0].name, "worker-a") == 0);
    assert(tasks[0].state == SCHEDULER_TASK_READY);

    const uintptr_t idle_first = 0x10000;
    const uintptr_t task_a = scheduler_schedule(idle_first);
    assert(task_a != 0 && task_a != idle_first);
    assert(scheduler_schedule(0x20000) == idle_first);
    const uintptr_t idle_second = 0x11000;
    const uintptr_t task_b = scheduler_schedule(idle_second);
    assert(task_b != 0 && task_b != idle_second);
    assert(scheduler_schedule(0x21000) == idle_second);
    const uintptr_t task_c = scheduler_schedule(0x12000);
    assert(task_c != 0 && task_c != 0x12000);
    assert(scheduler_schedule(0x22000) == 0x12000);

    assert(scheduler_task_snapshot(tasks, SCHEDULER_MAX_TASKS) == 3);
    assert(tasks[0].ticks == 1);
    assert(tasks[1].ticks == 1);
    assert(tasks[2].ticks == 1);
    assert(scheduler_task_snapshot(tasks, 2) == 2);

    puts("scheduler tests passed");
    return 0;
}
