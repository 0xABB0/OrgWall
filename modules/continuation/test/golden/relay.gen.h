#pragma once

#include <continuation/abi.h>
#include <core/types.h>

typedef struct Mel_Cont_Frame_child_seq
{
    i32 state;
    i32 base;
    i32 __ret;
} Mel_Cont_Frame_child_seq;

#define MEL_CONT_LAYOUT_HASH_child_seq 0x5fe3ae7255e012a5ull

Mel_Cont_Suspended child_seq__resume(Mel_Cont_Frame_child_seq* __f, int* __f_out);

typedef struct Mel_Cont_Frame_relay
{
    i32 state;
    i32 base;
    Mel_Cont_Frame_child_seq c;
    i32 __ret;
} Mel_Cont_Frame_relay;

#define MEL_CONT_LAYOUT_HASH_relay 0xa4b7dffe7272b83dull

Mel_Cont_Suspended relay__resume(Mel_Cont_Frame_relay* __f, int* __f_out);

