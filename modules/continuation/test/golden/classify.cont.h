#pragma once

#include <continuation/cont.h>
#include <core/types.h>

/* >>> mel_cont generated frames — managed region, do not edit >>> */
typedef struct Mel_Cont_Frame_classify
{
    i32 state;
    i32 n;
    i32 seen;
    i32 __ret;
} Mel_Cont_Frame_classify;

#define MEL_CONT_LAYOUT_HASH_classify 0x5cd88374c903e9a4ull

Mel_Cont_Suspended classify__resume(Mel_Cont_Frame_classify* __f, int* __f_out);

/* <<< mel_cont generated frames <<< */

mel_cont(classify, (i32 n), i32)
{
    i32 seen = 0;
    if (n > 0)
    {
        seen = 1;
        mel_cont_yield(seen);
    }
    else
    {
        seen = -1;
        mel_cont_yield(seen);
    }
    mel_cont_yield(seen + n);
    mel_cont_return(seen);
}
