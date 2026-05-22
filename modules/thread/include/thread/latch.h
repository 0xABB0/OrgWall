#pragma once

#include <core/types.h>

#include <stdatomic.h>

typedef struct Mel_Latch {
    _Atomic(u32) _count;
} Mel_Latch;

void mel_latch_init    (Mel_Latch* l, u32 count);
void mel_latch_count_down(Mel_Latch* l);
void mel_latch_wait    (Mel_Latch* l);
bool mel_latch_wait_for(Mel_Latch* l, i64 timeout_ns);
bool mel_latch_is_ready(const Mel_Latch* l);
