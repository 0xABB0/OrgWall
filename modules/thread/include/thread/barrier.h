#pragma once

#include <core/types.h>
#include <thread/storage.h>

#include <stdalign.h>

typedef struct Mel_Barrier {
    alignas(MEL_BARRIER_STORAGE_ALIGN) byte _storage[MEL_BARRIER_STORAGE_SIZE];
} Mel_Barrier;

bool mel_barrier_init    (Mel_Barrier* b, u32 count);
void mel_barrier_destroy (Mel_Barrier* b);
bool mel_barrier_wait    (Mel_Barrier* b);
