#include "mouse.h"

#include <stddef.h>

#define MOUSE_QUEUE_SIZE 32
#define MOUSE_QUEUE_MASK (MOUSE_QUEUE_SIZE - 1)

#define PS2_STATUS 0x64
#define PS2_COMMAND 0x64
#define PS2_DATA 0x60
#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_INPUT_FULL 0x02
#define PS2_STATUS_AUXILIARY 0x20
#define PS2_COMMAND_ENABLE_AUXILIARY 0xa8
#define PS2_COMMAND_READ_CONFIGURATION 0x20
#define PS2_COMMAND_WRITE_CONFIGURATION 0x60
#define PS2_COMMAND_WRITE_AUXILIARY 0xd4
#define PS2_CONFIG_IRQ_AUXILIARY 0x02
#define PS2_CONFIG_DISABLE_AUXILIARY 0x20
#define MOUSE_COMMAND_DEFAULTS 0xf6
#define MOUSE_COMMAND_ENABLE 0xf4
#define MOUSE_RESPONSE_ACK 0xfa
#define MOUSE_STATUS_OVERFLOW_X 0x40
#define MOUSE_STATUS_OVERFLOW_Y 0x80
#define MOUSE_STATUS_FIRST_BYTE 0x08
#define MOUSE_STATUS_BUTTONS 0x07
#define MOUSE_POLL_LIMIT 100000U

static volatile struct mouse_event event_queue[MOUSE_QUEUE_SIZE];
static volatile uint8_t event_head;
static volatile uint8_t event_tail;
static uint8_t packet[3];
static uint8_t packet_position;
static uint32_t completed_packets;
static uint8_t initialized;

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static int controller_wait_input_clear(void)
{
    for (uint32_t attempt = 0; attempt < MOUSE_POLL_LIMIT; ++attempt) {
        if ((inb(PS2_STATUS) & PS2_STATUS_INPUT_FULL) == 0) {
            return 1;
        }
        __asm__ volatile ("pause");
    }
    return 0;
}

static int controller_read(uint8_t *value)
{
    for (uint32_t attempt = 0; attempt < MOUSE_POLL_LIMIT; ++attempt) {
        if ((inb(PS2_STATUS) & PS2_STATUS_OUTPUT_FULL) != 0) {
            *value = inb(PS2_DATA);
            return 1;
        }
        __asm__ volatile ("pause");
    }
    return 0;
}

static int controller_command(uint8_t command)
{
    if (!controller_wait_input_clear()) {
        return 0;
    }
    outb(PS2_COMMAND, command);
    return 1;
}

static int mouse_send_command(uint8_t command)
{
    if (!controller_command(PS2_COMMAND_WRITE_AUXILIARY) ||
        !controller_wait_input_clear()) {
        return 0;
    }
    outb(PS2_DATA, command);

    for (uint32_t attempt = 0; attempt < MOUSE_POLL_LIMIT; ++attempt) {
        const uint8_t status = inb(PS2_STATUS);
        if ((status & PS2_STATUS_OUTPUT_FULL) != 0) {
            const uint8_t response = inb(PS2_DATA);
            if ((status & PS2_STATUS_AUXILIARY) != 0) {
                return response == MOUSE_RESPONSE_ACK;
            }
        }
        __asm__ volatile ("pause");
    }
    return 0;
}

int mouse_init(void)
{
    event_head = 0;
    event_tail = 0;
    packet_position = 0;
    completed_packets = 0;
    initialized = 0;

    if (!controller_command(PS2_COMMAND_ENABLE_AUXILIARY) ||
        !controller_command(PS2_COMMAND_READ_CONFIGURATION)) {
        return 0;
    }
    uint8_t configuration;
    if (!controller_read(&configuration)) {
        return 0;
    }
    configuration = (uint8_t)((configuration |
                               PS2_CONFIG_IRQ_AUXILIARY) &
                              ~PS2_CONFIG_DISABLE_AUXILIARY);
    if (!controller_command(PS2_COMMAND_WRITE_CONFIGURATION) ||
        !controller_wait_input_clear()) {
        return 0;
    }
    outb(PS2_DATA, configuration);

    if (!mouse_send_command(MOUSE_COMMAND_DEFAULTS) ||
        !mouse_send_command(MOUSE_COMMAND_ENABLE)) {
        return 0;
    }
    initialized = 1;
    return 1;
}

int mouse_is_available(void)
{
    return initialized;
}

void mouse_feed_byte(uint8_t byte)
{
    if (packet_position == 0) {
        if ((byte & MOUSE_STATUS_FIRST_BYTE) == 0) {
            return;
        }
        packet[packet_position++] = byte;
        return;
    }
    packet[packet_position++] = byte;
    if (packet_position != sizeof(packet)) {
        return;
    }
    packet_position = 0;

    if ((packet[0] & (MOUSE_STATUS_OVERFLOW_X |
                      MOUSE_STATUS_OVERFLOW_Y)) != 0) {
        return;
    }
    ++completed_packets;
    const uint8_t next = (uint8_t)((event_head + 1) & MOUSE_QUEUE_MASK);
    if (next == event_tail) {
        return;
    }
    event_queue[event_head] = (struct mouse_event) {
        .delta_x = (int8_t)packet[1],
        .delta_y = -(int16_t)(int8_t)packet[2],
        .buttons = packet[0] & MOUSE_STATUS_BUTTONS,
    };
    event_head = next;
}

int mouse_get_event(struct mouse_event *event)
{
    if (event == NULL || event_tail == event_head) {
        return 0;
    }
    *event = event_queue[event_tail];
    event_tail = (uint8_t)((event_tail + 1) & MOUSE_QUEUE_MASK);
    return 1;
}

uint32_t mouse_packet_count(void)
{
    return completed_packets;
}

void mouse_irq_handler(void)
{
    const uint8_t status = inb(PS2_STATUS);
    if ((status & PS2_STATUS_OUTPUT_FULL) != 0 &&
        (status & PS2_STATUS_AUXILIARY) != 0) {
        mouse_feed_byte(inb(PS2_DATA));
    }
    outb(0xa0, 0x20);
    outb(0x20, 0x20);
}
