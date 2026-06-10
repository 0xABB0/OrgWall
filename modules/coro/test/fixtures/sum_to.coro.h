#pragma once

#include <coro/coro.h>
#include <core/types.h>

/* >>> mel_coro generated frames — managed region, do not edit >>> */
typedef struct Mel_Coro_Frame_sum_to
{
    i32 state;
    i64 n;
    i64 acc;
    i32 i;
    i64 __ret;
} Mel_Coro_Frame_sum_to;

#define MEL_CORO_LAYOUT_HASH_sum_to 0x1851f9ec55aa97c5ull

Mel_Coro_Suspended sum_to__resume(Mel_Coro_Frame_sum_to* __f, long long* __f_out);

/* <<< mel_coro generated frames <<< */

mel_coro(sum_to, (i64 n), i64)
{
    i64 acc = 0;
    for (i32 i = 0; i < n; i++)
    {
        acc += i;
        mel_coro_yield(acc);
    }
    mel_coro_return(acc);
}
