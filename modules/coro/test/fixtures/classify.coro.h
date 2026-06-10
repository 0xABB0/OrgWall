#pragma once

#include <coro/coro.h>
#include <core/types.h>

/* >>> mel_coro generated frames — managed region, do not edit >>> */
typedef struct Mel_Coro_Frame_classify
{
    i32 state;
    i32 n;
    i32 seen;
    i32 __ret;
} Mel_Coro_Frame_classify;

#define MEL_CORO_LAYOUT_HASH_classify 0x5cd88374c903e9a4ull

Mel_Coro_Suspended classify__resume(Mel_Coro_Frame_classify* __f, int* __f_out);

/* <<< mel_coro generated frames <<< */

mel_coro(classify, (i32 n), i32)
{
    i32 seen = 0;
    if (n > 0)
    {
        seen = 1;
        mel_coro_yield(seen);
    }
    else
    {
        seen = -1;
        mel_coro_yield(seen);
    }
    mel_coro_yield(seen + n);
    mel_coro_return(seen);
}
