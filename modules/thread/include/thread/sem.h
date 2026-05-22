#pragma once

#include <core/types.h>
#include <thread/storage.h>

#include <stdalign.h>

typedef struct Mel_Sem {
    alignas(MEL_SEM_STORAGE_ALIGN) byte _storage[MEL_SEM_STORAGE_SIZE];
} Mel_Sem;

bool mel_sem_init    (Mel_Sem* s, u32 initial);
void mel_sem_destroy (Mel_Sem* s);
void mel_sem_wait    (Mel_Sem* s);
bool mel_sem_trywait (Mel_Sem* s);
bool mel_sem_wait_for(Mel_Sem* s, i64 timeout_ns);
void mel_sem_post    (Mel_Sem* s);
