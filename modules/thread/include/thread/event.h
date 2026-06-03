#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <thread/storage.h>

typedef enum
{
    MEL_THREAD_EVENT_AUTO_RESET = 0,
    MEL_THREAD_EVENT_MANUAL_RESET = 1,
} Mel_Thread_Event_Kind;

typedef struct Mel_Thread_Event
{
    MEL_ALIGNAS(MEL_THREAD_EVENT_STORAGE_ALIGN) byte _storage[MEL_THREAD_EVENT_STORAGE_SIZE];
} Mel_Thread_Event;

bool mel_thread_event_init(Mel_Thread_Event* e, Mel_Thread_Event_Kind kind);
void mel_thread_event_destroy(Mel_Thread_Event* e);
void mel_thread_event_wait(Mel_Thread_Event* e);
bool mel_thread_event_wait_for(Mel_Thread_Event* e, i64 timeout_ns);
void mel_thread_event_signal(Mel_Thread_Event* e);
void mel_thread_event_reset(Mel_Thread_Event* e);
