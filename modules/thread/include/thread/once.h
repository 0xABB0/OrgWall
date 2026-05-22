#pragma once

#include <core/types.h>
#include <thread/storage.h>

#include <stdalign.h>

typedef void (*Mel_Once_Fn)(void);

typedef struct Mel_Once {
    alignas(MEL_ONCE_STORAGE_ALIGN) byte _storage[MEL_ONCE_STORAGE_SIZE];
} Mel_Once;

#define MEL_ONCE_INIT (Mel_Once){0}

void mel_once(Mel_Once* o, Mel_Once_Fn fn);
