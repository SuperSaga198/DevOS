#ifndef DEVOS_E1000_H
#define DEVOS_E1000_H

#include <stddef.h>
#include <stdint.h>

int e1000_init(void);
int e1000_is_ready(void);
const uint8_t *e1000_mac_address(void);
void e1000_poll(void);
int e1000_ping_gateway(void);
uint64_t e1000_received_frames(void);
uint64_t e1000_echo_replies(void);

#endif
