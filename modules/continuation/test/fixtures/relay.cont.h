#pragma once

#include <continuation/cont.h>
#include <core/types.h>

/* >>> mel_cont generated frames — managed region, do not edit >>> */
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

/* <<< mel_cont generated frames <<< */

mel_cont(child_seq, (i32 base), i32)
{
    mel_cont_yield(base + 1);
    mel_cont_yield(base + 2);
    mel_cont_return(0);
}

mel_cont(relay, (i32 base), i32)
{
    mel_cont_yield(base);
    Mel_Cont_Frame_child_seq c = {0};
    c.base                     = base;
    mel_cont_await(c);
    mel_cont_yield(base + 100);
    mel_cont_return(base);
}
