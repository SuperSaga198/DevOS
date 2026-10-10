#ifndef DEVOS_MOUSE_H
#define DEVOS_MOUSE_H

#include <stdint.h>

struct mouse_event {
    int16_t delta_x;
    int16_t delta_y;
    uint8_t buttons;
};

int mouse_init(void);
int mouse_is_available(void);
void mouse_feed_byte(uint8_t byte);
int mouse_get_event(struct mouse_event *event);
uint32_t mouse_packet_count(void);
void mouse_irq_handler(void);

#endif
