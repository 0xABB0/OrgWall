#include "async.job.h"
#include "async.signal.h"
#include "async.fiber.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "allocator.vmem.h"
#include "collection.mpmc.h"
#include "collection.wsq.h"

#include <SDL3/SDL.h>
#include <string.h>

#define MEL__JOB_ANY_WORKER 0xFF

typedef enum {
    MEL__FIBER_STATE_FREE = 0,
    MEL__FIBER_STATE_IDLE,
    MEL__FIBER_STATE_JOB,
    MEL__FIBER_STATE_RUNNABLE,
    MEL__FIBER_STATE_PARKED,
    MEL__FIBER_STATE_SWITCHING,
} Mel__Fiber_State;

static const char* mel__fiber_state_name(Mel__Fiber_State state)
{
    switch (state)
    {
    case MEL__FIBER_STATE_FREE:      return "free";
    case MEL__FIBER_STATE_IDLE:      return "idle";
    case MEL__FIBER_STATE_JOB:       return "job";
    case MEL__FIBER_STATE_RUNNABLE:  return "runnable";
    case MEL__FIBER_STATE_PARKED:    return "parked";
    case MEL__FIBER_STATE_SWITCHING: return "switching";
    }

    return "unknown";
}

typedef struct {
    Mel_Job_Fn task;
    void* data;
    Mel_Counter* dec_on_finish;
    u8 worker_index;
    const char* debug_name;
    const char* debug_file;
    u32 debug_line;
} Mel__Job;

typedef struct Mel__Fiber_Decl {
    Mel_Fiber fiber;
    Mel__Job current_job;
    Mel__Fiber_State debug_state;
    const char* last_job_name;
    const char* last_job_file;
    void* last_job_task;
    u32 last_job_line;
} Mel__Fiber_Decl;

enum { MEL__WORK_NONE, MEL__WORK_JOB, MEL__WORK_FIBER };

typedef struct {
    u8 type;
    union {
        Mel__Job job;
        Mel__Fiber_Decl* fiber;
    };
} Mel__Work;

typedef struct {
    Mel_Wsq wsq;
    Mel_Mpmc pinned;
    Mel__Fiber_Decl* current_fiber;

    Mel_Fiber saved_context;
    Mel__Fiber_Decl* fiber_to_free;
    Mel_Signal* signal_to_park_on;
    Mel__Fiber_Decl* fiber_to_push;
    i32 push_target_worker;
    Mel__Fiber_Decl* debug_resume_fiber;
    const char* debug_resume_reason;

    Mel_Fiber primary_fiber;

    SDL_Thread* thread;
    SDL_Condition* sleep_cond;
    u8 worker_index;
    u8 last_steal_idx;
    _Atomic(bool) finished;
    _Atomic(bool) is_sleeping;
} Mel__Worker;

static struct {
    Mel__Worker* workers;
    u8 num_workers;

    Mel_Mpmc global_queue;

    Mel__Fiber_Decl* fiber_pool;
    Mel_Fiber_Stack* fiber_stacks;
    Mel_Mpmc free_fibers;
    u32 fiber_pool_size;

    Mel__Work* work_pool;
    Mel_Mpmc free_work;
    u32 work_pool_size;

    Mel__Park_Node* park_pool;

    SDL_Mutex* sleep_mutex;
    _Atomic(i32) num_sleeping;

    SDL_TLSID worker_tls;
} s_sys;

static void mel__manage(Mel_Fiber_Transfer transfer);

static Mel__Worker* mel__get_worker(void)
{
    return (Mel__Worker*)SDL_GetTLS(&s_sys.worker_tls);
}

static u16 mel__fiber_index(Mel__Fiber_Decl* f)
{
    assert(f >= s_sys.fiber_pool && f < s_sys.fiber_pool + s_sys.fiber_pool_size);
    return (u16)(f - s_sys.fiber_pool);
}

static void mel__fiber_assert_not_free(Mel__Fiber_Decl* fiber)
{
    assert(fiber != nullptr);
    assert(fiber->debug_state != MEL__FIBER_STATE_FREE);
}

static Mel__Fiber_Decl* mel__pop_free_fiber(void)
{
    void* f = NULL;
    bool ok = mel_mpmc_pop(&s_sys.free_fibers, &f);
    assert(ok && "free fiber pool exhausted");
    (void)ok;
    Mel__Fiber_Decl* fiber = (Mel__Fiber_Decl*)f;
    assert(fiber->debug_state == MEL__FIBER_STATE_FREE);
    fiber->debug_state = MEL__FIBER_STATE_IDLE;
    return fiber;
}

static void mel__push_free_fiber(Mel__Fiber_Decl* f)
{
    Mel__Worker* worker = mel__get_worker();
    assert(f != nullptr);
    assert(f->debug_state != MEL__FIBER_STATE_FREE);
    if (worker != nullptr)
    {
        assert(worker->current_fiber != f);
        assert(worker->debug_resume_fiber != f);
        assert(worker->fiber_to_push != f);
    }

    u16 fiber_index = mel__fiber_index(f);
    f->fiber = mel_fiber_create(s_sys.fiber_stacks[fiber_index], mel__manage);
    assert(f->fiber != MEL_FIBER_INVALID);
    f->current_job = (Mel__Job){0};
    f->last_job_name = NULL;
    f->last_job_file = NULL;
    f->last_job_task = NULL;
    f->last_job_line = 0;
    f->debug_state = MEL__FIBER_STATE_FREE;
    mel_mpmc_push(&s_sys.free_fibers, f);
}

static Mel__Work* mel__work_alloc(void)
{
    void* w = NULL;
    bool ok = mel_mpmc_pop(&s_sys.free_work, &w);
    assert(ok && "work item pool exhausted");
    (void)ok;
    return (Mel__Work*)w;
}

static void mel__work_free(Mel__Work* w)
{
    mel_mpmc_push(&s_sys.free_work, w);
}

static void mel__wake_worker(u8 idx)
{
    assert(idx < s_sys.num_workers);
    if (!atomic_load_explicit(&s_sys.workers[idx].is_sleeping, memory_order_acquire))
        return;

    SDL_LockMutex(s_sys.sleep_mutex);
    SDL_SignalCondition(s_sys.workers[idx].sleep_cond);
    SDL_UnlockMutex(s_sys.sleep_mutex);
}

static void mel__wake_workers(i32 count)
{
    if (atomic_load_explicit(&s_sys.num_sleeping, memory_order_acquire) == 0)
        return;

    SDL_LockMutex(s_sys.sleep_mutex);
    i32 woken = 0;
    for (u8 i = 0; i < s_sys.num_workers && woken < count; i++)
    {
        if (atomic_load_explicit(&s_sys.workers[i].is_sleeping, memory_order_acquire))
        {
            SDL_SignalCondition(s_sys.workers[i].sleep_cond);
            woken++;
        }
    }
    SDL_UnlockMutex(s_sys.sleep_mutex);
}

static void mel__schedule_fiber(u16 park_index)
{
    assert(park_index < s_sys.fiber_pool_size);
    Mel__Fiber_Decl* fiber = &s_sys.fiber_pool[park_index];
    mel__fiber_assert_not_free(fiber);
    fiber->debug_state = MEL__FIBER_STATE_RUNNABLE;

    Mel__Worker* worker = mel__get_worker();
    Mel__Work* w = mel__work_alloc();
    *w = (Mel__Work){ .type = MEL__WORK_FIBER, .fiber = fiber };

    if (worker)
        mel_wsq_push(&worker->wsq, w);
    else
        mel_mpmc_push(&s_sys.global_queue, w);
}

static void mel__signal_wake_workers_cb(i32 count)
{
    mel__wake_workers(count);
}

static void mel__push_work_job(Mel__Job job, u8 worker_index)
{
    if (worker_index != MEL__JOB_ANY_WORKER)
    {
        u8 idx = worker_index % s_sys.num_workers;
        Mel__Work* w = mel__work_alloc();
        *w = (Mel__Work){ .type = MEL__WORK_JOB, .job = job };
        bool ok = mel_mpmc_push(&s_sys.workers[idx].pinned, w);
        assert(ok && "pinned queue full");
        mel__wake_worker(idx);
        return;
    }

    Mel__Worker* worker = mel__get_worker();
    Mel__Work* w = mel__work_alloc();
    *w = (Mel__Work){ .type = MEL__WORK_JOB, .job = job };
    if (worker)
        mel_wsq_push(&worker->wsq, w);
    else
    {
        bool ok = mel_mpmc_push(&s_sys.global_queue, w);
        assert(ok && "global queue full");
    }
    mel__wake_workers(1);
}

static bool mel__try_pop_work(Mel__Worker* worker, Mel__Work* out)
{
    void* r = NULL;

    if (mel_mpmc_pop(&worker->pinned, &r))
    {
        *out = *(Mel__Work*)r;
        mel__work_free((Mel__Work*)r);
        return true;
    }

    r = mel_wsq_pop(&worker->wsq);
    if (r != MEL_WSQ_EMPTY)
    {
        *out = *(Mel__Work*)r;
        mel__work_free((Mel__Work*)r);
        return true;
    }

    u8 start = worker->last_steal_idx;
    for (u8 i = 0; i < s_sys.num_workers; i++)
    {
        u8 idx = (start + i) % s_sys.num_workers;
        if (idx == worker->worker_index)
            continue;
        r = mel_wsq_steal(&s_sys.workers[idx].wsq);
        if (r != MEL_WSQ_EMPTY && r != MEL_WSQ_ABORT)
        {
            worker->last_steal_idx = idx;
            *out = *(Mel__Work*)r;
            mel__work_free((Mel__Work*)r);
            return true;
        }
    }

    if (mel_mpmc_pop(&s_sys.global_queue, &r))
    {
        *out = *(Mel__Work*)r;
        mel__work_free((Mel__Work*)r);
        return true;
    }

    return false;
}

static bool mel__pop_work(Mel__Worker* worker, Mel__Work* out)
{
    while (!atomic_load_explicit(&worker->finished, memory_order_seq_cst))
    {
        for (u32 spin = 0; spin < MEL_JOB_SPIN_COUNT; spin++)
        {
            if (mel__try_pop_work(worker, out))
                return true;
        }

        atomic_fetch_add_explicit(&s_sys.num_sleeping, 1, memory_order_release);
        atomic_store_explicit(&worker->is_sleeping, true, memory_order_release);

        SDL_LockMutex(s_sys.sleep_mutex);

        if (atomic_load_explicit(&worker->finished, memory_order_seq_cst))
        {
            atomic_fetch_sub_explicit(&s_sys.num_sleeping, 1, memory_order_release);
            atomic_store_explicit(&worker->is_sleeping, false, memory_order_release);
            SDL_UnlockMutex(s_sys.sleep_mutex);
            return false;
        }

        if (mel__try_pop_work(worker, out))
        {
            atomic_fetch_sub_explicit(&s_sys.num_sleeping, 1, memory_order_release);
            atomic_store_explicit(&worker->is_sleeping, false, memory_order_release);
            SDL_UnlockMutex(s_sys.sleep_mutex);
            return true;
        }

        SDL_WaitCondition(worker->sleep_cond, s_sys.sleep_mutex);
        atomic_fetch_sub_explicit(&s_sys.num_sleeping, 1, memory_order_release);
        atomic_store_explicit(&worker->is_sleeping, false, memory_order_release);
        SDL_UnlockMutex(s_sys.sleep_mutex);
    }

    return false;
}

static void mel__after_switch(void)
{
    Mel__Worker* worker = mel__get_worker();

    if (worker->fiber_to_free)
    {
        assert(worker->fiber_to_free != worker->current_fiber);
        mel__push_free_fiber(worker->fiber_to_free);
        worker->fiber_to_free = NULL;
    }

    if (worker->signal_to_park_on)
    {
        Mel_Signal* signal = worker->signal_to_park_on;
        Mel__Fiber_Decl* parked = worker->fiber_to_push;
        mel__fiber_assert_not_free(parked);
        worker->signal_to_park_on = NULL;
        worker->fiber_to_push = NULL;

        parked->fiber = worker->saved_context;
        u16 park_idx = mel__fiber_index(parked);
        s_sys.park_pool[park_idx].fiber = worker->saved_context;

        for (;;)
        {
            i32 old = atomic_load_explicit(&signal->state, memory_order_acquire);
            u16 counter = mel__signal_counter(old);

            if (counter == 0)
            {
                Mel__Work* w = mel__work_alloc();
                *w = (Mel__Work){ .type = MEL__WORK_FIBER, .fiber = parked };
                parked->debug_state = MEL__FIBER_STATE_RUNNABLE;
                mel_wsq_push(&worker->wsq, w);
                mel__wake_workers(1);
                return;
            }

            s_sys.park_pool[park_idx].next = mel__signal_head(old);
            i32 desired = mel__signal_pack(counter, park_idx);
            if (atomic_compare_exchange_weak_explicit(&signal->state, &old, desired,
                                                       memory_order_release,
                                                       memory_order_relaxed))
            {
                parked->debug_state = MEL__FIBER_STATE_PARKED;
                return;
            }
        }
    }

    if (worker->fiber_to_push)
    {
        Mel__Fiber_Decl* fiber = worker->fiber_to_push;
        mel__fiber_assert_not_free(fiber);
        fiber->fiber = worker->saved_context;
        worker->fiber_to_push = NULL;

        i32 target = worker->push_target_worker;
        worker->push_target_worker = -1;

        Mel__Work* w = mel__work_alloc();
        *w = (Mel__Work){ .type = MEL__WORK_FIBER, .fiber = fiber };
        fiber->debug_state = MEL__FIBER_STATE_RUNNABLE;

        if (target >= 0 && target != MEL__JOB_ANY_WORKER)
        {
            u8 idx = (u8)(target % s_sys.num_workers);
            mel_mpmc_push(&s_sys.workers[idx].pinned, w);
            mel__wake_worker(idx);
        }
        else
        {
            mel_mpmc_push(&s_sys.global_queue, w);
            mel__wake_workers(1);
        }
    }
}

static inline void mel__execute_job(Mel__Job job)
{
    Mel__Worker* worker = mel__get_worker();
    if (worker && worker->current_fiber)
    {
        worker->current_fiber->debug_state = MEL__FIBER_STATE_JOB;
        worker->current_fiber->last_job_name = job.debug_name;
        worker->current_fiber->last_job_file = job.debug_file;
        worker->current_fiber->last_job_line = job.debug_line;
        worker->current_fiber->last_job_task = (void*)job.task;
    }

    job.task(job.data);

    if (worker && worker->current_fiber)
        worker->current_fiber->debug_state = MEL__FIBER_STATE_IDLE;

    if (job.dec_on_finish)
        mel_counter_decrement(job.dec_on_finish);
}

static void mel__switch_fibers(void)
{
    Mel__Worker* worker = mel__get_worker();
    Mel__Fiber_Decl* this_fiber = worker->current_fiber;
    mel__fiber_assert_not_free(this_fiber);

    Mel__Fiber_Decl* new_fiber = mel__pop_free_fiber();
    mel__fiber_assert_not_free(new_fiber);
    new_fiber->debug_state = MEL__FIBER_STATE_SWITCHING;
    worker->current_fiber = new_fiber;

    Mel_Fiber_Transfer t = mel_fiber_switch(new_fiber->fiber, new_fiber);
    (void)t;

    mel__after_switch();
    mel__fiber_assert_not_free(this_fiber);
    mel__get_worker()->current_fiber = this_fiber;
}

static void mel__manage(Mel_Fiber_Transfer transfer)
{
    Mel__Worker* worker = mel__get_worker();

    if (worker->primary_fiber == MEL_FIBER_INVALID)
        worker->primary_fiber = transfer.from;

    worker->saved_context = transfer.from;
    worker->debug_resume_fiber = NULL;
    worker->debug_resume_reason = NULL;
    mel__after_switch();

    Mel__Fiber_Decl* this_fiber = (Mel__Fiber_Decl*)transfer.user;
    worker->current_fiber = this_fiber;
    this_fiber->debug_state = MEL__FIBER_STATE_IDLE;

    while (!atomic_load_explicit(&worker->finished, memory_order_relaxed))
    {
        Mel__Work work = {0};
        if (!mel__pop_work(worker, &work))
            break;

        if (work.type == MEL__WORK_FIBER)
        {
            assert(work.fiber != nullptr);
            mel__fiber_assert_not_free(this_fiber);
            mel__fiber_assert_not_free(work.fiber);
            worker->debug_resume_fiber = work.fiber;
            worker->debug_resume_reason = "fiber_resume";
            if (work.fiber)
                work.fiber->debug_state = MEL__FIBER_STATE_SWITCHING;
            worker->current_fiber = work.fiber;
            worker->fiber_to_free = this_fiber;
            Mel_Fiber_Transfer t = mel_fiber_switch(work.fiber->fiber, work.fiber);

            worker = mel__get_worker();
            worker->saved_context = t.from;
            mel__after_switch();

            worker = mel__get_worker();
            mel__fiber_assert_not_free(this_fiber);
            worker->current_fiber = this_fiber;
        }
        else if (work.type == MEL__WORK_JOB)
        {
            worker->debug_resume_fiber = this_fiber;
            worker->debug_resume_reason = "job_execute";
            this_fiber->current_job = work.job;
            mel__execute_job(work.job);
            this_fiber->current_job.task = NULL;
            worker = mel__get_worker();
        }
    }

    mel_fiber_switch(worker->primary_fiber, NULL);
}

static int mel__worker_thread_fn(void* data)
{
    Mel__Worker* worker = (Mel__Worker*)data;
    SDL_SetTLS(&s_sys.worker_tls, worker, NULL);

    Mel__Fiber_Decl* fiber = mel__pop_free_fiber();
    worker->current_fiber = fiber;

    mel_fiber_switch(fiber->fiber, fiber);

    return 0;
}

void mel_job_init(void)
{
    if (s_sys.num_workers > 0) return;
    memset(&s_sys, 0, sizeof(s_sys));

    const Mel_Alloc* alloc = mel_alloc_heap();

    i32 cpus = SDL_GetNumLogicalCPUCores();
    s_sys.num_workers = (u8)(cpus > 1 ? cpus : 1);

    s_sys.fiber_pool_size = MEL_JOB_FIBER_POOL_SIZE;
    s_sys.fiber_pool = mel_alloc(alloc, sizeof(Mel__Fiber_Decl) * s_sys.fiber_pool_size);
    memset(s_sys.fiber_pool, 0, sizeof(Mel__Fiber_Decl) * s_sys.fiber_pool_size);

    s_sys.fiber_stacks = mel_alloc(alloc, sizeof(Mel_Fiber_Stack) * s_sys.fiber_pool_size);
    memset(s_sys.fiber_stacks, 0, sizeof(Mel_Fiber_Stack) * s_sys.fiber_pool_size);

    mel_mpmc_init(&s_sys.free_fibers, s_sys.fiber_pool_size, alloc);

    for (u32 i = 0; i < s_sys.fiber_pool_size; i++)
    {
        bool ok = mel_fiber_stack_init(&s_sys.fiber_stacks[i], MEL_JOB_FIBER_STACK_SIZE);
        assert(ok);
        (void)ok;
        s_sys.fiber_pool[i].fiber = mel_fiber_create(s_sys.fiber_stacks[i], mel__manage);
        s_sys.fiber_pool[i].debug_state = MEL__FIBER_STATE_FREE;
        mel_mpmc_push(&s_sys.free_fibers, &s_sys.fiber_pool[i]);
    }

    s_sys.work_pool_size = MEL_JOB_WORK_POOL_SIZE;
    s_sys.work_pool = mel_alloc(alloc, sizeof(Mel__Work) * s_sys.work_pool_size);
    memset(s_sys.work_pool, 0, sizeof(Mel__Work) * s_sys.work_pool_size);
    mel_mpmc_init(&s_sys.free_work, s_sys.work_pool_size, alloc);
    for (u32 i = 0; i < s_sys.work_pool_size; i++)
        mel_mpmc_push(&s_sys.free_work, &s_sys.work_pool[i]);

    s_sys.park_pool = mel_alloc(alloc, sizeof(Mel__Park_Node) * s_sys.fiber_pool_size);
    memset(s_sys.park_pool, 0, sizeof(Mel__Park_Node) * s_sys.fiber_pool_size);

    mel__signal_init_runtime((Mel__Signal_Runtime){
        .park_pool       = s_sys.park_pool,
        .park_pool_size  = s_sys.fiber_pool_size,
        .schedule_fiber  = mel__schedule_fiber,
        .wake_workers    = mel__signal_wake_workers_cb,
    });

    mel_mpmc_init(&s_sys.global_queue, MEL_JOB_GLOBAL_CAPACITY, alloc);

    s_sys.sleep_mutex = SDL_CreateMutex();

    s_sys.workers = mel_alloc(alloc, sizeof(Mel__Worker) * s_sys.num_workers);
    memset(s_sys.workers, 0, sizeof(Mel__Worker) * s_sys.num_workers);

    for (u8 i = 0; i < s_sys.num_workers; i++)
    {
        Mel__Worker* w = &s_sys.workers[i];
        w->worker_index = i;
        w->push_target_worker = -1;
        w->sleep_cond = SDL_CreateCondition();
        w->wsq = mel_wsq_create(alloc);
        mel_mpmc_init(&w->pinned, MEL_JOB_PINNED_CAPACITY, alloc);
    }

    for (u8 i = 0; i < s_sys.num_workers; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "mel_job(%d)", i);
        s_sys.workers[i].thread = SDL_CreateThread(mel__worker_thread_fn, name, &s_sys.workers[i]);
        assert(s_sys.workers[i].thread);
    }
}

void mel_job_shutdown(void)
{
    if (s_sys.num_workers == 0) return;
    for (u8 i = 0; i < s_sys.num_workers; i++)
        atomic_store_explicit(&s_sys.workers[i].finished, true, memory_order_seq_cst);

    SDL_LockMutex(s_sys.sleep_mutex);
    for (u8 i = 0; i < s_sys.num_workers; i++)
        SDL_SignalCondition(s_sys.workers[i].sleep_cond);
    SDL_UnlockMutex(s_sys.sleep_mutex);

    for (u8 i = 0; i < s_sys.num_workers; i++)
    {
        SDL_WaitThread(s_sys.workers[i].thread, NULL);
        s_sys.workers[i].thread = NULL;
    }

    const Mel_Alloc* alloc = mel_alloc_heap();

    for (u8 i = 0; i < s_sys.num_workers; i++)
    {
        SDL_DestroyCondition(s_sys.workers[i].sleep_cond);
        mel_wsq_destroy(&s_sys.workers[i].wsq);
        mel_mpmc_free(&s_sys.workers[i].pinned);
    }

    mel_mpmc_free(&s_sys.global_queue);
    mel_mpmc_free(&s_sys.free_fibers);
    mel_mpmc_free(&s_sys.free_work);
    SDL_DestroyMutex(s_sys.sleep_mutex);

    for (u32 i = 0; i < s_sys.fiber_pool_size; i++)
        mel_fiber_stack_release(&s_sys.fiber_stacks[i]);

    mel_dealloc(alloc, s_sys.fiber_stacks);
    mel_dealloc(alloc, s_sys.fiber_pool);
    mel_dealloc(alloc, s_sys.work_pool);
    mel_dealloc(alloc, s_sys.park_pool);
    mel_dealloc(alloc, s_sys.workers);

    memset(&s_sys, 0, sizeof(s_sys));
}

void mel_job_run_opt(void* data, Mel_Job_Fn fn, Mel_Counter* on_finish, Mel_Job_Run_Opt opt)
{
    mel_job_run_opt_debug(data, fn, nullptr, nullptr, 0, on_finish, opt);
}

void mel_job_run_opt_debug(void* data, Mel_Job_Fn fn,
                           const char* debug_name,
                           const char* debug_file,
                           u32 debug_line,
                           Mel_Counter* on_finish,
                           Mel_Job_Run_Opt opt)
{
    assert(fn);

    if (on_finish)
        mel_counter_increment(on_finish);

    Mel__Job job = {
        .task = fn,
        .data = data,
        .dec_on_finish = on_finish,
        .worker_index = opt.worker,
        .debug_name = debug_name,
        .debug_file = debug_file,
        .debug_line = debug_line,
    };

    mel__push_work_job(job, opt.worker);
}

void mel_job_run_n_impl(void* data, Mel_Job_Fn fn, Mel_Counter* on_finish, u32 n)
{
    mel_job_run_n_debug(data, fn, nullptr, nullptr, 0, on_finish, n);
}

void mel_job_run_n_debug(void* data, Mel_Job_Fn fn,
                         const char* debug_name,
                         const char* debug_file,
                         u32 debug_line,
                         Mel_Counter* on_finish,
                         u32 n)
{
    assert(fn);
    assert(n > 0);

    for (u32 i = 0; i < n; i++)
    {
        if (on_finish)
            mel_counter_increment(on_finish);

        Mel__Job job = {
            .task = fn,
            .data = data,
            .dec_on_finish = on_finish,
            .worker_index = MEL__JOB_ANY_WORKER,
            .debug_name = debug_name,
            .debug_file = debug_file,
            .debug_line = debug_line,
        };

        Mel__Worker* w = mel__get_worker();
        Mel__Work* work = mel__work_alloc();
        *work = (Mel__Work){ .type = MEL__WORK_JOB, .job = job };
        if (w)
            mel_wsq_push(&w->wsq, work);
        else
            mel_mpmc_push(&s_sys.global_queue, work);
    }

    mel__wake_workers((i32)n);
}

bool mel_job_debug_current(Mel_Job_Debug_Info* out)
{
    assert(out != nullptr);

    *out = (Mel_Job_Debug_Info){0};

    Mel__Worker* worker = mel__get_worker();
    if (worker == nullptr)
        return false;

    out->on_worker = true;
    out->worker_index = worker->worker_index;
    out->on_fiber = worker->current_fiber != nullptr;
    out->resume_reason = worker->debug_resume_reason;

    if (worker->current_fiber != nullptr && worker->current_fiber->current_job.task != nullptr)
    {
        Mel__Job* job = &worker->current_fiber->current_job;
        out->has_current_job = true;
        out->task = (void*)job->task;
        out->debug_name = job->debug_name;
        out->debug_file = job->debug_file;
        out->debug_line = job->debug_line;
    }

    if (worker->current_fiber != nullptr)
    {
        out->fiber_index = mel__fiber_index(worker->current_fiber);
        out->fiber_state = mel__fiber_state_name(worker->current_fiber->debug_state);

        if (!out->has_current_job)
        {
            out->task = worker->current_fiber->last_job_task;
            out->debug_name = worker->current_fiber->last_job_name;
            out->debug_file = worker->current_fiber->last_job_file;
            out->debug_line = worker->current_fiber->last_job_line;
        }
    }

    if (worker->debug_resume_fiber != nullptr)
        out->resume_fiber_index = mel__fiber_index(worker->debug_resume_fiber);

    return true;
}

void mel_job_move_to_worker(u8 worker_index)
{
    Mel__Worker* worker = mel__get_worker();
    assert(worker && "mel_job_move_to_worker must be called from a worker fiber");

    if (worker_index != MEL__JOB_ANY_WORKER && worker->worker_index == worker_index)
        return;

    worker->fiber_to_push = worker->current_fiber;
    worker->push_target_worker = worker_index;

    mel__switch_fibers();
}

void mel_job_yield(void)
{
    mel_job_move_to_worker(MEL__JOB_ANY_WORKER);
}

u8 mel_job_worker_count(void)
{
    return s_sys.num_workers;
}

u8 mel_job_current_worker(void)
{
    Mel__Worker* w = mel__get_worker();
    assert(w);
    return w->worker_index;
}

bool mel_job_is_worker_fiber(void)
{
    return mel__get_worker() != NULL;
}

void mel_signal_wait(Mel_Signal* s)
{
    Mel__Worker* worker = mel__get_worker();
    assert(worker && "mel_signal_wait must be called from a worker fiber");

    if (mel__signal_counter(atomic_load_explicit(&s->state, memory_order_acquire)) == 0)
        return;

    for (u32 i = 0; i < MEL_SIGNAL_SPIN_COUNT; i++)
    {
        if (mel__signal_counter(atomic_load_explicit(&s->state, memory_order_acquire)) == 0)
            return;
    }

    worker->signal_to_park_on = s;
    worker->fiber_to_push = worker->current_fiber;

    mel__switch_fibers();
}

void mel_signal_wait_and_set(Mel_Signal* s)
{
    assert(mel__get_worker() && "mel_signal_wait_and_set must be called from a worker fiber");

    for (;;)
    {
        i32 old = atomic_load_explicit(&s->state, memory_order_acquire);
        u16 counter = mel__signal_counter(old);

        if (counter == 0)
        {
            u16 head = mel__signal_head(old);
            i32 desired = mel__signal_pack(1, head);
            if (atomic_compare_exchange_weak_explicit(&s->state, &old, desired,
                                                       memory_order_acq_rel,
                                                       memory_order_relaxed))
            {
                s->generation = mel__signal_next_generation();
                return;
            }
            continue;
        }

        for (u32 i = 0; i < MEL_SIGNAL_SPIN_COUNT; i++)
        {
            i32 state = atomic_load_explicit(&s->state, memory_order_acquire);
            if (mel__signal_counter(state) == 0)
                goto retry;
        }

        mel_signal_wait(s);
    retry:;
    }
}
