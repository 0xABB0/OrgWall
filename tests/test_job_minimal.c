#include "../melody/test.harness.h"
#include "../melody/async.job.h"
#include "../melody/async.signal.h"

#include <SDL3/SDL.h>

static void noop_job(void* data)
{
    (void)data;
}

MEL_TEST(job_minimal_noop, .tags = "async")
{
    mel_job_init();
    mel_job_shutdown();
}

MEL_TEST(job_minimal_dispatch, .tags = "async")
{
    mel_job_init();

    Mel_Counter counter = MEL_COUNTER_INIT;
    mel_job_run(NULL, noop_job, &counter);

    while (mel__signal_counter(atomic_load_explicit(&counter.signal.state, memory_order_seq_cst)) != 0)
        SDL_Delay(1);

    mel_job_shutdown();
}

MEL_TEST(job_minimal_init_shutdown_x10, .tags = "async")
{
    for (i32 i = 0; i < 10; i++)
    {
        mel_job_init();
        mel_job_shutdown();
    }
}
