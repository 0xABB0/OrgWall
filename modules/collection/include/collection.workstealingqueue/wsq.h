#pragma once

#include <core/types.h>
#include <allocator/fwd.h>
#include <stdatomic.h>

typedef struct Mel_Wsq_Array Mel_Wsq_Array;

struct Mel_Wsq_Array
{
    i64 capacity;
    _Atomic(void*) slots[];
};

typedef struct
{
    _Atomic(i64) top;
    _Atomic(i64) bottom;
    _Atomic(Mel_Wsq_Array*) array;
    const Mel_Alloc* alloc;
    Mel_Wsq_Array** garbage;
    i64 garbage_count;
    i64 garbage_capacity;
} Mel_Wsq;

#define MEL_WSQ_EMPTY  ((void*)(uintptr_t)0xDEAD00000001ULL)
#define MEL_WSQ_ABORT  ((void*)(uintptr_t)0xDEAD00000002ULL)

typedef struct {
    i64 initial_capacity;
} Mel_Wsq_Opt;

Mel_Wsq mel_wsq_create_opt(const Mel_Alloc* alloc, Mel_Wsq_Opt opt);
#define mel_wsq_create(alloc, ...) mel_wsq_create_opt((alloc), (Mel_Wsq_Opt){ __VA_ARGS__ })

void    mel_wsq_destroy(Mel_Wsq* q);
void    mel_wsq_push(Mel_Wsq* q, void* item);
void*   mel_wsq_pop(Mel_Wsq* q);
void*   mel_wsq_steal(Mel_Wsq* q);
i64     mel_wsq_size(Mel_Wsq* q);
