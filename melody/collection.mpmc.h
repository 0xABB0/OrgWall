#pragma once

#include "core.types.h"
#include "allocator.fwd.h"
#include <stdatomic.h>

typedef struct {
    _Atomic(u64) sequence;
    void* data;
} Mel__Mpmc_Cell;

typedef struct Mel_Mpmc {
    _Alignas(64) _Atomic(u64) head;
    _Alignas(64) _Atomic(u64) tail;
    Mel__Mpmc_Cell* cells;
    u64 mask;
    const Mel_Alloc* alloc;
} Mel_Mpmc;

void mel_mpmc_init(Mel_Mpmc* q, u64 capacity, const Mel_Alloc* alloc);
void mel_mpmc_free(Mel_Mpmc* q);
bool mel_mpmc_push(Mel_Mpmc* q, void* data);
bool mel_mpmc_pop(Mel_Mpmc* q, void** out);
