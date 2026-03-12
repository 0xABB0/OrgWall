#include "../melody/test.harness.h"
#include "../melody/allocator.heap.h"
#include "../melody/async.task.h"
#include "../melody/progress.h"

typedef struct {
    f32  value;
    bool failed;
} Progress_Source_Ctx;

static Mel_Progress_Status progress_source_state(void* user)
{
    Progress_Source_Ctx* ctx = user;
    return (Mel_Progress_Status){
        .value = ctx->value,
        .failed = ctx->failed,
    };
}

static Mel_Task_Ctx* make_task_ctx(void)
{
    Mel_Task_Ctx_Desc desc = { .alloc = mel_alloc_heap() };
    return mel_task_ctx_create(&desc);
}

static Mel_Task_Step_Result step_done(Mel_Task_Ctx* ctx, void* user_data)
{
    MEL_UNUSED(ctx);
    MEL_UNUSED(user_data);
    return (Mel_Task_Step_Result){ .result = MEL_TASK_STEP_DONE };
}

MEL_TEST(progress_empty_is_ready, .tags = "progress")
{
    Mel_Progress progress;
    mel_progress_init(&progress, mel_alloc_heap());

    MEL_ASSERT_FLOAT_EQ(mel_progress_value(&progress), 1.0f, 0.001f);
    MEL_ASSERT(mel_progress_is_ready(&progress, 1.0f));
    MEL_ASSERT(!mel_progress_is_failed(&progress));

    mel_progress_destroy(&progress);
}

MEL_TEST(progress_weighted_custom_sources, .tags = "progress")
{
    Progress_Source_Ctx a = { .value = 0.25f };
    Progress_Source_Ctx b = { .value = 0.75f };

    Mel_Progress progress;
    mel_progress_init(&progress, mel_alloc_heap());
    mel_progress_add_custom(&progress, progress_source_state, &a, 1.0f);
    mel_progress_add_custom(&progress, progress_source_state, &b, 3.0f);

    MEL_ASSERT_FLOAT_EQ(mel_progress_value(&progress), 0.625f, 0.001f);
    MEL_ASSERT(mel_progress_is_ready(&progress, 0.6f));
    MEL_ASSERT(!mel_progress_is_ready(&progress, 0.7f));

    mel_progress_destroy(&progress);
}

MEL_TEST(progress_child_and_task_sources_compose, .tags = "progress")
{
    Mel_Task_Ctx* task_ctx = make_task_ctx();
    Mel_Task_Handle task = mel_task_begin(task_ctx);
    mel_task_add_step(task_ctx, task, (Mel_Task_Step_Desc){ .fn = step_done });
    mel_task_submit(task_ctx, task);

    Progress_Source_Ctx leaf = { .value = 0.5f };

    Mel_Progress child;
    mel_progress_init(&child, mel_alloc_heap());
    mel_progress_add_custom(&child, progress_source_state, &leaf, 1.0f);

    Mel_Progress parent;
    mel_progress_init(&parent, mel_alloc_heap());
    mel_progress_add_child(&parent, &child, 1.0f);
    mel_progress_add_task(&parent, task_ctx, task, 2.0f);

    MEL_ASSERT_FLOAT_EQ(mel_progress_value(&parent), 0.166f, 0.01f);
    MEL_ASSERT(!mel_progress_is_ready(&parent, 0.5f));

    mel_task_tick(task_ctx);

    MEL_ASSERT_FLOAT_EQ(mel_progress_value(&parent), 0.833f, 0.01f);
    MEL_ASSERT(mel_progress_is_ready(&parent, 0.8f));

    mel_progress_destroy(&parent);
    mel_progress_destroy(&child);
    mel_task_release(task_ctx, task);
    mel_task_ctx_destroy(task_ctx);
}

MEL_TEST(progress_failure_blocks_ready, .tags = "progress")
{
    Progress_Source_Ctx ok = { .value = 1.0f };
    Progress_Source_Ctx bad = { .value = 1.0f, .failed = true };

    Mel_Progress progress;
    mel_progress_init(&progress, mel_alloc_heap());
    mel_progress_add_custom(&progress, progress_source_state, &ok, 1.0f);
    mel_progress_add_custom(&progress, progress_source_state, &bad, 1.0f);

    MEL_ASSERT_FLOAT_EQ(mel_progress_value(&progress), 1.0f, 0.001f);
    MEL_ASSERT(mel_progress_is_failed(&progress));
    MEL_ASSERT(!mel_progress_is_ready(&progress, 1.0f));

    mel_progress_destroy(&progress);
}
