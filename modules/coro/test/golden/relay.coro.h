#pragma once

#include <coro/coro.h>
#include <core/types.h>

/* >>> mel_coro generated frames — managed region, do not edit >>> */
typedef struct Mel_Coro_Frame_child_seq
{
    i32 state;
    i32 base;
    i32 __ret;
} Mel_Coro_Frame_child_seq;

#define MEL_CORO_LAYOUT_HASH_child_seq 0x5fe3ae7255e012a5ull

Mel_Coro_Suspended child_seq__resume(Mel_Coro_Frame_child_seq* __f, int* __f_out);

typedef struct Mel_Coro_Frame_relay
{
    i32 state;
    i32 base;
    Mel_Coro_Frame_child_seq c;
    i32 __ret;
} Mel_Coro_Frame_relay;

#define MEL_CORO_LAYOUT_HASH_relay 0xb832fbff54f56b4aull

Mel_Coro_Suspended relay__resume(Mel_Coro_Frame_relay* __f, int* __f_out);

/* <<< mel_coro generated frames <<< */

mel_coro(child_seq, (i32 base), i32)
{
    mel_coro_yield(base + 1);
    mel_coro_yield(base + 2);
    mel_coro_return(0);
}

mel_coro(relay, (i32 base), i32)
{
    mel_coro_yield(base);
    Mel_Coro_Frame_child_seq c = { 0 };
    c.base = base;
    mel_coro_await(c);
    mel_coro_yield(base + 100);
    mel_coro_return(base);
}
