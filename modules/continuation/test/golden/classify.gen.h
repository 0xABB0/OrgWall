#pragma once

#include <continuation/abi.h>
#include <core/types.h>

typedef struct Mel_Cont_Frame_classify
{
    i32 state;
    i32 n;
    i32 seen;
    i32 __ret;
} Mel_Cont_Frame_classify;

#define MEL_CONT_LAYOUT_HASH_classify 0x5cd88374c903e9a4ull

Mel_Cont_Suspended classify__resume(Mel_Cont_Frame_classify* __f, int* __f_out);

