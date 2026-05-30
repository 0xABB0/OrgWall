#pragma once

#include <continuation/abi.h>
#include <core/types.h>

typedef struct Mel_Cont_Frame_repeat_sum
{
    i32 state;
    i32 n;
    i32 total;
    i32 k;
    i32 __ret;
} Mel_Cont_Frame_repeat_sum;

#define MEL_CONT_LAYOUT_HASH_repeat_sum 0x3bf8d897332b7217ull

Mel_Cont_Suspended repeat_sum__resume(Mel_Cont_Frame_repeat_sum* __f, int* __f_out);

