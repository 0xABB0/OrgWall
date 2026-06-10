#pragma once

#include <coro/coro.h>
#include <core/types.h>

/* >>> mel_coro generated frames — managed region, do not edit >>> */
typedef struct Mel_Coro_Frame_repeat_sum
{
    i32 state;
    i32 n;
    i32 total;
    i32 k;
    i32 __ret;
} Mel_Coro_Frame_repeat_sum;

#define MEL_CORO_LAYOUT_HASH_repeat_sum 0x3bf8d897332b7217ull

Mel_Coro_Suspended repeat_sum__resume(Mel_Coro_Frame_repeat_sum* __f, int* __f_out);

/* <<< mel_coro generated frames <<< */

mel_coro(repeat_sum, (i32 n), i32)
{
    i32 total = 0;
    i32 k = 0;
    do
    {
        total += k;
        mel_coro_yield(total);
        k++;
    } while (k < n);
    mel_coro_return(total);
}
