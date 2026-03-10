#include "async.task.h"
#include "allocator.h"
#include "async.io.h"
#include "async.job.h"
#include "vfs.async.h"

#include <assert.h>
#include <string.h>

#define MEL_TASK__STEP_WAITING  0
#define MEL_TASK__STEP_RUNNING  1
#define MEL_TASK__STEP_DONE     2
#define MEL_TASK__STEP_FAILED   3

typedef struct {
    Mel_Task_Step_Fn fn;
    void*            user_data;
    u32              status;
    u32              wait_type;
    union {
        u64     io_ticket;
        Mel_Job job_handle;
    };
    u32              deps_remaining;
} Mel_Task__Step;

typedef struct {
    u32 step;
    u32 depends_on;
} Mel_Task__Edge;

typedef struct {
    Mel_Task__Step*      steps;
    u32                  step_count;
    u32                  step_capacity;

    Mel_Task__Edge*      edges;
    u32                  edge_count;
    u32                  edge_capacity;

    u32                  steps_done;
    u32                  status;

    Mel_Task_Complete_Fn on_complete;
    void*                on_complete_user;

    const Mel_Alloc*     alloc;
} Mel_Task;

struct Mel_Task_Ctx {
    const Mel_Alloc* alloc;
    Mel_Io*          io;
    Mel_Vfs*         vfs;
    Mel_Job_Context* jobs;
    Mel_SlotMap      tasks;
};

Mel_Task_Ctx* mel_task_ctx_create(const Mel_Task_Ctx_Desc* desc)
{
    assert(desc);
    assert(desc->alloc);

    Mel_Task_Ctx* ctx = mel_alloc_type(desc->alloc, Mel_Task_Ctx);
    memset(ctx, 0, sizeof(*ctx));
    ctx->alloc = desc->alloc;
    ctx->io    = desc->io;
    ctx->vfs   = desc->vfs;
    ctx->jobs  = desc->jobs;

    mel_slotmap_init(&ctx->tasks, ctx->alloc, .item_size = sizeof(Mel_Task));
    return ctx;
}

void mel_task_ctx_destroy(Mel_Task_Ctx* ctx)
{
    assert(ctx);
    assert(mel_slotmap_count(&ctx->tasks) == 0);

    mel_slotmap_free(&ctx->tasks);

    const Mel_Alloc* alloc = ctx->alloc;
    mel_dealloc(alloc, ctx);
}

Mel_Task_Handle mel_task_begin_opt(Mel_Task_Ctx* ctx, Mel_Task_Begin_Opt opt)
{
    assert(ctx);

    Mel_Task task;
    memset(&task, 0, sizeof(task));
    task.alloc            = ctx->alloc;
    task.status           = MEL_TASK_STATUS_BUILDING;
    task.on_complete      = opt.on_complete;
    task.on_complete_user = opt.on_complete_user;

    task.step_capacity = MEL_TASK_DEFAULT_STEP_CAPACITY;
    task.steps = mel_alloc(ctx->alloc, sizeof(Mel_Task__Step) * task.step_capacity);

    task.edge_capacity = MEL_TASK_DEFAULT_EDGE_CAPACITY;
    task.edges = mel_alloc(ctx->alloc, sizeof(Mel_Task__Edge) * task.edge_capacity);

    return mel_slotmap_insert(&ctx->tasks, &task);
}

u32 mel_task_add_step(Mel_Task_Ctx* ctx, Mel_Task_Handle handle, Mel_Task_Step_Desc desc)
{
    assert(ctx);
    assert(desc.fn);

    Mel_Task* task = mel_slotmap_get(&ctx->tasks, handle);
    assert(task);
    assert(task->status == MEL_TASK_STATUS_BUILDING);

    if (task->step_count >= task->step_capacity) {
        task->step_capacity *= 2;
        task->steps = mel_realloc(task->alloc, task->steps,
                                  sizeof(Mel_Task__Step) * task->step_capacity);
    }

    u32 idx = task->step_count++;
    Mel_Task__Step* step = &task->steps[idx];
    memset(step, 0, sizeof(*step));
    step->fn        = desc.fn;
    step->user_data = desc.user_data;
    step->status    = MEL_TASK__STEP_WAITING;

    return idx;
}

void mel_task_add_dep(Mel_Task_Ctx* ctx, Mel_Task_Handle handle, u32 step, u32 depends_on)
{
    assert(ctx);

    Mel_Task* task = mel_slotmap_get(&ctx->tasks, handle);
    assert(task);
    assert(task->status == MEL_TASK_STATUS_BUILDING);
    assert(step < task->step_count);
    assert(depends_on < task->step_count);
    assert(step != depends_on);

    if (task->edge_count >= task->edge_capacity) {
        task->edge_capacity *= 2;
        task->edges = mel_realloc(task->alloc, task->edges,
                                  sizeof(Mel_Task__Edge) * task->edge_capacity);
    }

    task->edges[task->edge_count++] = (Mel_Task__Edge){
        .step       = step,
        .depends_on = depends_on,
    };
}

void mel_task_submit(Mel_Task_Ctx* ctx, Mel_Task_Handle handle)
{
    assert(ctx);

    Mel_Task* task = mel_slotmap_get(&ctx->tasks, handle);
    assert(task);
    assert(task->status == MEL_TASK_STATUS_BUILDING);
    assert(task->step_count > 0);

    for (u32 i = 0; i < task->step_count; i++)
        task->steps[i].deps_remaining = 0;

    for (u32 i = 0; i < task->edge_count; i++)
        task->steps[task->edges[i].step].deps_remaining++;

    task->status = MEL_TASK_STATUS_RUNNING;
}

static void mel__task_cancel_steps(Mel_Task_Ctx* ctx, Mel_Task* task)
{
    for (u32 i = 0; i < task->step_count; i++) {
        Mel_Task__Step* step = &task->steps[i];
        if (step->status == MEL_TASK__STEP_WAITING) {
            step->status = MEL_TASK__STEP_FAILED;
        } else if (step->status == MEL_TASK__STEP_RUNNING) {
            if (step->wait_type == MEL_TASK_WAIT_IO && ctx->io)
                mel_io_cancel(ctx->io, step->io_ticket);
            else if (step->wait_type == MEL_TASK_WAIT_VFS && ctx->io && ctx->vfs) {
                u64 cancel_ticket = mel_vfs_next_ticket(ctx->vfs);
                Mel_Vfs_Sqe sqe;
                memset(&sqe, 0, sizeof(sqe));
                sqe.ticket = cancel_ticket;
                sqe.op = MEL_VFS_OP_CANCEL;
                sqe.cancel.ticket_to_cancel = step->io_ticket;
                mel_vfs_submit(ctx->vfs, &sqe, 1);
            }
            step->status = MEL_TASK__STEP_FAILED;
        }
    }
}

static void mel__task_step_completed(Mel_Task* task, u32 step_idx)
{
    task->steps[step_idx].status = MEL_TASK__STEP_DONE;
    task->steps_done++;

    for (u32 i = 0; i < task->edge_count; i++) {
        if (task->edges[i].depends_on == step_idx) {
            assert(task->steps[task->edges[i].step].deps_remaining > 0);
            task->steps[task->edges[i].step].deps_remaining--;
        }
    }
}

static void mel__task_run_step(Mel_Task_Ctx* ctx, Mel_Task* task, u32 step_idx)
{
    Mel_Task__Step* step = &task->steps[step_idx];
    Mel_Task_Step_Result result = step->fn(ctx, step->user_data);

    switch (result.result) {
    case MEL_TASK_STEP_DONE:
        mel__task_step_completed(task, step_idx);
        break;

    case MEL_TASK_STEP_PENDING:
        step->status    = MEL_TASK__STEP_RUNNING;
        step->wait_type = result.wait_type;
        if (result.wait_type == MEL_TASK_WAIT_JOB)
            step->job_handle = result.job_handle;
        else
            step->io_ticket = result.io_ticket;
        break;

    case MEL_TASK_STEP_FAILED:
        step->status = MEL_TASK__STEP_FAILED;
        task->status = MEL_TASK_STATUS_FAILED;
        mel__task_cancel_steps(ctx, task);
        break;
    }
}

static bool mel__task_poll_running_step(Mel_Task_Ctx* ctx, Mel_Task__Step* step)
{
    switch (step->wait_type) {
    case MEL_TASK_WAIT_IO: {
        assert(ctx->io);
        Mel_Io_Cqe cqe;
        if (mel_io_poll_ticket(ctx->io, step->io_ticket, &cqe)) {
            if (cqe.status == MEL_IO_STATUS_OK)
                return true;
            step->status = MEL_TASK__STEP_FAILED;
            return false;
        }
    } break;

    case MEL_TASK_WAIT_VFS: {
        assert(ctx->vfs);
        Mel_Vfs_Cqe cqe;
        if (mel_vfs_poll_ticket(ctx->vfs, step->io_ticket, &cqe)) {
            if (cqe.status == MEL_VFS_STATUS_OK)
                return true;
            step->status = MEL_TASK__STEP_FAILED;
            return false;
        }
    } break;

    case MEL_TASK_WAIT_JOB: {
        assert(ctx->jobs);
        if (mel_job_test_and_del(ctx->jobs, step->job_handle))
            return true;
    } break;
    }

    return false;
}

static Mel_SlotMap_Handle mel__task_handle_at(Mel_SlotMap* sm, u32 packed_idx)
{
    u32 slot_idx = sm->packed_to_slot[packed_idx];
    return mel_slotmap_handle_make(slot_idx, sm->slots[slot_idx].generation);
}

void mel_task_tick(Mel_Task_Ctx* ctx)
{
    assert(ctx);

    u32 count = mel_slotmap_count(&ctx->tasks);
    if (count == 0) return;

    Mel_Task* data = mel_slotmap_data(&ctx->tasks);

    for (u32 pi = 0; pi < count; pi++) {
        Mel_Task* task = &data[pi];
        if (task->status != MEL_TASK_STATUS_RUNNING) continue;

        for (u32 si = 0; si < task->step_count; si++) {
            Mel_Task__Step* step = &task->steps[si];
            if (step->status != MEL_TASK__STEP_RUNNING) continue;

            bool completed = mel__task_poll_running_step(ctx, step);
            if (completed) {
                mel__task_step_completed(task, si);
            } else if (step->status == MEL_TASK__STEP_FAILED) {
                task->status = MEL_TASK_STATUS_FAILED;
                mel__task_cancel_steps(ctx, task);
                break;
            }
        }

        if (task->status != MEL_TASK_STATUS_RUNNING) continue;

        for (u32 si = 0; si < task->step_count; si++) {
            Mel_Task__Step* step = &task->steps[si];
            if (step->status != MEL_TASK__STEP_WAITING) continue;
            if (step->deps_remaining > 0) continue;

            mel__task_run_step(ctx, task, si);
            if (task->status != MEL_TASK_STATUS_RUNNING) break;
        }

        if (task->status == MEL_TASK_STATUS_RUNNING && task->steps_done == task->step_count) {
            task->status = MEL_TASK_STATUS_DONE;
        }

        if (task->status == MEL_TASK_STATUS_DONE || task->status == MEL_TASK_STATUS_FAILED) {
            if (task->on_complete) {
                Mel_SlotMap_Handle handle = mel__task_handle_at(&ctx->tasks, pi);
                task->on_complete(handle, task->status, task->on_complete_user);
            }
        }
    }
}

bool mel_task_is_done(Mel_Task_Ctx* ctx, Mel_Task_Handle handle)
{
    assert(ctx);
    Mel_Task* task = mel_slotmap_get(&ctx->tasks, handle);
    assert(task);
    return task->status == MEL_TASK_STATUS_DONE;
}

u32 mel_task_status(Mel_Task_Ctx* ctx, Mel_Task_Handle handle)
{
    assert(ctx);
    Mel_Task* task = mel_slotmap_get(&ctx->tasks, handle);
    assert(task);
    return task->status;
}

f32 mel_task_progress(Mel_Task_Ctx* ctx, Mel_Task_Handle handle)
{
    assert(ctx);
    Mel_Task* task = mel_slotmap_get(&ctx->tasks, handle);
    assert(task);
    if (task->step_count == 0) return 1.0f;
    return (f32)task->steps_done / (f32)task->step_count;
}

void mel_task_cancel(Mel_Task_Ctx* ctx, Mel_Task_Handle handle)
{
    assert(ctx);
    Mel_Task* task = mel_slotmap_get(&ctx->tasks, handle);
    assert(task);
    assert(task->status == MEL_TASK_STATUS_RUNNING || task->status == MEL_TASK_STATUS_BUILDING);

    mel__task_cancel_steps(ctx, task);
    task->status = MEL_TASK_STATUS_CANCELLED;

    if (task->on_complete)
        task->on_complete(handle, task->status, task->on_complete_user);
}

void mel_task_release(Mel_Task_Ctx* ctx, Mel_Task_Handle handle)
{
    assert(ctx);
    Mel_Task* task = mel_slotmap_get(&ctx->tasks, handle);
    assert(task);
    assert(task->status == MEL_TASK_STATUS_DONE ||
           task->status == MEL_TASK_STATUS_FAILED ||
           task->status == MEL_TASK_STATUS_CANCELLED);

    if (task->steps)  mel_dealloc(task->alloc, task->steps);
    if (task->edges)  mel_dealloc(task->alloc, task->edges);

    mel_slotmap_remove(&ctx->tasks, handle);
}
