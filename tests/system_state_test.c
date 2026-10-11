#include "system.h"

#include <stdio.h>
#include <string.h>

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "system-state test failed: %s\n", message);
        return 0;
    }
    return 1;
}

int main(void)
{
    devos_system_init();

    if (!expect(devos_process_count() == 2, "kernel component count")) {
        return 1;
    }

    struct devos_process processes[16];
    size_t count = 0;
    if (!expect(devos_process_snapshot(processes, 16, &count) != 0,
                "snapshot generation")) {
        return 1;
    }
    if (!expect(count == 2, "snapshot contains registered components")) {
        return 1;
    }

    if (!expect(strcmp(processes[0].name, "kernel") == 0, "kernel process registration")) {
        return 1;
    }

    if (!expect(devos_process_set_state(processes[0].pid, DEVOS_PROCESS_RUNNING),
                "state update")) {
        return 1;
    }

    struct devos_service_status status;
    if (!expect(devos_service_status(&status) != 0, "service status request")) {
        return 1;
    }
    if (!expect(status.component_registry_ready == DEVOS_SERVICE_READY &&
                status.storage_ready == DEVOS_SERVICE_OFF &&
                status.network_ready == DEVOS_SERVICE_OFF &&
                status.audio_ready == DEVOS_SERVICE_OFF &&
                status.desktop_ready == DEVOS_SERVICE_OFF,
                "unavailable services remain offline")) {
        return 1;
    }
    if (!expect(devos_service_set_state(DEVOS_SERVICE_STORAGE,
                                        DEVOS_SERVICE_READY) &&
                devos_service_set_state(DEVOS_SERVICE_DESKTOP,
                                        DEVOS_SERVICE_DEGRADED),
                "service state updates")) {
        return 1;
    }
    if (!expect(devos_service_status(&status) &&
                status.storage_ready == DEVOS_SERVICE_READY &&
                status.desktop_ready == DEVOS_SERVICE_DEGRADED &&
                status.audio_ready == DEVOS_SERVICE_OFF,
                "service status reflects registered state")) {
        return 1;
    }

    puts("system state tests passed");
    return 0;
}
