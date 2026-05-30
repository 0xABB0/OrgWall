#pragma once

#include <continuation/abi.h>
#include <core/types.h>

typedef struct Mel_Cont_Frame_countdown
{
    i32 state;
    i32 from;
} Mel_Cont_Frame_countdown;

#define MEL_CONT_LAYOUT_HASH_countdown 0xbb765f03dac7638eull

Mel_Cont_Suspended countdown__resume(Mel_Cont_Frame_countdown* __f);

