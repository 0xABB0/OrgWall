#pragma once

#include "core.types.h"

typedef struct Mel_Job_Context Mel_Job_Context;
typedef u32* Mel_Job;

typedef void (*Mel_Job_Cb)(i32 range_start, i32 range_end, i32 thread_index, void* user);
typedef void (*Mel_Job_Thread_Init_Cb)(Mel_Job_Context* ctx, i32 thread_index, u32 thread_id, void* user);
typedef void (*Mel_Job_Thread_Shutdown_Cb)(Mel_Job_Context* ctx, i32 thread_index, u32 thread_id, void* user);
