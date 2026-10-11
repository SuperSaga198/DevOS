#include "pc_speaker.h"
#include "timer.h"

#include <assert.h>
#include <stdio.h>

uint64_t timer_ticks(void)
{
    return 0;
}

int main(void)
{
    uint16_t divisor = 0;
    assert(pc_speaker_frequency_divisor(440, &divisor));
    assert(divisor == 2711);
    assert(pc_speaker_frequency_divisor(20000, &divisor));
    assert(divisor == 59);
    assert(!pc_speaker_frequency_divisor(19, &divisor));
    assert(!pc_speaker_frequency_divisor(20001, &divisor));
    assert(!pc_speaker_frequency_divisor(440, NULL));

    puts("PC speaker range and PIT divisor tests passed");
    return 0;
}
