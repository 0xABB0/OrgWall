#pragma once

#include <core/types.h>
#include <thread/storage.h>
#include <thread/mutex.h>

#include <stdalign.h>

typedef struct Mel_Cond {
    alignas(MEL_COND_STORAGE_ALIGN) byte _storage[MEL_COND_STORAGE_SIZE];
} Mel_Cond;

bool mel_cond_init      (Mel_Cond* c);
void mel_cond_destroy   (Mel_Cond* c);
void mel_cond_wait      (Mel_Cond* c, Mel_Mutex* m);
bool mel_cond_wait_for  (Mel_Cond* c, Mel_Mutex* m, i64 timeout_ns);
void mel_cond_signal    (Mel_Cond* c);
void mel_cond_broadcast (Mel_Cond* c);
