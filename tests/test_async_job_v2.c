#include "../melody/test.harness.h"
#include "../melody/async.job.h"
#include "../melody/async.signal.h"

#include <SDL3/SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <execinfo.h>

static void mel__test_segv_handler(int sig)
{
    void* bt[32];
    int n = backtrace(bt, 32);
    fprintf(stderr, "\n=== SIGSEGV caught ===\n");
    backtrace_symbols_fd(bt, n, 2);
    _exit(139);
}

__attribute__((constructor))
static void mel__test_install_segv_handler(void)
{
    static char altstack[SIGSTKSZ];
    stack_t ss = { .ss_sp = altstack, .ss_size = sizeof(altstack), .ss_flags = 0 };
    sigaltstack(&ss, NULL);

    struct sigaction sa = {0};
    sa.sa_handler = mel__test_segv_handler;
    sa.sa_flags = SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
}

static void mel__test_job_spin_wait_counter(Mel_Counter* c)
{
    while (mel__signal_counter(atomic_load_explicit(&c->signal.state, memory_order_acquire)) != 0)
        SDL_Delay(0);
}

static void job_set_flag(void* data)
{
    _Atomic(bool)* flag = data;
    atomic_store_explicit(flag, true, memory_order_release);
}

MEL_TEST(job_single_dispatch, .tags = "async")
{
    mel_job_init();

    _Atomic(bool) done = false;
    Mel_Counter counter = MEL_COUNTER_INIT;

    mel_job_run(&done, job_set_flag, &counter);
    mel__test_job_spin_wait_counter(&counter);

    MEL_ASSERT(atomic_load(&done));

    mel_job_shutdown();
}

static void job_increment_atomic(void* data)
{
    _Atomic(i32)* val = data;
    atomic_fetch_add_explicit(val, 1, memory_order_relaxed);
}

MEL_TEST(job_run_n, .tags = "async")
{
    mel_job_init();

    _Atomic(i32) sum = 0;
    Mel_Counter counter = MEL_COUNTER_INIT;

    mel_job_run_n(&sum, job_increment_atomic, &counter, 8);
    mel__test_job_spin_wait_counter(&counter);

    MEL_ASSERT_EQ(atomic_load(&sum), 8);

    mel_job_shutdown();
}

MEL_TEST(job_fire_and_forget, .tags = "async")
{
    mel_job_init();

    _Atomic(bool) done = false;
    mel_job_run(&done, job_set_flag, NULL);

    while (!atomic_load_explicit(&done, memory_order_acquire))
        SDL_Delay(0);

    MEL_ASSERT(atomic_load(&done));

    mel_job_shutdown();
}

static void job_worker_count(void* data)
{
    u8* out = data;
    *out = mel_job_worker_count();
}

MEL_TEST(job_worker_count, .tags = "async")
{
    mel_job_init();

    u8 count = 0;
    Mel_Counter counter = MEL_COUNTER_INIT;
    mel_job_run(&count, job_worker_count, &counter);
    mel__test_job_spin_wait_counter(&counter);

    MEL_ASSERT_GT(count, (u8)0);
    MEL_ASSERT_EQ(count, mel_job_worker_count());

    mel_job_shutdown();
}

MEL_TEST(job_init_shutdown_only, .tags = "async")
{
    mel_job_init();
    mel_job_shutdown();
}

MEL_TEST(job_is_worker_fiber, .tags = "async")
{
    mel_job_init();

    MEL_ASSERT(!mel_job_is_worker_fiber());

    mel_job_shutdown();
}

typedef struct {
    _Atomic(i32) sum;
    i32 n;
} Nested_Data;

static void job_nested_child(void* data)
{
    Nested_Data* nd = data;
    atomic_fetch_add_explicit(&nd->sum, 1, memory_order_relaxed);
}

static void job_nested_parent(void* data)
{
    Nested_Data* nd = data;

    Mel_Counter child_counter = MEL_COUNTER_INIT;
    for (i32 i = 0; i < nd->n; i++)
        mel_job_run(nd, job_nested_child, &child_counter);

    mel_counter_wait(&child_counter);
}

MEL_TEST(job_nested_fork_join, .tags = "async")
{
    mel_job_init();

    Nested_Data nd = { .sum = 0, .n = 5 };
    Mel_Counter counter = MEL_COUNTER_INIT;

    mel_job_run(&nd, job_nested_parent, &counter);
    mel__test_job_spin_wait_counter(&counter);

    MEL_ASSERT_EQ(atomic_load(&nd.sum), 5);

    mel_job_shutdown();
}

typedef struct {
    _Atomic(i32) work_index;
    _Atomic(i32) processed;
    i32 total;
} Parallel_For_Data;

static void job_parallel_for_worker(void* data)
{
    Parallel_For_Data* pf = data;

    for (;;)
    {
        i32 idx = atomic_fetch_add_explicit(&pf->work_index, 1, memory_order_relaxed);
        if (idx >= pf->total)
            break;
        atomic_fetch_add_explicit(&pf->processed, 1, memory_order_relaxed);
    }
}

MEL_TEST(job_parallel_for, .tags = "async")
{
    mel_job_init();

    Parallel_For_Data pf = { .work_index = 0, .processed = 0, .total = 1000 };
    Mel_Counter counter = MEL_COUNTER_INIT;

    u8 n = mel_job_worker_count();
    mel_job_run_n(&pf, job_parallel_for_worker, &counter, n);
    mel__test_job_spin_wait_counter(&counter);

    MEL_ASSERT_EQ(atomic_load(&pf.processed), 1000);

    mel_job_shutdown();
}

static void job_move_to_zero(void* data)
{
    u8* out = data;
    mel_job_move_to_worker(0);
    *out = mel_job_current_worker();
}

MEL_TEST(job_move_to_worker, .tags = "async")
{
    mel_job_init();

    u8 result = 0xFF;
    Mel_Counter counter = MEL_COUNTER_INIT;
    mel_job_run(&result, job_move_to_zero, &counter);
    mel__test_job_spin_wait_counter(&counter);

    MEL_ASSERT_EQ(result, (u8)0);

    mel_job_shutdown();
}

static void job_yield_fn(void* data)
{
    _Atomic(i32)* val = data;
    atomic_fetch_add_explicit(val, 1, memory_order_relaxed);
    mel_job_yield();
    atomic_fetch_add_explicit(val, 1, memory_order_relaxed);
}

MEL_TEST(job_yield, .tags = "async")
{
    mel_job_init();

    _Atomic(i32) val = 0;
    Mel_Counter counter = MEL_COUNTER_INIT;
    mel_job_run(&val, job_yield_fn, &counter);
    mel__test_job_spin_wait_counter(&counter);

    MEL_ASSERT_EQ(atomic_load(&val), 2);

    mel_job_shutdown();
}

MEL_TEST(job_stress_10000, .tags = "async")
{
    mel_job_init();

    _Atomic(i32) sum = 0;
    Mel_Counter counter = MEL_COUNTER_INIT;

    for (i32 i = 0; i < 10000; i++)
        mel_job_run(&sum, job_increment_atomic, &counter);

    mel__test_job_spin_wait_counter(&counter);

    MEL_ASSERT_EQ(atomic_load(&sum), 10000);

    mel_job_shutdown();
}

typedef struct {
    Mel_Mutex* mtx;
    i32* shared;
    i32 increments;
} Mutex_Data;

static void job_mutex_increment(void* data)
{
    Mutex_Data* md = data;
    for (i32 i = 0; i < md->increments; i++)
    {
        mel_mutex_enter(md->mtx);
        (*md->shared)++;
        mel_mutex_exit(md->mtx);
    }
}

MEL_TEST(job_mutex_correctness, .tags = "async")
{
    mel_job_init();

    Mel_Mutex mtx = MEL_MUTEX_INIT;
    i32 shared = 0;
    Mel_Counter counter = MEL_COUNTER_INIT;

    Mutex_Data md = { .mtx = &mtx, .shared = &shared, .increments = 100 };

    for (i32 i = 0; i < 8; i++)
        mel_job_run(&md, job_mutex_increment, &counter);

    mel__test_job_spin_wait_counter(&counter);

    MEL_ASSERT_EQ(shared, 800);

    mel_job_shutdown();
}

typedef struct {
    _Atomic(i32)* result;
    i32 n;
} Fib_Data;

static void job_fib(void* data);

static Fib_Data s_fib_pool[65536];
static _Atomic(i32) s_fib_pool_idx;

static void job_fib(void* data)
{
    Fib_Data* fd = data;
    if (fd->n <= 1)
    {
        atomic_store_explicit(fd->result, fd->n, memory_order_release);
        return;
    }

    _Atomic(i32) a = 0, b = 0;
    Mel_Counter counter = MEL_COUNTER_INIT;

    i32 ia = atomic_fetch_add_explicit(&s_fib_pool_idx, 1, memory_order_relaxed);
    i32 ib = atomic_fetch_add_explicit(&s_fib_pool_idx, 1, memory_order_relaxed);
    s_fib_pool[ia] = (Fib_Data){ .result = &a, .n = fd->n - 1 };
    s_fib_pool[ib] = (Fib_Data){ .result = &b, .n = fd->n - 2 };

    mel_job_run(&s_fib_pool[ia], job_fib, &counter);
    mel_job_run(&s_fib_pool[ib], job_fib, &counter);
    mel_counter_wait(&counter);

    atomic_store_explicit(fd->result, atomic_load(&a) + atomic_load(&b), memory_order_release);
}

MEL_TEST(job_recursive_fibonacci, .tags = "async")
{
    mel_job_init();

    atomic_store(&s_fib_pool_idx, 0);
    _Atomic(i32) result = 0;
    Mel_Counter counter = MEL_COUNTER_INIT;

    Fib_Data root = { .result = &result, .n = 15 };
    mel_job_run(&root, job_fib, &counter);
    mel__test_job_spin_wait_counter(&counter);

    MEL_ASSERT_EQ(atomic_load(&result), 610);

    mel_job_shutdown();
}
