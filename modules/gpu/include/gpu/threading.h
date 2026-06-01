#pragma once

#include <core/types.h>

typedef enum
{
    MEL_GPU_CONCURRENCY_CONCURRENT = 0,
    MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT = 1,
    MEL_GPU_CONCURRENCY_SERIALIZED_PER_DEVICE = 2,
} Mel_Gpu_Concurrency;

typedef struct Mel_Gpu_Thread_Tracker Mel_Gpu_Thread_Tracker;

Mel_Gpu_Thread_Tracker* mel_gpu_thread_tracker_create(void);
void                    mel_gpu_thread_tracker_destroy(Mel_Gpu_Thread_Tracker* t);
void                    mel_gpu_thread_tracker_enter(Mel_Gpu_Thread_Tracker* t, const void* object, Mel_Gpu_Concurrency cls);
void                    mel_gpu_thread_tracker_exit(Mel_Gpu_Thread_Tracker* t, const void* object);
