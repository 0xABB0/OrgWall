#pragma once

#include <core/types.h>
#include <thread/storage.h>

#include <stdalign.h>

typedef enum {
    MEL_EVENT_AUTO_RESET   = 0,
    MEL_EVENT_MANUAL_RESET = 1,
} Mel_Event_Kind;

typedef struct Mel_Event {
    alignas(MEL_EVENT_STORAGE_ALIGN) byte _storage[MEL_EVENT_STORAGE_SIZE];
} Mel_Event;

bool mel_event_init    (Mel_Event* e, Mel_Event_Kind kind);
void mel_event_destroy (Mel_Event* e);
void mel_event_wait    (Mel_Event* e);
bool mel_event_wait_for(Mel_Event* e, i64 timeout_ns);
void mel_event_signal  (Mel_Event* e);
void mel_event_reset   (Mel_Event* e);
