#pragma once

#include <coro/coro.h>
#include <core/types.h>

/* >>> mel_coro generated frames — managed region, do not edit >>> */
typedef struct Mel_Coro_Frame_ticker
{
    i32 state;
    i32 frames;
    i32 amplitude;
    int x;
    i32 t;
    i32 value;
    i32 __ret;
} Mel_Coro_Frame_ticker;

#define MEL_CORO_LAYOUT_HASH_ticker 0xf05a4e1442e2d8adull

Mel_Coro_Suspended ticker__resume(Mel_Coro_Frame_ticker* __f, int* __f_out);

/* <<< mel_coro generated frames <<< */

mel_coro(ticker, (i32 frames, i32 amplitude), i32)
{
    int x = 0;
    mel_coro_yield(x);
    for (i32 t = x; t < frames; t++)
    {
        i32 value = (amplitude * t) / frames;
        mel_coro_yield(value);
    }
    mel_coro_return(amplitude);
}
