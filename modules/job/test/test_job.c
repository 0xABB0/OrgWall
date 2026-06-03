#include <job/job.h>
#include <executor/executor.h>
#include <signal/signal.h>
#include <test/test.h>

#include <collection.list/list.h>
#include <thread/thread.h>
#include <thread/barrier.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <stdatomic.h>
#include <sys/resource.h>
#include <sys/time.h>

#define JOB_SPIN_LIMIT      200000000
#define JOB_MULTI_THREADS   8
#define JOB_MULTI_PER       2000
#define JOB_COALESCE_REPEAT 1000

static void spin_until(const _Atomic(i32)* flag, i32 want)
{
    for (i64 i = 0; i < JOB_SPIN_LIMIT; i++)
    {
        if (atomic_load_explicit(flag, memory_order_acquire) == want)
            return;
        mel_thread_yield();
    }
}

typedef struct
{
    Mel_Task     task;
    _Atomic(i32) ran;
    i32          tag;
    u8           saw_worker;
    u8           saw_fiber;
} Probe;

static void probe_run(Mel_Task* self)
{
    Probe* p = mel_container_of(self, Probe, task);
    p->saw_worker = mel_job_is_worker_fiber() ? 1 : 0;

    Mel_Job_Debug_Info info = { 0 };
    if (mel_job_debug_current(&info))
        p->saw_fiber = info.on_fiber ? 1 : 0;

    atomic_fetch_add_explicit(&p->ran, 1, memory_order_release);
}

MEL_TEST(job_exec, plain_task_runs_on_worker)
{
    mel_job_init();

    Mel_Executor* exec = mel_job_executor();
    MEL_REQUIRE_NOT_NULL(exec);

    Probe p = { 0 };
    p.tag = 0x1234;
    mel_task_init(&p.task, probe_run);

    mel_executor_submit(exec, &p.task);

    spin_until(&p.ran, 1);

    MEL_EXPECT_EQ(atomic_load(&p.ran), 1);
    MEL_EXPECT_EQ(p.tag, 0x1234);
    MEL_EXPECT_EQ((int)p.saw_worker, 1);
    MEL_EXPECT_EQ((int)p.saw_fiber, 1);

    mel_job_shutdown();
}

MEL_TEST(job_exec, submit_before_run_no_double_dispatch)
{
    mel_job_init();

    Mel_Executor* exec = mel_job_executor();

    Probe p = { 0 };
    mel_task_init(&p.task, probe_run);

    i32  expected = 0;
    bool first = atomic_compare_exchange_strong(&p.task.armed, &expected, 1);
    MEL_REQUIRE(first);

    mel_executor_submit(exec, &p.task);
    mel_executor_submit(exec, &p.task);

    mel_thread_sleep(50 * 1000 * 1000);

    MEL_EXPECT_EQ(atomic_load(&p.ran), 0);

    atomic_store_explicit(&p.task.armed, 0, memory_order_release);
    mel_executor_submit(exec, &p.task);
    spin_until(&p.ran, 1);
    MEL_EXPECT_EQ(atomic_load(&p.ran), 1);

    mel_job_shutdown();
}

typedef struct
{
    Mel_Task     task;
    Mel_Counter* gate;
    _Atomic(i32) phase;
} Blocker;

static void blocker_run(Mel_Task* self)
{
    Blocker* b = mel_container_of(self, Blocker, task);
    atomic_store_explicit(&b->phase, 1, memory_order_release);
    mel_counter_wait(b->gate);
    atomic_store_explicit(&b->phase, 2, memory_order_release);
}

MEL_TEST(job_exec, task_blocks_on_counter_parks_then_resumes)
{
    mel_job_init();

    Mel_Executor* exec = mel_job_executor();

    Mel_Counter gate = MEL_COUNTER_INIT;
    mel_counter_increment(&gate);

    Blocker b = { 0 };
    b.gate = &gate;
    mel_task_init(&b.task, blocker_run);

    mel_executor_submit(exec, &b.task);

    spin_until(&b.phase, 1);
    MEL_EXPECT_EQ(atomic_load(&b.phase), 1);

    mel_thread_sleep(20 * 1000 * 1000);
    MEL_EXPECT_EQ(atomic_load(&b.phase), 1);

    mel_counter_decrement(&gate);

    spin_until(&b.phase, 2);
    MEL_EXPECT_EQ(atomic_load(&b.phase), 2);

    mel_job_shutdown();
}

typedef struct
{
    Mel_Task     task;
    _Atomic(i32) ran;
} Coalesce;

static void coalesce_run(Mel_Task* self)
{
    Coalesce* c = mel_container_of(self, Coalesce, task);
    atomic_fetch_add_explicit(&c->ran, 1, memory_order_release);
}

MEL_TEST(job_exec, armed_coalesces_repeated_submits)
{
    mel_job_init();

    Mel_Executor* exec = mel_job_executor();

    Coalesce c = { 0 };
    mel_task_init(&c.task, coalesce_run);

    i32  expected = 0;
    bool armed = atomic_compare_exchange_strong(&c.task.armed, &expected, 1);
    MEL_REQUIRE(armed);

    for (i32 i = 0; i < JOB_COALESCE_REPEAT; i++)
        mel_executor_submit(exec, &c.task);

    mel_thread_sleep(50 * 1000 * 1000);
    MEL_EXPECT_EQ(atomic_load(&c.ran), 0);

    atomic_store_explicit(&c.task.armed, 0, memory_order_release);
    mel_executor_submit(exec, &c.task);
    spin_until(&c.ran, 1);
    MEL_EXPECT_EQ(atomic_load(&c.ran), 1);

    mel_job_shutdown();
}

MEL_TEST(job_exec, resubmit_waker_lands_the_task)
{
    mel_job_init();

    Mel_Executor* exec = mel_job_executor();

    Coalesce c = { 0 };
    mel_task_init(&c.task, coalesce_run);

    Mel_Resubmit_Cell cell = { .exec = exec, .task = &c.task };
    Mel_Waker         waker = mel_resubmit_waker(&cell);

    waker.wake(waker.user);
    spin_until(&c.ran, 1);
    MEL_EXPECT_EQ(atomic_load(&c.ran), 1);

    mel_job_shutdown();
}

typedef struct
{
    Mel_Task     task;
    _Atomic(i32) ran;
    i32          rearmed;
    _Atomic(i32) gen;
} Rearm;

static void rearm_run(Mel_Task* self)
{
    Rearm* r = mel_container_of(self, Rearm, task);
    atomic_fetch_add_explicit(&r->ran, 1, memory_order_release);
    if (r->rearmed == 0)
    {
        r->rearmed = 1;
        mel_executor_submit(mel_job_executor(), &r->task);
    }
}

MEL_TEST(job_exec, resubmit_from_inside_run_reruns)
{
    mel_job_init();

    Mel_Executor* exec = mel_job_executor();

    Rearm r = { 0 };
    mel_task_init(&r.task, rearm_run);

    mel_executor_submit(exec, &r.task);

    spin_until(&r.ran, 2);
    MEL_EXPECT_EQ(atomic_load(&r.ran), 2);

    mel_job_shutdown();
}

typedef struct
{
    Mel_Task     task;
    _Atomic(i32) ran;
} Counted;

static void counted_run(Mel_Task* self)
{
    Counted* c = mel_container_of(self, Counted, task);
    atomic_fetch_add_explicit(&c->ran, 1, memory_order_relaxed);
}

typedef struct
{
    Mel_Executor* exec;
    Mel_Barrier*  start;
    Counted*      tasks;
    i32           first;
    i32           count;
} Submitter_Ctx;

static int submitter_main(void* user)
{
    Submitter_Ctx* ctx = (Submitter_Ctx*)user;
    mel_barrier_wait(ctx->start);
    for (i32 i = 0; i < ctx->count; i++)
        mel_executor_submit(ctx->exec, &ctx->tasks[ctx->first + i].task);
    return 0;
}

MEL_TEST(job_exec, many_tasks_from_many_threads_each_run_once)
{
    mel_job_init();

    Mel_Executor* exec = mel_job_executor();

    const i32        total = JOB_MULTI_THREADS * JOB_MULTI_PER;
    const Mel_Alloc* alloc = mel_alloc_heap();

    Counted* tasks = mel_alloc(alloc, sizeof(Counted) * (usize)total);
    MEL_REQUIRE_NOT_NULL(tasks);
    for (i32 i = 0; i < total; i++)
    {
        tasks[i] = (Counted){ 0 };
        mel_task_init(&tasks[i].task, counted_run);
    }

    Mel_Barrier start;
    MEL_REQUIRE(mel_barrier_init(&start, JOB_MULTI_THREADS + 1));

    Submitter_Ctx ctx[JOB_MULTI_THREADS];
    Mel_Thread    threads[JOB_MULTI_THREADS];
    for (i32 t = 0; t < JOB_MULTI_THREADS; t++)
    {
        ctx[t] = (Submitter_Ctx){ .exec = exec, .start = &start, .tasks = tasks, .first = t * JOB_MULTI_PER, .count = JOB_MULTI_PER };
        MEL_REQUIRE(mel_thread_spawn(&threads[t], submitter_main, &ctx[t], .name = "job-submit"));
    }

    mel_barrier_wait(&start);

    for (i32 t = 0; t < JOB_MULTI_THREADS; t++)
    {
        int rc = 0;
        mel_thread_join(&threads[t], &rc);
    }

    bool all_one = false;
    for (i64 spin = 0; spin < JOB_SPIN_LIMIT; spin++)
    {
        i64 sum = 0;
        for (i32 i = 0; i < total; i++)
            sum += atomic_load_explicit(&tasks[i].ran, memory_order_acquire);
        if (sum == total)
        {
            all_one = true;
            break;
        }
        mel_thread_yield();
    }

    MEL_REQUIRE(all_one);
    for (i32 i = 0; i < total; i++)
        MEL_EXPECT_EQ(atomic_load(&tasks[i].ran), 1);

    mel_barrier_destroy(&start);
    mel_dealloc(alloc, tasks);

    mel_job_shutdown();
}

typedef struct
{
    Mel_Task     task;
    Mel_Counter* done;
    _Atomic(i32) ran;
} Inner;

static void inner_task_run(Mel_Task* self)
{
    Inner* in = mel_container_of(self, Inner, task);
    atomic_fetch_add_explicit(&in->ran, 1, memory_order_release);
    mel_counter_decrement(in->done);
}

static void submit_from_worker(void* data)
{
    Inner* in = (Inner*)data;
    mel_executor_submit(mel_job_executor(), &in->task);
}

MEL_TEST(job_exec, submit_from_worker_fiber)
{
    mel_job_init();

    Mel_Counter done = MEL_COUNTER_INIT;
    mel_counter_increment(&done);

    Inner in = { 0 };
    in.done = &done;
    mel_task_init(&in.task, inner_task_run);

    mel_job_run(&in, submit_from_worker, nullptr);

    spin_until(&in.ran, 1);
    MEL_EXPECT_EQ(atomic_load(&in.ran), 1);

    mel_job_shutdown();
}

typedef struct
{
    Mel_Task     task;
    void*        recovered;
    void*        self_ptr;
    _Atomic(i32) ran;
} Recover;

static void recover_run(Mel_Task* self)
{
    Recover* r = mel_container_of(self, Recover, task);
    r->recovered = r;
    atomic_fetch_add_explicit(&r->ran, 1, memory_order_release);
}

MEL_TEST(job_exec, container_of_recovers_owner)
{
    mel_job_init();

    Recover r = { 0 };
    r.self_ptr = &r;
    mel_task_init(&r.task, recover_run);

    mel_executor_submit(mel_job_executor(), &r.task);

    spin_until(&r.ran, 1);
    MEL_EXPECT_EQ(atomic_load(&r.ran), 1);
    MEL_EXPECT_EQ(r.recovered, r.self_ptr);

    mel_job_shutdown();
}

MEL_TEST(job_exec, single_task_quiesce_loop_no_strand)
{
    mel_job_init();

    Mel_Executor* exec = mel_job_executor();

    Coalesce c = { 0 };
    mel_task_init(&c.task, coalesce_run);

    const i32 rounds = 200000;
    for (i32 r = 0; r < rounds; r++)
    {
        atomic_store_explicit(&c.ran, 0, memory_order_relaxed);
        atomic_store_explicit(&c.task.armed, 0, memory_order_release);
        mel_executor_submit(exec, &c.task);

        bool landed = false;
        for (i64 spin = 0; spin < JOB_SPIN_LIMIT; spin++)
        {
            if (atomic_load_explicit(&c.ran, memory_order_acquire) == 1)
            {
                landed = true;
                break;
            }
            mel_thread_yield();
        }
        if (!landed)
        {
            MEL_EXPECT_EQ(r, -1);
            break;
        }
    }

    mel_job_shutdown();
}

MEL_TEST(job_exec, idle_sleep_wake_no_lost_wakeup)
{
    mel_job_init();

    Mel_Executor* exec = mel_job_executor();

    Coalesce c = { 0 };
    mel_task_init(&c.task, coalesce_run);

    const i32 rounds = 4000;
    for (i32 r = 0; r < rounds; r++)
    {
        mel_thread_sleep(300 * 1000);

        atomic_store_explicit(&c.ran, 0, memory_order_relaxed);
        atomic_store_explicit(&c.task.armed, 0, memory_order_release);
        mel_executor_submit(exec, &c.task);

        bool landed = false;
        for (i64 spin = 0; spin < JOB_SPIN_LIMIT; spin++)
        {
            if (atomic_load_explicit(&c.ran, memory_order_acquire) == 1)
            {
                landed = true;
                break;
            }
            mel_thread_yield();
        }
        if (!landed)
        {
            MEL_EXPECT_EQ(r, -1);
            break;
        }
    }

    mel_job_shutdown();
}

typedef struct
{
    Mel_Task     task;
    _Atomic(i32) ran;
} Inject_Probe;

static void inject_probe_run(Mel_Task* self)
{
    Inject_Probe* p = mel_container_of(self, Inject_Probe, task);
    atomic_store_explicit(&p->ran, 1, memory_order_release);
}

static void submit_inject_probe(void* data)
{
    Inject_Probe* p = (Inject_Probe*)data;
    mel_executor_submit(mel_job_executor(), &p->task);
}

MEL_TEST(job_exec, job_run_idle_sleep_wake_no_lost_wakeup)
{
    mel_job_init();

    const i32 rounds = 4000;
    for (i32 r = 0; r < rounds; r++)
    {
        mel_thread_sleep(300 * 1000);

        Inject_Probe p = { 0 };
        mel_task_init(&p.task, inject_probe_run);

        mel_job_run(&p, submit_inject_probe, nullptr);

        bool landed = false;
        for (i64 spin = 0; spin < JOB_SPIN_LIMIT; spin++)
        {
            if (atomic_load_explicit(&p.ran, memory_order_acquire) == 1)
            {
                landed = true;
                break;
            }
            mel_thread_yield();
        }
        if (!landed)
        {
            MEL_EXPECT_EQ(r, -1);
            break;
        }
    }

    mel_job_shutdown();
}

typedef struct
{
    Mel_Executor* exec;
    Mel_Barrier*  start;
    Counted*      tasks;
    i32           first;
    i32           per;
    i32           reps;
} Storm_Ctx;

static int storm_main(void* user)
{
    Storm_Ctx* ctx = (Storm_Ctx*)user;
    mel_barrier_wait(ctx->start);
    for (i32 rep = 0; rep < ctx->reps; rep++)
        for (i32 i = 0; i < ctx->per; i++)
        {
            Counted* t = &ctx->tasks[ctx->first + i];
            atomic_store_explicit(&t->task.armed, 0, memory_order_release);
            mel_executor_submit(ctx->exec, &t->task);
            for (i64 spin = 0; spin < JOB_SPIN_LIMIT; spin++)
            {
                if (atomic_load_explicit(&t->ran, memory_order_acquire) == rep + 1)
                    break;
                mel_thread_yield();
            }
        }
    return 0;
}

MEL_TEST(job_exec, submit_storm_strict_quiescence)
{
    mel_job_init();

    Mel_Executor* exec = mel_job_executor();

    const i32        threads_n = JOB_MULTI_THREADS;
    const i32        per = 64;
    const i32        reps = 250;
    const i32        total = threads_n * per;
    const Mel_Alloc* alloc = mel_alloc_heap();

    Counted* tasks = mel_alloc(alloc, sizeof(Counted) * (usize)total);
    MEL_REQUIRE_NOT_NULL(tasks);
    for (i32 i = 0; i < total; i++)
    {
        tasks[i] = (Counted){ 0 };
        mel_task_init(&tasks[i].task, counted_run);
    }

    Mel_Barrier start;
    MEL_REQUIRE(mel_barrier_init(&start, threads_n + 1));

    Storm_Ctx  ctx[JOB_MULTI_THREADS];
    Mel_Thread th[JOB_MULTI_THREADS];
    for (i32 t = 0; t < threads_n; t++)
    {
        ctx[t] = (Storm_Ctx){ .exec = exec, .start = &start, .tasks = tasks, .first = t * per, .per = per, .reps = reps };
        MEL_REQUIRE(mel_thread_spawn(&th[t], storm_main, &ctx[t], .name = "job-storm"));
    }

    mel_barrier_wait(&start);

    for (i32 t = 0; t < threads_n; t++)
    {
        int rc = 0;
        mel_thread_join(&th[t], &rc);
    }

    for (i32 i = 0; i < total; i++)
        MEL_EXPECT_EQ(atomic_load(&tasks[i].ran), reps);

    mel_thread_sleep(50 * 1000 * 1000);

    for (i32 i = 0; i < total; i++)
        MEL_EXPECT_EQ(atomic_load(&tasks[i].ran), reps);

    mel_barrier_destroy(&start);
    mel_dealloc(alloc, tasks);

    mel_job_shutdown();
}

static i64 cpu_micros(void)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    i64 u = (i64)ru.ru_utime.tv_sec * 1000000 + ru.ru_utime.tv_usec;
    i64 s = (i64)ru.ru_stime.tv_sec * 1000000 + ru.ru_stime.tv_usec;
    return u + s;
}

MEL_TEST(job_exec, idle_pool_truly_parks_no_spin)
{
    mel_job_init();

    Mel_Executor* exec = mel_job_executor();

    Probe warm = { 0 };
    mel_task_init(&warm.task, probe_run);
    mel_executor_submit(exec, &warm.task);
    spin_until(&warm.ran, 1);

    mel_thread_sleep(50 * 1000 * 1000);

    const i64 idle_ns = 300 * 1000 * 1000;
    i64       before = cpu_micros();
    mel_thread_sleep(idle_ns);
    i64 after = cpu_micros();

    i64 cpu_used = after - before;
    i64 wall_us = idle_ns / 1000;
    i64 budget = wall_us / 4;

    MEL_EXPECT(cpu_used < budget);

    mel_job_shutdown();
}

MEL_TEST(job_exec, lifecycle_reinit_after_shutdown)
{
    mel_job_init();
    mel_job_shutdown();

    mel_job_init();

    Probe p = { 0 };
    mel_task_init(&p.task, probe_run);
    mel_executor_submit(mel_job_executor(), &p.task);
    spin_until(&p.ran, 1);
    MEL_EXPECT_EQ(atomic_load(&p.ran), 1);

    mel_job_shutdown();
}
