#pragma once

#include <core/types.h>
#include <thread/storage.h>

#include <stdalign.h>

typedef enum {
    MEL_MUTEX_PLAIN     = 0,
    MEL_MUTEX_RECURSIVE = 1,
} Mel_Mutex_Kind;

typedef struct Mel_Mutex {
    alignas(MEL_MUTEX_STORAGE_ALIGN) byte _storage[MEL_MUTEX_STORAGE_SIZE];
} Mel_Mutex;

bool mel_mutex_init    (Mel_Mutex* m, Mel_Mutex_Kind kind);
void mel_mutex_destroy (Mel_Mutex* m);
void mel_mutex_lock    (Mel_Mutex* m);
bool mel_mutex_trylock (Mel_Mutex* m);
void mel_mutex_unlock  (Mel_Mutex* m);
