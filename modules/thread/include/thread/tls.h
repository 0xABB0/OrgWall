#pragma once

#include <core/types.h>
#include <thread/storage.h>

#include <stdalign.h>

typedef void (*Mel_Tls_Dtor)(void* value);

typedef struct Mel_Tls {
    alignas(MEL_TLS_STORAGE_ALIGN) byte _storage[MEL_TLS_STORAGE_SIZE];
} Mel_Tls;

bool  mel_tls_init    (Mel_Tls* k, Mel_Tls_Dtor dtor);
void  mel_tls_destroy (Mel_Tls* k);
void* mel_tls_get     (Mel_Tls* k);
void  mel_tls_set     (Mel_Tls* k, void* value);
