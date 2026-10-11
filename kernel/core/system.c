#include "system.h"

#include <string.h>

#define DEVOS_DEFAULT_PID_START 1000u

struct devos_slot {
    uint8_t occupied;
    struct devos_process process;
};

static struct devos_slot g_process_slots[DEVOS_MAX_PROCESSES];
static uint8_t g_system_initialized;
static struct devos_service_status g_services;

static uint32_t next_pid(void)
{
    static uint32_t next = DEVOS_DEFAULT_PID_START;
    return next++;
}

static int find_slot_by_pid(uint32_t pid, size_t *slot_index)
{
    for (size_t index = 0; index < DEVOS_MAX_PROCESSES; ++index) {
        if (g_process_slots[index].occupied &&
            g_process_slots[index].process.pid == pid) {
            *slot_index = index;
            return 1;
        }
    }
    return 0;
}

void devos_system_init(void)
{
    if (g_system_initialized) {
        return;
    }

    memset(g_process_slots, 0, sizeof(g_process_slots));
    memset(&g_services, 0, sizeof(g_services));
    g_system_initialized = 1;

    devos_process_register("kernel", 128u, 255u);
    devos_process_register("shell", 12u, 180u);
    g_services.component_registry_ready = DEVOS_SERVICE_READY;
}

int devos_process_register(const char *name, uint32_t memory_pages, uint32_t priority)
{
    if (name == NULL || *name == '\0') {
        return 0;
    }

    if (!g_system_initialized) {
        devos_system_init();
    }

    for (size_t index = 0; index < DEVOS_MAX_PROCESSES; ++index) {
        if (!g_process_slots[index].occupied) {
            struct devos_process *process = &g_process_slots[index].process;
            memset(process, 0, sizeof(*process));
            process->pid = next_pid();
            process->priority = priority;
            process->memory_pages = memory_pages;
            process->state = DEVOS_PROCESS_READY;
            process->created_ticks = 0;
            process->cpu_ticks = 0;
            memset(process->name, 0, sizeof(process->name));
            for (size_t name_index = 0; name[name_index] != '\0' &&
                 name_index < sizeof(process->name) - 1; ++name_index) {
                process->name[name_index] = name[name_index];
            }
            g_process_slots[index].occupied = 1;
            return 1;
        }
    }

    return 0;
}

int devos_process_set_state(uint32_t pid, uint8_t state)
{
    if (!g_system_initialized) {
        devos_system_init();
    }

    size_t slot_index = 0;
    if (!find_slot_by_pid(pid, &slot_index)) {
        return 0;
    }

    g_process_slots[slot_index].process.state = state;
    return 1;
}

size_t devos_process_count(void)
{
    if (!g_system_initialized) {
        devos_system_init();
    }

    size_t total = 0;
    for (size_t index = 0; index < DEVOS_MAX_PROCESSES; ++index) {
        if (g_process_slots[index].occupied) {
            ++total;
        }
    }
    return total;
}

int devos_process_snapshot(struct devos_process *buffer, size_t capacity, size_t *count)
{
    if (buffer == NULL || count == NULL || capacity == 0) {
        return 0;
    }

    if (!g_system_initialized) {
        devos_system_init();
    }

    size_t copied = 0;
    for (size_t index = 0; index < DEVOS_MAX_PROCESSES && copied < capacity; ++index) {
        if (g_process_slots[index].occupied) {
            buffer[copied++] = g_process_slots[index].process;
        }
    }

    *count = copied;
    return copied > 0;
}

int devos_service_status(struct devos_service_status *status)
{
    if (status == NULL) {
        return 0;
    }

    if (!g_system_initialized) {
        devos_system_init();
    }

    *status = g_services;
    return 1;
}

int devos_service_set_state(enum devos_service service, uint8_t state)
{
    if (!g_system_initialized) {
        devos_system_init();
    }
    if (state > DEVOS_SERVICE_DEGRADED) {
        return 0;
    }

    switch (service) {
    case DEVOS_SERVICE_STORAGE:
        g_services.storage_ready = state;
        return 1;
    case DEVOS_SERVICE_NETWORK:
        g_services.network_ready = state;
        return 1;
    case DEVOS_SERVICE_AUDIO:
        g_services.audio_ready = state;
        return 1;
    case DEVOS_SERVICE_DESKTOP:
        g_services.desktop_ready = state;
        return 1;
    default:
        return 0;
    }
}

const char *devos_service_state_name(uint8_t state)
{
    switch (state) {
    case DEVOS_SERVICE_READY:
        return "ready";
    case DEVOS_SERVICE_DEGRADED:
        return "degraded";
    case DEVOS_SERVICE_OFF:
    default:
        return "offline";
    }
}

const char *devos_process_state_name(uint8_t state)
{
    switch (state) {
    case DEVOS_PROCESS_READY:
        return "ready";
    case DEVOS_PROCESS_RUNNING:
        return "running";
    case DEVOS_PROCESS_BLOCKED:
        return "blocked";
    case DEVOS_PROCESS_EXITED:
        return "exited";
    case DEVOS_PROCESS_UNSET:
    default:
        return "unset";
    }
}
