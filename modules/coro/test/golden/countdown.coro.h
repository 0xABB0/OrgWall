#pragma once

#include <coro/coro.h>
#include <core/types.h>

/* >>> mel_coro generated frames — managed region, do not edit >>> */
typedef struct Mel_Coro_Frame_countdown
{
    i32 state;
    i32 from;
} Mel_Coro_Frame_countdown;

#define MEL_CORO_LAYOUT_HASH_countdown 0xbb765f03dac7638eull

Mel_Coro_Suspended countdown__resume(Mel_Coro_Frame_countdown* __f);

/* <<< mel_coro generated frames <<< */

mel_coro(countdown, (i32 from), void)
{
    while (from > 0)
    {
        mel_coro_yield();
        from--;
    }
}
