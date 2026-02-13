#pragma once

#include "core.types.h"
#include "allocator.fwd.h"
#include "async.job.fwd.h"

#define MEL_JOB_PRIORITY_HIGH   0
#define MEL_JOB_PRIORITY_NORMAL 1
#define MEL_JOB_PRIORITY_LOW    2
#define MEL_JOB_PRIORITY_COUNT  3

typedef struct {
    i32 num_threads;
    i32 max_fibers;
    u32 fiber_stack_sz;
    Mel_Job_Thread_Init_Cb thread_init_cb;
    Mel_Job_Thread_Shutdown_Cb thread_shutdown_cb;
    void* thread_user_data;
} Mel_Job_Context_Opt;

Mel_Job_Context* mel_job_create_context_opt(const Mel_Alloc* alloc, Mel_Job_Context_Opt opt);
#define mel_job_create_context(alloc, ...) mel_job_create_context_opt((alloc), (Mel_Job_Context_Opt){ .num_threads = -1, .max_fibers = 64, .fiber_stack_sz = 1048576, __VA_ARGS__})

void    mel_job_destroy_context(Mel_Job_Context* ctx, const Mel_Alloc* alloc);

typedef struct {
    u32 priority;
    u32 tags;
} Mel_Job_Dispatch_Opt;

Mel_Job mel_job_dispatch_opt(Mel_Job_Context* ctx, i32 count, Mel_Job_Cb callback, void* user, Mel_Job_Dispatch_Opt opt);
#define mel_job_dispatch(ctx, count, callback, user, ...) mel_job_dispatch_opt((ctx), (count), (callback), (user), (Mel_Job_Dispatch_Opt){ .priority = MEL_JOB_PRIORITY_NORMAL, __VA_ARGS__})

void    mel_job_wait_and_del(Mel_Job_Context* ctx, Mel_Job job);
bool    mel_job_test_and_del(Mel_Job_Context* ctx, Mel_Job job);
i32     mel_job_num_worker_threads(Mel_Job_Context* ctx);
i32     mel_job_thread_index(Mel_Job_Context* ctx);
void    mel_job_set_current_thread_tags(Mel_Job_Context* ctx, u32 tags);
