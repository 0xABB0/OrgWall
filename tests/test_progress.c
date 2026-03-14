#include "../melody/test.harness.h"
#include "../melody/allocator.heap.h"
// ASYNC_V2: removed, needs migration
// #include "../melody/async.task.h"
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

// ASYNC_V2: removed, needs migration

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

// ASYNC_V2: removed, needs migration — task+progress compose test disabled

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
