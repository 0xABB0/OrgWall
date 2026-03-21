#include "../melody/test.harness.h"
#include "../melody/async.job.h"
#include "../melody/async.signal.h"

#include <SDL3/SDL.h>
#include <stdatomic.h>

static void mel__test_job_spin_wait_counter(Mel_Counter* c)
{
    while (mel__signal_counter(atomic_load_explicit(&c->signal.state, memory_order_acquire)) != 0)
        SDL_Delay(0);
}

typedef struct {
    _Atomic(i32) completed;
    _Atomic(i32) inside_worker_fiber;
    _Atomic(i32) debug_info_ok;
    i32 yields_per_job;
} Fiber_Reuse_Stress_Data;

static void job_fiber_reuse_stress(void* data)
{
    Fiber_Reuse_Stress_Data* stress = data;

    Mel_Job_Debug_Info info = {0};
    bool has_info = mel_job_debug_current(&info);
    if (has_info && info.on_worker && info.on_fiber)
        atomic_fetch_add_explicit(&stress->inside_worker_fiber, 1, memory_order_relaxed);
    if (has_info && info.on_worker && info.on_fiber && info.has_current_job)
        atomic_fetch_add_explicit(&stress->debug_info_ok, 1, memory_order_relaxed);

    for (i32 i = 0; i < stress->yields_per_job; i++)
        mel_job_yield();

    atomic_fetch_add_explicit(&stress->completed, 1, memory_order_relaxed);
}

MEL_TEST(job_fiber_reuse_after_yield_stress, .tags = "async")
{
    mel_job_init();

    Fiber_Reuse_Stress_Data stress = {
        .completed = 0,
        .inside_worker_fiber = 0,
        .debug_info_ok = 0,
        .yields_per_job = 256,
    };

    i32 worker_count = (i32)mel_job_worker_count();
    i32 job_count = worker_count * 8;
    Mel_Counter counter = MEL_COUNTER_INIT;

    for (i32 i = 0; i < job_count; i++)
        mel_job_run(&stress, job_fiber_reuse_stress, &counter);

    mel__test_job_spin_wait_counter(&counter);

    MEL_ASSERT_EQ(atomic_load_explicit(&stress.completed, memory_order_acquire), job_count);
    MEL_ASSERT_EQ(atomic_load_explicit(&stress.inside_worker_fiber, memory_order_acquire), job_count);
    MEL_ASSERT_EQ(atomic_load_explicit(&stress.debug_info_ok, memory_order_acquire), job_count);

    mel_job_shutdown();
}
