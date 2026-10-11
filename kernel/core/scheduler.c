#include "scheduler.h"

#include <stdint.h>

#define SCHEDULER_STACK_SIZE 16384u
#define INITIAL_RFLAGS 0x202u
#define USER_CODE_SELECTOR 0x23u
#define USER_DATA_SELECTOR 0x1bu

extern void tss_set_kernel_stack(uintptr_t stack_top) __attribute__((weak));
extern int memory_destroy_user_address_space(uint64_t root_physical)
    __attribute__((weak));

struct interrupt_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

struct scheduler_task {
    uint8_t used;
    enum scheduler_task_state state;
    char name[SCHEDULER_TASK_NAME_CAPACITY];
    scheduler_task_entry entry;
    void *argument;
    uintptr_t stack_pointer;
    uintptr_t kernel_stack_top;
    uint64_t address_space;
    uint64_t ticks;
    uint8_t is_user;
    _Alignas(16) uint8_t stack[SCHEDULER_STACK_SIZE];
};

static struct scheduler_task tasks[SCHEDULER_MAX_TASKS];
static int current_task = -1;
static int last_task = -1;
static uintptr_t idle_stack_pointer;
static uint16_t code_selector;
static uint16_t stack_selector;
static uint64_t kernel_address_space;

static void zero_bytes(void *buffer, size_t size)
{
    uint8_t *bytes = buffer;
    for (size_t index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void scheduler_task_returned(void)
{
    if (current_task >= 0) {
        tasks[current_task].state = SCHEDULER_TASK_EXITED;
        current_task = -2;
    }
    for (;;) {
        __asm__ volatile ("sti; hlt");
    }
}

static void scheduler_task_bootstrap(scheduler_task_entry entry,
                                     void *argument)
{
    entry(argument);
    scheduler_task_returned();
}

void scheduler_init(void)
{
    zero_bytes(tasks, sizeof(tasks));
    current_task = -1;
    last_task = -1;
    idle_stack_pointer = 0;
    __asm__ volatile ("mov %%cs, %0" : "=r"(code_selector));
    __asm__ volatile ("mov %%ss, %0" : "=r"(stack_selector));
}

void scheduler_set_kernel_address_space(uint64_t address_space)
{
    kernel_address_space = address_space;
}

static int scheduler_create_task(const char *name, scheduler_task_entry entry,
                                 void *argument, uint64_t address_space,
                                 uintptr_t entry_point, uintptr_t user_stack)
{
    const int is_user = address_space != 0;
    if (name == NULL || name[0] == '\0' ||
        (is_user ? (entry_point == 0 || user_stack == 0) : entry == NULL)) {
        return 0;
    }

    size_t slot = 0;
    while (slot < SCHEDULER_MAX_TASKS && tasks[slot].used) {
        ++slot;
    }
    if (slot == SCHEDULER_MAX_TASKS) {
        return 0;
    }

    struct scheduler_task *task = &tasks[slot];
    task->used = 1;
    task->state = SCHEDULER_TASK_READY;
    task->entry = entry;
    task->argument = argument;
    task->address_space = address_space;
    task->is_user = (uint8_t)is_user;
    task->ticks = 0;
    size_t name_index = 0;
    while (name[name_index] != '\0' &&
           name_index + 1 < sizeof(task->name)) {
        task->name[name_index] = name[name_index];
        ++name_index;
    }
    task->name[name_index] = '\0';

    uintptr_t stack_top = (uintptr_t)(task->stack + sizeof(task->stack));
    stack_top &= ~(uintptr_t)0xf;
    uint64_t *return_address = (uint64_t *)(stack_top - sizeof(uint64_t));
    *return_address = (uintptr_t)scheduler_task_returned;
    struct interrupt_frame *frame =
        (struct interrupt_frame *)(stack_top -
                                   sizeof(struct interrupt_frame));
    zero_bytes(frame, sizeof(*frame));
    frame->rdi = (uintptr_t)entry;
    frame->rsi = (uintptr_t)argument;
    frame->rip = (uintptr_t)scheduler_task_bootstrap;
    frame->cs = is_user ? USER_CODE_SELECTOR : code_selector;
    frame->rflags = INITIAL_RFLAGS;
    frame->rsp = is_user ? user_stack : (uintptr_t)return_address;
    frame->ss = is_user ? USER_DATA_SELECTOR : stack_selector;
    if (is_user) {
        frame->rdi = 0;
        frame->rsi = 0;
        frame->rip = entry_point;
    }
    task->stack_pointer = (uintptr_t)frame;
    task->kernel_stack_top = stack_top;
    return 1;
}

int scheduler_create(const char *name, scheduler_task_entry entry,
                     void *argument)
{
    return scheduler_create_task(name, entry, argument, 0, 0, 0);
}

int scheduler_create_user(const char *name, uint64_t address_space,
                          uintptr_t entry_point, uintptr_t user_stack)
{
    if (address_space == 0) {
        return 0;
    }
    return scheduler_create_task(name, NULL, NULL, address_space,
                                 entry_point, user_stack);
}

static int next_ready_task(void)
{
    const size_t start = last_task < 0 ? 0u :
                         ((size_t)last_task + 1) % SCHEDULER_MAX_TASKS;
    for (size_t offset = 0; offset < SCHEDULER_MAX_TASKS; ++offset) {
        const size_t index = (start + offset) % SCHEDULER_MAX_TASKS;
        if (tasks[index].used &&
            tasks[index].state == SCHEDULER_TASK_READY) {
            return (int)index;
        }
    }
    return -1;
}

uintptr_t scheduler_schedule(uintptr_t interrupted_stack)
{
    if (current_task >= 0) {
        struct scheduler_task *task = &tasks[current_task];
        task->stack_pointer = interrupted_stack;
        if (task->state == SCHEDULER_TASK_RUNNING) {
            task->state = SCHEDULER_TASK_READY;
            ++task->ticks;
        }
        last_task = current_task;
        const int was_user = task->is_user;
        current_task = -1;
        if (was_user) {
            __asm__ volatile ("mov %0, %%cr3" ::
                              "r"(kernel_address_space) : "memory");
        }
        if (idle_stack_pointer != 0) {
            return idle_stack_pointer;
        }
        return interrupted_stack;
    }

    const int was_exited = current_task == -2;
    current_task = -1;
    if (!was_exited) {
        idle_stack_pointer = interrupted_stack;
    }

    const int next = next_ready_task();
    if (next < 0) {
        return interrupted_stack;
    }
    current_task = next;
    last_task = next;
    tasks[next].state = SCHEDULER_TASK_RUNNING;
    if (tasks[next].is_user) {
        if (kernel_address_space == 0 || tss_set_kernel_stack == NULL) {
            tasks[next].state = SCHEDULER_TASK_READY;
            current_task = -1;
            return interrupted_stack;
        }
        tss_set_kernel_stack(tasks[next].kernel_stack_top);
        __asm__ volatile ("mov %0, %%cr3" ::
                          "r"(tasks[next].address_space) : "memory");
    }
    return tasks[next].stack_pointer;
}

uintptr_t scheduler_exit_user(uintptr_t interrupted_stack)
{
    if (current_task < 0 || !tasks[current_task].is_user ||
        idle_stack_pointer == 0) {
        return interrupted_stack;
    }
    tasks[current_task].state = SCHEDULER_TASK_EXITED;
    const uint64_t address_space = tasks[current_task].address_space;
    current_task = -2;
    __asm__ volatile ("mov %0, %%cr3" ::
                      "r"(kernel_address_space) : "memory");
    if (memory_destroy_user_address_space != NULL) {
        (void)memory_destroy_user_address_space(address_space);
    }
    return idle_stack_pointer;
}

uint64_t scheduler_current_address_space(void)
{
    if (current_task < 0 || !tasks[current_task].is_user) {
        return 0;
    }
    return tasks[current_task].address_space;
}

size_t scheduler_task_snapshot(struct scheduler_task_info *result,
                               size_t capacity)
{
    if (result == NULL || capacity == 0) {
        return 0;
    }
    size_t copied = 0;
    for (size_t index = 0; index < SCHEDULER_MAX_TASKS &&
                           copied < capacity; ++index) {
        if (!tasks[index].used) {
            continue;
        }
        struct scheduler_task_info *info = &result[copied++];
        size_t name_index = 0;
        while (tasks[index].name[name_index] != '\0' &&
               name_index + 1 < sizeof(info->name)) {
            info->name[name_index] = tasks[index].name[name_index];
            ++name_index;
        }
        info->name[name_index] = '\0';
        info->state = tasks[index].state;
        info->ticks = tasks[index].ticks;
        info->is_user = tasks[index].is_user;
    }
    return copied;
}
