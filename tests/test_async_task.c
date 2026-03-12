#include "../melody/test.harness.h"
#include "../melody/async.task.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"

static u32 g_exec_order[32];
static u32 g_exec_count;

static void reset_exec_tracker(void)
{
    g_exec_count = 0;
    memset(g_exec_order, 0, sizeof(g_exec_order));
}

static Mel_Task_Step_Result step_track_order(Mel_Task_Ctx* ctx, void* user_data)
{
    (void)ctx;
    u32 id = (u32)(usize)user_data;
    g_exec_order[g_exec_count++] = id;
    return (Mel_Task_Step_Result){ .result = MEL_TASK_STEP_DONE };
}

static Mel_Task_Step_Result step_fail(Mel_Task_Ctx* ctx, void* user_data)
{
    (void)ctx;
    (void)user_data;
    return (Mel_Task_Step_Result){ .result = MEL_TASK_STEP_FAILED };
}

typedef struct {
    Mel_Task_Handle handle;
    u32 status;
    u32 called;
} Complete_Ctx;

static void on_complete_tracker(Mel_Task_Handle handle, u32 status, void* user)
{
    Complete_Ctx* c = user;
    c->handle = handle;
    c->status = status;
    c->called++;
}

static Mel_Task_Ctx* make_ctx(void)
{
    Mel_Task_Ctx_Desc desc = { .alloc = mel_alloc_heap() };
    return mel_task_ctx_create(&desc);
}

typedef struct {
    Mel_Task_Ctx*     task_ctx;
    Mel_Job_Context*  job_ctx;
} Task_Job_Ctx;

static Task_Job_Ctx make_ctx_with_jobs(void)
{
    Task_Job_Ctx ctx = {
        .job_ctx = mel_job_create_context_opt(mel_alloc_heap(), (Mel_Job_Context_Opt){
            .num_threads = 1,
            .max_fibers = 64,
            .fiber_stack_sz = 1048576,
        }),
    };

    Mel_Task_Ctx_Desc desc = {
        .alloc = mel_alloc_heap(),
        .jobs = ctx.job_ctx,
    };
    ctx.task_ctx = mel_task_ctx_create(&desc);
    return ctx;
}

MEL_TEST(task_single_step_immediate, .tags = "async")
{
    Mel_Task_Ctx* ctx = make_ctx();
    reset_exec_tracker();

    Mel_Task_Handle t = mel_task_begin(ctx);
    mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)42 });
    mel_task_submit(ctx, t);

    MEL_ASSERT_EQ(mel_task_status(ctx, t), (u32)MEL_TASK_STATUS_RUNNING);

    mel_task_tick(ctx);

    MEL_ASSERT(mel_task_is_done(ctx, t));
    MEL_ASSERT_EQ(mel_task_status(ctx, t), (u32)MEL_TASK_STATUS_DONE);
    MEL_ASSERT_EQ(g_exec_count, 1u);
    MEL_ASSERT_EQ(g_exec_order[0], 42u);

    mel_task_release(ctx, t);
    mel_task_ctx_destroy(ctx);
}

MEL_TEST(task_fan_out_no_deps, .tags = "async")
{
    Mel_Task_Ctx* ctx = make_ctx();
    reset_exec_tracker();

    Mel_Task_Handle t = mel_task_begin(ctx);
    mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)1 });
    mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)2 });
    mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)3 });
    mel_task_submit(ctx, t);

    mel_task_tick(ctx);

    MEL_ASSERT(mel_task_is_done(ctx, t));
    MEL_ASSERT_EQ(g_exec_count, 3u);

    mel_task_release(ctx, t);
    mel_task_ctx_destroy(ctx);
}

MEL_TEST(task_chain_deps, .tags = "async")
{
    Mel_Task_Ctx* ctx = make_ctx();
    reset_exec_tracker();

    Mel_Task_Handle t = mel_task_begin(ctx);
    u32 s0 = mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)10 });
    u32 s1 = mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)20 });
    u32 s2 = mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)30 });
    mel_task_add_dep(ctx, t, s1, s0);
    mel_task_add_dep(ctx, t, s2, s1);
    mel_task_submit(ctx, t);

    mel_task_tick(ctx);

    MEL_ASSERT(mel_task_is_done(ctx, t));
    MEL_ASSERT_EQ(g_exec_count, 3u);
    MEL_ASSERT_EQ(g_exec_order[0], 10u);
    MEL_ASSERT_EQ(g_exec_order[1], 20u);
    MEL_ASSERT_EQ(g_exec_order[2], 30u);

    mel_task_release(ctx, t);
    mel_task_ctx_destroy(ctx);
}

MEL_TEST(task_fan_in, .tags = "async")
{
    Mel_Task_Ctx* ctx = make_ctx();
    reset_exec_tracker();

    Mel_Task_Handle t = mel_task_begin(ctx);
    u32 s0 = mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)1 });
    u32 s1 = mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)2 });
    u32 s2 = mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)3 });
    mel_task_add_dep(ctx, t, s2, s0);
    mel_task_add_dep(ctx, t, s2, s1);
    mel_task_submit(ctx, t);

    mel_task_tick(ctx);

    MEL_ASSERT(mel_task_is_done(ctx, t));
    MEL_ASSERT_EQ(g_exec_count, 3u);
    MEL_ASSERT_EQ(g_exec_order[0], 1u);
    MEL_ASSERT_EQ(g_exec_order[1], 2u);
    MEL_ASSERT_EQ(g_exec_order[2], 3u);

    mel_task_release(ctx, t);
    mel_task_ctx_destroy(ctx);
}

MEL_TEST(task_progress, .tags = "async")
{
    Mel_Task_Ctx* ctx = make_ctx();
    reset_exec_tracker();

    Mel_Task_Handle t = mel_task_begin(ctx);
    mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)1 });
    mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)2 });
    mel_task_submit(ctx, t);

    MEL_ASSERT_FLOAT_EQ(mel_task_progress(ctx, t), 0.0f, 0.001f);

    mel_task_tick(ctx);

    MEL_ASSERT_FLOAT_EQ(mel_task_progress(ctx, t), 1.0f, 0.001f);
    MEL_ASSERT(mel_task_is_done(ctx, t));

    mel_task_release(ctx, t);
    mel_task_ctx_destroy(ctx);
}

MEL_TEST(task_step_failure_propagates, .tags = "async")
{
    Mel_Task_Ctx* ctx = make_ctx();
    reset_exec_tracker();

    Mel_Task_Handle t = mel_task_begin(ctx);
    u32 s0 = mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_fail });
    u32 s1 = mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)99 });
    mel_task_add_dep(ctx, t, s1, s0);
    mel_task_submit(ctx, t);

    mel_task_tick(ctx);

    MEL_ASSERT_EQ(mel_task_status(ctx, t), (u32)MEL_TASK_STATUS_FAILED);
    MEL_ASSERT_EQ(g_exec_count, 0u);

    mel_task_release(ctx, t);
    mel_task_ctx_destroy(ctx);
}

MEL_TEST(task_cancel, .tags = "async")
{
    Mel_Task_Ctx* ctx = make_ctx();
    reset_exec_tracker();

    Mel_Task_Handle t = mel_task_begin(ctx);
    u32 s0 = mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)1 });
    u32 s1 = mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)2 });
    mel_task_add_dep(ctx, t, s1, s0);
    mel_task_submit(ctx, t);

    mel_task_cancel(ctx, t);

    MEL_ASSERT_EQ(mel_task_status(ctx, t), (u32)MEL_TASK_STATUS_CANCELLED);

    mel_task_release(ctx, t);
    mel_task_ctx_destroy(ctx);
}

MEL_TEST(task_on_complete_callback, .tags = "async")
{
    Mel_Task_Ctx* ctx = make_ctx();
    reset_exec_tracker();
    Complete_Ctx cc = {0};

    Mel_Task_Handle t = mel_task_begin(ctx, .on_complete = on_complete_tracker, .on_complete_user = &cc);
    mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)1 });
    mel_task_submit(ctx, t);

    mel_task_tick(ctx);

    MEL_ASSERT_EQ(cc.called, 1u);
    MEL_ASSERT_EQ(cc.status, (u32)MEL_TASK_STATUS_DONE);

    mel_task_release(ctx, t);
    mel_task_ctx_destroy(ctx);
}

MEL_TEST(task_on_complete_on_failure, .tags = "async")
{
    Mel_Task_Ctx* ctx = make_ctx();
    Complete_Ctx cc = {0};

    Mel_Task_Handle t = mel_task_begin(ctx, .on_complete = on_complete_tracker, .on_complete_user = &cc);
    mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_fail });
    mel_task_submit(ctx, t);

    mel_task_tick(ctx);

    MEL_ASSERT_EQ(cc.called, 1u);
    MEL_ASSERT_EQ(cc.status, (u32)MEL_TASK_STATUS_FAILED);

    mel_task_release(ctx, t);
    mel_task_ctx_destroy(ctx);
}

MEL_TEST(task_multiple_tasks, .tags = "async")
{
    Mel_Task_Ctx* ctx = make_ctx();
    reset_exec_tracker();

    Mel_Task_Handle t1 = mel_task_begin(ctx);
    mel_task_add_step(ctx, t1, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)1 });
    mel_task_submit(ctx, t1);

    Mel_Task_Handle t2 = mel_task_begin(ctx);
    mel_task_add_step(ctx, t2, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)2 });
    mel_task_submit(ctx, t2);

    mel_task_tick(ctx);

    MEL_ASSERT(mel_task_is_done(ctx, t1));
    MEL_ASSERT(mel_task_is_done(ctx, t2));
    MEL_ASSERT_EQ(g_exec_count, 2u);

    mel_task_release(ctx, t1);
    mel_task_release(ctx, t2);
    mel_task_ctx_destroy(ctx);
}

typedef struct {
    i32 accumulator;
} Shared_Data;

static Mel_Task_Step_Result step_add_10(Mel_Task_Ctx* ctx, void* user_data)
{
    (void)ctx;
    Shared_Data* d = user_data;
    d->accumulator += 10;
    return (Mel_Task_Step_Result){ .result = MEL_TASK_STEP_DONE };
}

static Mel_Task_Step_Result step_multiply_2(Mel_Task_Ctx* ctx, void* user_data)
{
    (void)ctx;
    Shared_Data* d = user_data;
    d->accumulator *= 2;
    return (Mel_Task_Step_Result){ .result = MEL_TASK_STEP_DONE };
}

MEL_TEST(task_shared_user_data, .tags = "async")
{
    Mel_Task_Ctx* ctx = make_ctx();
    Shared_Data data = { .accumulator = 5 };

    Mel_Task_Handle t = mel_task_begin(ctx);
    u32 s0 = mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_add_10, .user_data = &data });
    u32 s1 = mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_multiply_2, .user_data = &data });
    mel_task_add_dep(ctx, t, s1, s0);
    mel_task_submit(ctx, t);

    mel_task_tick(ctx);
    mel_task_tick(ctx);

    MEL_ASSERT(mel_task_is_done(ctx, t));
    MEL_ASSERT_EQ(data.accumulator, 30);

    mel_task_release(ctx, t);
    mel_task_ctx_destroy(ctx);
}

static void job_add_10(i32 range_start, i32 range_end, i32 thread_index, void* user)
{
    MEL_UNUSED(range_start);
    MEL_UNUSED(range_end);
    MEL_UNUSED(thread_index);

    Shared_Data* d = user;
    d->accumulator += 10;
}

static Mel_Task_Step_Result step_dispatch_job(Mel_Task_Ctx* ctx, void* user_data)
{
    return mel_task_step_dispatch_job(ctx, .callback = job_add_10, .user = user_data);
}

MEL_TEST(task_job_step_helper, .tags = "async")
{
    Task_Job_Ctx ctx = make_ctx_with_jobs();
    Shared_Data data = { .accumulator = 5 };

    Mel_Task_Handle t = mel_task_begin(ctx.task_ctx);
    u32 s0 = mel_task_add_step(ctx.task_ctx, t, (Mel_Task_Step_Desc){ .fn = step_dispatch_job, .user_data = &data });
    u32 s1 = mel_task_add_step(ctx.task_ctx, t, (Mel_Task_Step_Desc){ .fn = step_multiply_2, .user_data = &data });
    mel_task_add_dep(ctx.task_ctx, t, s1, s0);
    mel_task_submit(ctx.task_ctx, t);

    mel_task_tick(ctx.task_ctx);

    MEL_ASSERT_EQ(mel_task_status(ctx.task_ctx, t), (u32)MEL_TASK_STATUS_RUNNING);
    MEL_ASSERT_FLOAT_EQ(mel_task_progress(ctx.task_ctx, t), 0.0f, 0.001f);

    mel_task_wait(ctx.task_ctx, t);

    MEL_ASSERT(mel_task_is_done(ctx.task_ctx, t));
    MEL_ASSERT_EQ(data.accumulator, 30);

    mel_task_release(ctx.task_ctx, t);
    mel_task_ctx_destroy(ctx.task_ctx);
    mel_job_destroy_context(ctx.job_ctx, mel_alloc_heap());
}

MEL_TEST(task_diamond_deps, .tags = "async")
{
    Mel_Task_Ctx* ctx = make_ctx();
    reset_exec_tracker();

    Mel_Task_Handle t = mel_task_begin(ctx);
    u32 s0 = mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)1 });
    u32 s1 = mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)2 });
    u32 s2 = mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)3 });
    u32 s3 = mel_task_add_step(ctx, t, (Mel_Task_Step_Desc){ .fn = step_track_order, .user_data = (void*)(usize)4 });

    mel_task_add_dep(ctx, t, s1, s0);
    mel_task_add_dep(ctx, t, s2, s0);
    mel_task_add_dep(ctx, t, s3, s1);
    mel_task_add_dep(ctx, t, s3, s2);
    mel_task_submit(ctx, t);

    mel_task_tick(ctx);

    MEL_ASSERT(mel_task_is_done(ctx, t));
    MEL_ASSERT_EQ(g_exec_count, 4u);
    MEL_ASSERT_EQ(g_exec_order[0], 1u);
    MEL_ASSERT_EQ(g_exec_order[1], 2u);
    MEL_ASSERT_EQ(g_exec_order[2], 3u);
    MEL_ASSERT_EQ(g_exec_order[3], 4u);

    mel_task_release(ctx, t);
    mel_task_ctx_destroy(ctx);
}
