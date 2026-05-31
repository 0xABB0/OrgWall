#pragma once

#include <continuation/cont.h>
#include <core/types.h>

/* >>> mel_cont generated frames — managed region, do not edit >>> */
typedef struct Mel_Cont_Frame_countdown
{
    i32 state;
    i32 from;
} Mel_Cont_Frame_countdown;

#define MEL_CONT_LAYOUT_HASH_countdown 0xbb765f03dac7638eull

Mel_Cont_Suspended countdown__resume(Mel_Cont_Frame_countdown* __f);

/* <<< mel_cont generated frames <<< */

mel_cont(countdown, (i32 from), void)
{
    while (from > 0)
    {
        mel_cont_yield();
        from--;
    }
}
