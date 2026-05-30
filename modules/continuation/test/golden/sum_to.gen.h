#pragma once

#include <continuation/abi.h>
#include <core/types.h>

typedef struct Mel_Cont_Frame_sum_to
{
    i32 state;
    i64 n;
    i64 acc;
    i32 i;
    i64 __ret;
} Mel_Cont_Frame_sum_to;

#define MEL_CONT_LAYOUT_HASH_sum_to 0x1851f9ec55aa97c5ull

Mel_Cont_Suspended sum_to__resume(Mel_Cont_Frame_sum_to* __f, long long* __f_out);

