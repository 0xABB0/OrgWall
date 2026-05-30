#pragma once

#include <continuation/cont.h>
#include <core/types.h>

/* >>> mel_cont generated frames — managed region, do not edit >>> */
typedef struct Mel_Cont_Frame_ticker
{
    i32 state;
    i32 frames;
    i32 amplitude;
    int x;
    i32 t;
    i32 value;
    i32 __ret;
} Mel_Cont_Frame_ticker;

#define MEL_CONT_LAYOUT_HASH_ticker 0xf05a4e1442e2d8adull

Mel_Cont_Suspended ticker__resume(Mel_Cont_Frame_ticker* __f, int* __f_out);

/* <<< mel_cont generated frames <<< */

mel_cont(ticker, (i32 frames, i32 amplitude), i32)
{
  int x = 0;
  mel_cont_yield(x);
    for (i32 t = x; t < frames; t++)
    {
        i32 value = (amplitude * t) / frames;
        mel_cont_yield(value);
    }
    mel_cont_return(amplitude);
}
