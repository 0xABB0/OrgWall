#include "async.job.h"
#include "async.fiber.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "allocator.pool.h"

#include <SDL3/SDL.h>
#include <string.h>

static inline void mel__sem_post_n(SDL_Semaphore* sem, i32 n)
{
    for (i32 i = 0; i < n; i++)
        SDL_SignalSemaphore(sem);
}

#define MEL__JOB_COUNTER_POOL_SIZE 256
#define MEL__JOB_DEFAULT_MAX_FIBERS 64
#define MEL__JOB_DEFAULT_FIBER_STACK_SIZE 1048576

typedef struct Mel__Job {
    i32 job_index;
    i32 done;
    u32 owner_tid;
    u32 tags;
    Mel_Fiber_Stack stack_mem;
    Mel_Fiber fiber;
    Mel_Fiber selector_fiber;
    Mel_Job counter;
    Mel_Job wait_counter;
    Mel_Job_Context* ctx;
    Mel_Job_Cb callback;
    void* user;
    i32 range_start;
    i32 range_end;
    u32 priority;
    struct Mel__Job* next;
    struct Mel__Job* prev;
} Mel__Job;

typedef struct {
    Mel__Job* cur_job;
    Mel_Fiber_Stack selector_stack;
    Mel_Fiber selector_fiber;
    i32 thread_index;
    u32 tid;
    u32 tags;
    bool main_thrd;
} Mel__Job_Thread_Data;

typedef struct {
    Mel_Job counter;
    i32 range_size;
    i32 range_reminder;
    Mel_Job_Cb callback;
    void* user;
    u32 priority;
    u32 tags;
} Mel__Job_Pending;

struct Mel_Job_Context {
    const Mel_Alloc* alloc;
    SDL_Thread** threads;
    i32 num_threads;
    i32 stack_sz;
    Mel_Pool job_pool;
    u8* job_pool_buf;
    Mel_Pool counter_pool;
    u8* counter_pool_buf;
    Mel__Job* waiting_list[MEL_JOB_PRIORITY_COUNT];
    Mel__Job* waiting_list_last[MEL_JOB_PRIORITY_COUNT];
    u32* tags;
    SDL_SpinLock job_lk;
    SDL_SpinLock counter_lk;
    SDL_TLSID thread_tls;
    SDL_AtomicInt dummy_counter;
    SDL_Semaphore* sem;
    i32 quit;
    Mel_Job_Thread_Init_Cb thread_init_cb;
    Mel_Job_Thread_Shutdown_Cb thread_shutdown_cb;
    void* thread_user;
    Mel__Job_Pending* pending;
    i32 pending_count;
    i32 pending_capacity;
};

static void mel__job_del(Mel_Job_Context* ctx, Mel__Job* job)
{
    SDL_LockSpinlock(&ctx->job_lk);
    mel_pool_free(&ctx->job_pool, job);
    SDL_UnlockSpinlock(&ctx->job_lk);
}

static void mel__job_fiber_fn(Mel_Fiber_Transfer transfer)
{
    Mel__Job* job = (Mel__Job*)transfer.user;
    Mel_Job_Context* ctx = job->ctx;
    Mel__Job_Thread_Data* tdata = (Mel__Job_Thread_Data*)SDL_GetTLS(&ctx->thread_tls);

    assert(tdata->cur_job == job);

    job->selector_fiber = transfer.from;
    tdata->selector_fiber = transfer.from;
    tdata->cur_job = job;

    job->callback(job->range_start, job->range_end, tdata->thread_index, job->user);
    job->done = 1;

    mel_fiber_switch(transfer.from, transfer.user);
}

static Mel__Job* mel__job_new(Mel_Job_Context* ctx, i32 index, Mel_Job_Cb callback, void* user,
                               i32 range_start, i32 range_end, Mel_Job counter, u32 tags,
                               u32 priority)
{
    Mel__Job* j = (Mel__Job*)mel_pool_alloc(&ctx->job_pool);

    if (j)
    {
        j->job_index = index;
        j->owner_tid = 0;
        j->tags = tags;
        j->done = 0;
        if (!j->stack_mem.sptr)
        {
            if (!mel_fiber_stack_init(&j->stack_mem, (u32)ctx->stack_sz))
            {
                assert(false && "not enough memory for fiber stack");
                return nullptr;
            }
        }
        j->fiber = mel_fiber_create(j->stack_mem, mel__job_fiber_fn);
        j->counter = counter;
        j->wait_counter = (Mel_Job)&ctx->dummy_counter;
        j->ctx = ctx;
        j->callback = callback;
        j->user = user;
        j->range_start = range_start;
        j->range_end = range_end;
        j->priority = priority;
        j->next = j->prev = nullptr;
    }
    return j;
}

static inline void mel__job_add_list(Mel__Job** pfirst, Mel__Job** plast, Mel__Job* node)
{
    if (*plast)
    {
        (*plast)->next = node;
        node->prev = *plast;
    }
    *plast = node;
    if (*pfirst == nullptr)
        *pfirst = node;
}

static inline void mel__job_remove_list(Mel__Job** pfirst, Mel__Job** plast, Mel__Job* node)
{
    if (node->prev)
        node->prev->next = node->next;
    if (node->next)
        node->next->prev = node->prev;
    if (*pfirst == node)
        *pfirst = node->next;
    if (*plast == node)
        *plast = node->prev;
    node->prev = node->next = nullptr;
}

typedef struct {
    Mel__Job* job;
    bool waiting_list_alive;
} Mel__Job_Select_Result;

static Mel__Job_Select_Result mel__job_select(Mel_Job_Context* ctx, u32 tid, u32 tags)
{
    Mel__Job_Select_Result r = {0};

    SDL_LockSpinlock(&ctx->job_lk);
    for (i32 pr = 0; pr < MEL_JOB_PRIORITY_COUNT; pr++)
    {
        Mel__Job* node = ctx->waiting_list[pr];
        while (node)
        {
            r.waiting_list_alive = true;
            if (SDL_GetAtomicInt((SDL_AtomicInt*)node->wait_counter) == 0)
            {
                if ((node->owner_tid == 0 || node->owner_tid == tid) &&
                    (node->tags == 0 || (node->tags & tags)))
                {
                    r.job = node;
                    mel__job_remove_list(&ctx->waiting_list[pr], &ctx->waiting_list_last[pr], node);
                    pr = MEL_JOB_PRIORITY_COUNT;
                    break;
                }
            }
            node = node->next;
        }
    }
    SDL_UnlockSpinlock(&ctx->job_lk);

    return r;
}

static void mel__job_selector_main_thrd(Mel_Fiber_Transfer transfer)
{
    Mel_Job_Context* ctx = (Mel_Job_Context*)transfer.user;
    Mel__Job_Thread_Data* tdata = (Mel__Job_Thread_Data*)SDL_GetTLS(&ctx->thread_tls);
    assert(tdata);

    Mel__Job_Select_Result r =
        mel__job_select(ctx, tdata->tid, ctx->num_threads > 0 ? tdata->tags : 0xffffffff);

    if (r.job)
    {
        if (r.job->owner_tid > 0)
        {
            assert(tdata->cur_job == nullptr);
            r.job->owner_tid = 0;
        }

        tdata->selector_fiber = r.job->selector_fiber;
        tdata->cur_job = r.job;
        r.job->fiber = mel_fiber_switch(r.job->fiber, r.job).from;

        if (r.job->done)
        {
            tdata->cur_job = nullptr;
            SDL_AddAtomicInt((SDL_AtomicInt*)r.job->counter, -1);
            mel__job_del(ctx, r.job);
        }
    }

    tdata->selector_fiber = nullptr;
    mel_fiber_switch(transfer.from, transfer.user);
}

static void mel__job_selector_fn(Mel_Fiber_Transfer transfer)
{
    Mel_Job_Context* ctx = (Mel_Job_Context*)transfer.user;
    Mel__Job_Thread_Data* tdata = (Mel__Job_Thread_Data*)SDL_GetTLS(&ctx->thread_tls);
    assert(tdata);

    while (!ctx->quit)
    {
        SDL_WaitSemaphore(ctx->sem);

        Mel__Job_Select_Result r = mel__job_select(ctx, tdata->tid, tdata->tags);

        if (r.job)
        {
            if (r.job->owner_tid > 0)
            {
                assert(tdata->cur_job == nullptr);
                r.job->owner_tid = 0;
            }

            tdata->selector_fiber = r.job->selector_fiber;
            tdata->cur_job = r.job;
            r.job->fiber = mel_fiber_switch(r.job->fiber, r.job).from;

            if (r.job->done)
            {
                tdata->cur_job = nullptr;
                SDL_AddAtomicInt((SDL_AtomicInt*)r.job->counter, -1);
                mel__job_del(ctx, r.job);
            }
        }
        else if (r.waiting_list_alive)
        {
            SDL_SignalSemaphore(ctx->sem);
            SDL_CPUPauseInstruction();
        }
    }

    mel_fiber_switch(transfer.from, transfer.user);
}

static void mel__pending_push(Mel_Job_Context* ctx, Mel__Job_Pending p)
{
    if (ctx->pending_count >= ctx->pending_capacity)
    {
        i32 new_cap = ctx->pending_capacity > 0 ? ctx->pending_capacity * 2 : 16;
        ctx->pending = mel_realloc(ctx->alloc, ctx->pending, (usize)new_cap * sizeof(Mel__Job_Pending));
        ctx->pending_capacity = new_cap;
    }
    ctx->pending[ctx->pending_count++] = p;
}

static void mel__pending_pop(Mel_Job_Context* ctx, i32 index)
{
    ctx->pending[index] = ctx->pending[ctx->pending_count - 1];
    ctx->pending_count--;
}

static void mel__job_process_pending(Mel_Job_Context* ctx)
{
    for (i32 i = 0; i < ctx->pending_count; i++)
    {
        Mel__Job_Pending pending = ctx->pending[i];

        i32 count = SDL_GetAtomicInt((SDL_AtomicInt*)pending.counter);
        if (ctx->job_pool.used_count + (usize)count <= ctx->job_pool.block_count)
        {
            i32 range_start = 0;
            i32 range_end = pending.range_size + (pending.range_reminder > 0 ? 1 : 0);
            --pending.range_reminder;

            mel__pending_pop(ctx, i);

            for (i32 k = 0; k < count; k++)
            {
                mel__job_add_list(
                    &ctx->waiting_list[pending.priority], &ctx->waiting_list_last[pending.priority],
                    mel__job_new(ctx, k, pending.callback, pending.user, range_start, range_end,
                                 pending.counter, pending.tags, pending.priority));

                range_start = range_end;
                range_end += (pending.range_size + (pending.range_reminder > 0 ? 1 : 0));
                --pending.range_reminder;
            }

            mel__sem_post_n(ctx->sem, count);
            break;
        }
    }
}

static void mel__job_process_pending_single(Mel_Job_Context* ctx, i32 index)
{
    SDL_LockSpinlock(&ctx->job_lk);

    Mel__Job_Pending pending = ctx->pending[index];
    i32 count = SDL_GetAtomicInt((SDL_AtomicInt*)pending.counter);
    if (ctx->job_pool.used_count + (usize)count <= ctx->job_pool.block_count)
    {
        mel__pending_pop(ctx, index);

        i32 range_start = 0;
        i32 range_end = pending.range_size + (pending.range_reminder > 0 ? 1 : 0);
        --pending.range_reminder;

        for (i32 k = 0; k < count; k++)
        {
            mel__job_add_list(
                &ctx->waiting_list[pending.priority], &ctx->waiting_list_last[pending.priority],
                mel__job_new(ctx, k, pending.callback, pending.user, range_start, range_end,
                             pending.counter, pending.tags, pending.priority));

            range_start = range_end;
            range_end += (pending.range_size + (pending.range_reminder > 0 ? 1 : 0));
            --pending.range_reminder;
        }
        mel__sem_post_n(ctx->sem, count);
    }

    SDL_UnlockSpinlock(&ctx->job_lk);
}

static Mel__Job_Thread_Data* mel__job_create_tdata(const Mel_Alloc* alloc, u32 tid, i32 index, bool main_thrd)
{
    Mel__Job_Thread_Data* tdata = mel_alloc_type(alloc, Mel__Job_Thread_Data);
    if (!tdata) return nullptr;

    memset(tdata, 0, sizeof(Mel__Job_Thread_Data));
    tdata->thread_index = index;
    tdata->tid = tid;
    tdata->tags = 0xffffffff;
    tdata->main_thrd = main_thrd;

    bool r = mel_fiber_stack_init(&tdata->selector_stack, 0);
    assert(r && "not enough memory for selector stack");
    MEL_UNUSED(r);

    return tdata;
}

static void mel__job_destroy_tdata(Mel__Job_Thread_Data* tdata, const Mel_Alloc* alloc)
{
    mel_fiber_stack_release(&tdata->selector_stack);
    mel_dealloc(alloc, tdata);
}

static i32 mel__job_thread_fn(void* user)
{
    Mel_Job_Context* ctx = (Mel_Job_Context*)user;

    static SDL_AtomicInt thread_counter = {0};
    i32 index = SDL_AddAtomicInt(&thread_counter, 1);

    u32 thread_id = (u32)SDL_GetCurrentThreadID();

    Mel__Job_Thread_Data* tdata = mel__job_create_tdata(ctx->alloc, thread_id, index + 1, false);
    if (!tdata) return -1;

    SDL_SetTLS(&ctx->thread_tls, tdata, nullptr);

    if (ctx->thread_init_cb)
        ctx->thread_init_cb(ctx, index, thread_id, ctx->thread_user);

    Mel_Fiber fiber = mel_fiber_create(tdata->selector_stack, mel__job_selector_fn);
    mel_fiber_switch(fiber, ctx);

    SDL_SetTLS(&ctx->thread_tls, nullptr, nullptr);

    if (ctx->thread_shutdown_cb)
        ctx->thread_shutdown_cb(ctx, index, thread_id, ctx->thread_user);

    mel__job_destroy_tdata(tdata, ctx->alloc);

    return 0;
}

Mel_Job_Context* mel_job_create_context_opt(const Mel_Alloc* alloc, Mel_Job_Context_Opt opt)
{
    assert(alloc != nullptr);

    Mel_Job_Context* ctx = mel_alloc_type(alloc, Mel_Job_Context);
    if (!ctx) return nullptr;

    memset(ctx, 0, sizeof(Mel_Job_Context));

    ctx->alloc = alloc;
    ctx->num_threads = opt.num_threads > 0 ? opt.num_threads : (SDL_GetNumLogicalCPUCores() - 1);
    if (ctx->num_threads < 0) ctx->num_threads = 0;

    ctx->thread_tls = (SDL_TLSID){0};
    ctx->stack_sz = opt.fiber_stack_sz > 0 ? (i32)opt.fiber_stack_sz : MEL__JOB_DEFAULT_FIBER_STACK_SIZE;
    ctx->thread_init_cb = opt.thread_init_cb;
    ctx->thread_shutdown_cb = opt.thread_shutdown_cb;
    ctx->thread_user = opt.thread_user_data;
    i32 max_fibers = opt.max_fibers > 0 ? opt.max_fibers : MEL__JOB_DEFAULT_MAX_FIBERS;

    ctx->sem = SDL_CreateSemaphore(0);
    assert(ctx->sem);

    Mel__Job_Thread_Data* main_tdata = mel__job_create_tdata(alloc, (u32)SDL_GetCurrentThreadID(), 0, true);
    if (!main_tdata)
    {
        mel_dealloc(alloc, ctx);
        return nullptr;
    }
    SDL_SetTLS(&ctx->thread_tls, main_tdata, nullptr);
    main_tdata->selector_fiber = mel_fiber_create(main_tdata->selector_stack, mel__job_selector_main_thrd);

    usize job_pool_sz = sizeof(Mel__Job) * (usize)max_fibers;
    ctx->job_pool_buf = mel_alloc(alloc, job_pool_sz);
    memset(ctx->job_pool_buf, 0, job_pool_sz);
    mel_pool_init(&ctx->job_pool, ctx->job_pool_buf, job_pool_sz, .block_size = sizeof(Mel__Job));

    usize counter_block_sz = sizeof(SDL_AtomicInt) < sizeof(void*) ? sizeof(void*) : sizeof(SDL_AtomicInt);
    usize counter_pool_sz = counter_block_sz * MEL__JOB_COUNTER_POOL_SIZE;
    ctx->counter_pool_buf = mel_alloc(alloc, counter_pool_sz);
    mel_pool_init(&ctx->counter_pool, ctx->counter_pool_buf, counter_pool_sz, .block_size = counter_block_sz);

    ctx->tags = mel_alloc(alloc, sizeof(u32) * (usize)(ctx->num_threads + 1));
    memset(ctx->tags, 0xff, sizeof(u32) * (usize)(ctx->num_threads + 1));

    if (ctx->num_threads > 0)
    {
        ctx->threads = mel_alloc(alloc, sizeof(SDL_Thread*) * (usize)ctx->num_threads);
        assert(ctx->threads);

        for (i32 i = 0; i < ctx->num_threads; i++)
        {
            char name[32];
            snprintf(name, sizeof(name), "mel_job(%d)", i + 1);
            ctx->threads[i] = SDL_CreateThread(mel__job_thread_fn, name, ctx);
            assert(ctx->threads[i]);
        }
    }

    return ctx;
}

void mel_job_destroy_context(Mel_Job_Context* ctx, const Mel_Alloc* alloc)
{
    assert(ctx != nullptr);
    assert(ctx->alloc == alloc);

    ctx->quit = 1;
    mel__sem_post_n(ctx->sem, ctx->num_threads + 1);

    for (i32 i = 0; i < ctx->num_threads; i++)
        SDL_WaitThread(ctx->threads[i], nullptr);

    if (ctx->threads)
        mel_dealloc(alloc, ctx->threads);

    mel__job_destroy_tdata((Mel__Job_Thread_Data*)SDL_GetTLS(&ctx->thread_tls), alloc);

    mel_dealloc(alloc, ctx->job_pool_buf);
    mel_dealloc(alloc, ctx->counter_pool_buf);

    SDL_DestroySemaphore(ctx->sem);

    mel_dealloc(alloc, ctx->tags);
    if (ctx->pending)
        mel_dealloc(alloc, ctx->pending);
    mel_dealloc(alloc, ctx);
}

Mel_Job mel_job_dispatch_opt(Mel_Job_Context* ctx, i32 count, Mel_Job_Cb callback, void* user, Mel_Job_Dispatch_Opt opt)
{
    assert(count > 0);

    Mel__Job_Thread_Data* tdata = (Mel__Job_Thread_Data*)SDL_GetTLS(&ctx->thread_tls);

    i32 num_workers = 0;
    if (opt.tags != 0)
    {
        for (i32 i = 0, ic = ctx->num_threads + 1; i < ic; i++)
        {
            if (ctx->tags[i] & opt.tags)
                num_workers++;
        }
    }
    else
    {
        num_workers = ctx->num_threads + 1;
    }

    i32 range_size = count / num_workers;
    i32 range_reminder = count % num_workers;
    i32 num_jobs = range_size > 0 ? num_workers : (range_reminder > 0 ? range_reminder : 0);
    assert(num_jobs > 0);

    SDL_AtomicInt* counter;
    SDL_LockSpinlock(&ctx->counter_lk);
    counter = (SDL_AtomicInt*)mel_pool_alloc(&ctx->counter_pool);
    SDL_UnlockSpinlock(&ctx->counter_lk);

    if (!counter)
    {
        assert(false && "maximum job counter instances exceeded");
        return nullptr;
    }

    SDL_SetAtomicInt(counter, num_jobs);
    assert(tdata && "dispatch must be called from main thread or job threads");

    if (tdata->cur_job)
        tdata->cur_job->wait_counter = (Mel_Job)counter;

    SDL_LockSpinlock(&ctx->job_lk);
    if (ctx->job_pool.used_count + (usize)num_jobs <= ctx->job_pool.block_count)
    {
        i32 range_start = 0;
        i32 range_end = range_size + (range_reminder > 0 ? 1 : 0);
        --range_reminder;

        for (i32 i = 0; i < num_jobs; i++)
        {
            mel__job_add_list(&ctx->waiting_list[opt.priority], &ctx->waiting_list_last[opt.priority],
                              mel__job_new(ctx, i, callback, user, range_start, range_end, (Mel_Job)counter,
                                           opt.tags, opt.priority));
            range_start = range_end;
            range_end += (range_size + (range_reminder > 0 ? 1 : 0));
            --range_reminder;
        }

        mel__sem_post_n(ctx->sem, num_jobs);
    }
    else
    {
        Mel__Job_Pending pending = {
            .counter = (Mel_Job)counter,
            .range_size = range_size,
            .range_reminder = range_reminder,
            .callback = callback,
            .user = user,
            .priority = opt.priority,
            .tags = opt.tags,
        };
        mel__pending_push(ctx, pending);
    }
    SDL_UnlockSpinlock(&ctx->job_lk);

    return (Mel_Job)counter;
}

void mel_job_wait_and_del(Mel_Job_Context* ctx, Mel_Job job)
{
    Mel__Job_Thread_Data* tdata = (Mel__Job_Thread_Data*)SDL_GetTLS(&ctx->thread_tls);

    while (SDL_GetAtomicInt((SDL_AtomicInt*)job) > 0)
    {
        for (i32 i = 0; i < ctx->pending_count; i++)
        {
            if (ctx->pending[i].counter == job)
            {
                mel__job_process_pending_single(ctx, i);
                break;
            }
        }

        if (tdata->cur_job)
        {
            Mel__Job* cur_job = tdata->cur_job;
            tdata->cur_job = nullptr;
            cur_job->owner_tid = tdata->tid;

            SDL_LockSpinlock(&ctx->job_lk);
            i32 list_idx = (i32)cur_job->priority;
            mel__job_add_list(&ctx->waiting_list[list_idx], &ctx->waiting_list_last[list_idx], cur_job);
            SDL_UnlockSpinlock(&ctx->job_lk);

            if (!tdata->main_thrd)
                SDL_SignalSemaphore(ctx->sem);
        }

        mel_fiber_switch(tdata->selector_fiber, ctx);

        if (!tdata->selector_fiber)
            tdata->selector_fiber = mel_fiber_create(tdata->selector_stack, mel__job_selector_main_thrd);

        SDL_CPUPauseInstruction();
    }

    SDL_LockSpinlock(&ctx->counter_lk);
    mel_pool_free(&ctx->counter_pool, (void*)job);
    SDL_UnlockSpinlock(&ctx->counter_lk);

    SDL_LockSpinlock(&ctx->job_lk);
    mel__job_process_pending(ctx);
    SDL_UnlockSpinlock(&ctx->job_lk);
}

bool mel_job_test_and_del(Mel_Job_Context* ctx, Mel_Job job)
{
    if (SDL_GetAtomicInt((SDL_AtomicInt*)job) == 0)
    {
        SDL_LockSpinlock(&ctx->counter_lk);
        mel_pool_free(&ctx->counter_pool, (void*)job);
        SDL_UnlockSpinlock(&ctx->counter_lk);

        SDL_LockSpinlock(&ctx->job_lk);
        mel__job_process_pending(ctx);
        SDL_UnlockSpinlock(&ctx->job_lk);
        return true;
    }

    return false;
}

i32 mel_job_num_worker_threads(Mel_Job_Context* ctx)
{
    return ctx->num_threads;
}

i32 mel_job_thread_index(Mel_Job_Context* ctx)
{
    Mel__Job_Thread_Data* tdata = (Mel__Job_Thread_Data*)SDL_GetTLS(&ctx->thread_tls);
    assert(tdata);
    return tdata->thread_index;
}

void mel_job_set_current_thread_tags(Mel_Job_Context* ctx, u32 tags)
{
    Mel__Job_Thread_Data* tdata = (Mel__Job_Thread_Data*)SDL_GetTLS(&ctx->thread_tls);
    assert(tdata);
    tdata->tags = tags;
    ctx->tags[tdata->thread_index] = tags;
}
