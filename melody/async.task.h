#pragma once

#include "async.task.cfg.h"
#include "async.task.fwd.h"
#include "async.io.fwd.h"
#include "async.job.h"
#include "vfs.fwd.h"
#include "allocator.fwd.h"
#include "collection.slotmap.h"
#include "core.types.h"

#define MEL_TASK_STATUS_BUILDING   0
#define MEL_TASK_STATUS_RUNNING    1
#define MEL_TASK_STATUS_DONE       2
#define MEL_TASK_STATUS_FAILED     3
#define MEL_TASK_STATUS_CANCELLED  4

#define MEL_TASK_STEP_DONE     0
#define MEL_TASK_STEP_PENDING  1
#define MEL_TASK_STEP_FAILED   2

#define MEL_TASK_WAIT_NONE  0
#define MEL_TASK_WAIT_IO    1
#define MEL_TASK_WAIT_VFS   2
#define MEL_TASK_WAIT_JOB   3

typedef struct {
    u32   result;
    u32   wait_type;
    union {
        u64     io_ticket;
        Mel_Job job_handle;
    };
} Mel_Task_Step_Result;

typedef Mel_Task_Step_Result (*Mel_Task_Step_Fn)(Mel_Task_Ctx* ctx, void* user_data);

typedef struct {
    Mel_Task_Step_Fn fn;
    void*            user_data;
} Mel_Task_Step_Desc;

typedef struct {
    i32         count;
    Mel_Job_Cb  callback;
    void*       user;
    u32         priority;
    u32         tags;
} Mel_Task_Job_Step_Opt;

typedef void (*Mel_Task_Complete_Fn)(Mel_Task_Handle handle, u32 status, void* user);

typedef struct {
    Mel_Task_Complete_Fn on_complete;
    void*                on_complete_user;
} Mel_Task_Begin_Opt;

typedef struct {
    const Mel_Alloc* alloc;
    Mel_Io*          io;
    Mel_Vfs*         vfs;
    Mel_Job_Context* jobs;
} Mel_Task_Ctx_Desc;

Mel_Task_Ctx*   mel_task_ctx_create(const Mel_Task_Ctx_Desc* desc);
void            mel_task_ctx_destroy(Mel_Task_Ctx* ctx);
void            mel_task_tick(Mel_Task_Ctx* ctx);
void            mel_task_wait(Mel_Task_Ctx* ctx, Mel_Task_Handle handle);

Mel_Task_Handle mel_task_begin_opt(Mel_Task_Ctx* ctx, Mel_Task_Begin_Opt opt);
#define mel_task_begin(ctx, ...) mel_task_begin_opt((ctx), (Mel_Task_Begin_Opt){__VA_ARGS__})

Mel_Task_Step_Result mel_task_step_dispatch_job_opt(Mel_Task_Ctx* ctx, Mel_Task_Job_Step_Opt opt);
#define mel_task_step_dispatch_job(ctx, ...) \
    mel_task_step_dispatch_job_opt((ctx), (Mel_Task_Job_Step_Opt){ .count = 1, .priority = MEL_JOB_PRIORITY_NORMAL, __VA_ARGS__ })

u32             mel_task_add_step(Mel_Task_Ctx* ctx, Mel_Task_Handle handle, Mel_Task_Step_Desc desc);
void            mel_task_add_dep(Mel_Task_Ctx* ctx, Mel_Task_Handle handle, u32 step, u32 depends_on);
void            mel_task_submit(Mel_Task_Ctx* ctx, Mel_Task_Handle handle);

bool            mel_task_is_done(Mel_Task_Ctx* ctx, Mel_Task_Handle handle);
u32             mel_task_status(Mel_Task_Ctx* ctx, Mel_Task_Handle handle);
f32             mel_task_progress(Mel_Task_Ctx* ctx, Mel_Task_Handle handle);

void            mel_task_cancel(Mel_Task_Ctx* ctx, Mel_Task_Handle handle);
void            mel_task_release(Mel_Task_Ctx* ctx, Mel_Task_Handle handle);
