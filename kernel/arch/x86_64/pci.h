#ifndef DEVOS_PCI_H
#define DEVOS_PCI_H

#include <stdint.h>

void pci_init(void);
uint32_t pci_device_count(void);
uint32_t pci_ide_controller_count(void);

#endif
