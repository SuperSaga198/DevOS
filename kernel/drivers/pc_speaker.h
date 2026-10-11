#ifndef DEVOS_PC_SPEAKER_H
#define DEVOS_PC_SPEAKER_H

#include <stdint.h>

int pc_speaker_frequency_divisor(uint32_t frequency_hz,
                                 uint16_t *divisor);
void pc_speaker_init(void);
int pc_speaker_play_tone(uint32_t frequency_hz, uint32_t duration_ms);
void pc_speaker_stop(void);

#endif
