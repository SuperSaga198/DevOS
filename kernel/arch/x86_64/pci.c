#include "pci.h"

#include <stddef.h>

#define PCI_CONFIG_ADDRESS 0x0cf8
#define PCI_CONFIG_DATA 0x0cfc
#define PCI_CLASS_BRIDGE 0x06
#define PCI_SUBCLASS_PCI_BRIDGE 0x04

static uint8_t visited_buses[256];
static uint32_t devices;
static uint32_t ide_controllers;

static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t config_read(uint8_t bus, uint8_t slot, uint8_t function,
                            uint8_t offset)
{
    const uint32_t address = 0x80000000U |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)function << 8) |
        ((uint32_t)offset & 0xfcU);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void scan_bus(uint8_t bus, uint8_t pending_buses[256],
                     size_t *pending_count)
{
    for (uint8_t slot = 0; slot < 32; ++slot) {
        const uint32_t function_zero =
            config_read(bus, slot, 0, 0x00);
        if ((function_zero & 0xffffU) == 0xffffU) {
            continue;
        }

        const uint8_t header_type =
            (uint8_t)(config_read(bus, slot, 0, 0x0c) >> 16);
        const uint8_t function_count =
            (header_type & 0x80U) != 0 ? 8 : 1;
        for (uint8_t function = 0; function < function_count; ++function) {
            const uint32_t identity =
                config_read(bus, slot, function, 0x00);
            if ((identity & 0xffffU) == 0xffffU) {
                continue;
            }

            const uint32_t class_register =
                config_read(bus, slot, function, 0x08);
            const uint8_t class_code = (uint8_t)(class_register >> 24);
            const uint8_t subclass = (uint8_t)(class_register >> 16);
            ++devices;
            if (class_code == 0x01 && subclass == 0x01) {
                ++ide_controllers;
            }

            if (class_code == PCI_CLASS_BRIDGE &&
                subclass == PCI_SUBCLASS_PCI_BRIDGE &&
                *pending_count < 256) {
                const uint8_t secondary_bus =
                    (uint8_t)(config_read(bus, slot, function, 0x18) >> 8);
                if (secondary_bus != 0 && !visited_buses[secondary_bus]) {
                    visited_buses[secondary_bus] = 1;
                    pending_buses[(*pending_count)++] = secondary_bus;
                }
            }
        }
    }
}

void pci_init(void)
{
    for (size_t bus = 0; bus < sizeof(visited_buses); ++bus) {
        visited_buses[bus] = 0;
    }
    devices = 0;
    ide_controllers = 0;
    uint8_t pending_buses[256];
    size_t pending_count = 1;
    size_t next_bus = 0;
    visited_buses[0] = 1;
    pending_buses[0] = 0;
    while (next_bus < pending_count) {
        scan_bus(pending_buses[next_bus++], pending_buses, &pending_count);
    }
}

uint32_t pci_device_count(void)
{
    return devices;
}

uint32_t pci_ide_controller_count(void)
{
    return ide_controllers;
}
