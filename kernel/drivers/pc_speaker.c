#include "pc_speaker.h"

#include "timer.h"

#include <stddef.h>

#define PIT_INPUT_HZ 1193182u
#define MIN_FREQUENCY_HZ 20u
#define MAX_FREQUENCY_HZ 20000u
#define MAX_TONE_DURATION_MS 2000u

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

void pc_speaker_init(void)
{
    pc_speaker_stop();
}

int pc_speaker_frequency_divisor(uint32_t frequency_hz,
                                 uint16_t *divisor)
{
    if (divisor == NULL || frequency_hz < MIN_FREQUENCY_HZ ||
        frequency_hz > MAX_FREQUENCY_HZ) {
        return 0;
    }
    const uint32_t value = PIT_INPUT_HZ / frequency_hz;
    if (value == 0 || value > UINT16_MAX) {
        return 0;
    }
    *divisor = (uint16_t)value;
    return 1;
}

void pc_speaker_stop(void)
{
    outb(0x61, (uint8_t)(inb(0x61) & 0xfc));
}

int pc_speaker_play_tone(uint32_t frequency_hz, uint32_t duration_ms)
{
    uint16_t divisor = 0;
    if (!pc_speaker_frequency_divisor(frequency_hz, &divisor) ||
        duration_ms == 0 || duration_ms > MAX_TONE_DURATION_MS) {
        return 0;
    }

    outb(0x43, 0xb6);
    outb(0x42, (uint8_t)(divisor & 0xff));
    outb(0x42, (uint8_t)(divisor >> 8));
    outb(0x61, (uint8_t)(inb(0x61) | 0x03));

    const uint64_t duration_ticks =
        ((uint64_t)duration_ms * TIMER_TICKS_PER_SECOND + 999) / 1000;
    const uint64_t deadline = timer_ticks() + duration_ticks;
    while (timer_ticks() < deadline) {
        __asm__ volatile ("sti; hlt");
    }
    pc_speaker_stop();
    return 1;
}
