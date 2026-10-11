#ifndef DEVOS_SYSTEM_H
#define DEVOS_SYSTEM_H

#include <stddef.h>
#include <stdint.h>

#define DEVOS_MAX_PROCESSES 32u

enum devos_process_state {
    DEVOS_PROCESS_UNSET = 0,
    DEVOS_PROCESS_READY = 1,
    DEVOS_PROCESS_RUNNING = 2,
    DEVOS_PROCESS_BLOCKED = 3,
    DEVOS_PROCESS_EXITED = 4,
};

enum devos_service_state {
    DEVOS_SERVICE_OFF = 0,
    DEVOS_SERVICE_READY = 1,
    DEVOS_SERVICE_DEGRADED = 2,
};

enum devos_service {
    DEVOS_SERVICE_STORAGE = 0,
    DEVOS_SERVICE_NETWORK = 1,
    DEVOS_SERVICE_AUDIO = 2,
    DEVOS_SERVICE_DESKTOP = 3,
};

struct devos_process {
    uint32_t pid;
    char name[20];
    uint32_t priority;
    uint64_t created_ticks;
    uint64_t cpu_ticks;
    uint32_t memory_pages;
    uint8_t state;
};

struct devos_service_status {
    uint8_t component_registry_ready;
    uint8_t storage_ready;
    uint8_t network_ready;
    uint8_t audio_ready;
    uint8_t desktop_ready;
};

void devos_system_init(void);
int devos_process_register(const char *name, uint32_t memory_pages, uint32_t priority);
int devos_process_set_state(uint32_t pid, uint8_t state);
size_t devos_process_count(void);
int devos_process_snapshot(struct devos_process *buffer, size_t capacity, size_t *count);
int devos_service_status(struct devos_service_status *status);
int devos_service_set_state(enum devos_service service, uint8_t state);
const char *devos_service_state_name(uint8_t state);
const char *devos_process_state_name(uint8_t state);

#endif
