#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <thread/storage.h>

typedef struct Mel_Barrier
{
    MEL_ALIGNAS(MEL_BARRIER_STORAGE_ALIGN) byte _storage[MEL_BARRIER_STORAGE_SIZE];
} Mel_Barrier;

bool mel_barrier_init(Mel_Barrier* b, u32 count);
void mel_barrier_destroy(Mel_Barrier* b);
bool mel_barrier_wait(Mel_Barrier* b);
