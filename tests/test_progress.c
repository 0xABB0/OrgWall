#include "../melody/test.harness.h"
#include "../melody/allocator.heap.h"
#include "../melody/progress.h"
#include "../melody/async.signal.h"

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

MEL_TEST(progress_child_source, .tags = "progress")
{
    Progress_Source_Ctx a = { .value = 0.5f };

    Mel_Progress child;
    mel_progress_init(&child, mel_alloc_heap());
    mel_progress_add_custom(&child, progress_source_state, &a, 1.0f);

    Mel_Progress parent;
    mel_progress_init(&parent, mel_alloc_heap());
    mel_progress_add_child(&parent, &child, 1.0f);

    MEL_ASSERT_FLOAT_EQ(mel_progress_value(&parent), 0.5f, 0.001f);

    a.value = 1.0f;
    MEL_ASSERT_FLOAT_EQ(mel_progress_value(&parent), 1.0f, 0.001f);
    MEL_ASSERT(mel_progress_is_ready(&parent, 1.0f));

    mel_progress_destroy(&parent);
    mel_progress_destroy(&child);
}

MEL_TEST(progress_counter_source, .tags = "progress")
{
    Mel_Counter counter = MEL_COUNTER_INIT;

    mel_counter_increment(&counter);
    mel_counter_increment(&counter);
    mel_counter_increment(&counter);
    mel_counter_increment(&counter);

    Mel_Progress progress;
    mel_progress_init(&progress, mel_alloc_heap());
    mel_progress_add_counter(&progress, &counter, 4, 1.0f);

    MEL_ASSERT_FLOAT_EQ(mel_progress_value(&progress), 0.0f, 0.001f);

    mel_counter_decrement(&counter);
    MEL_ASSERT_FLOAT_EQ(mel_progress_value(&progress), 0.25f, 0.001f);

    mel_counter_decrement(&counter);
    mel_counter_decrement(&counter);
    MEL_ASSERT_FLOAT_EQ(mel_progress_value(&progress), 0.75f, 0.001f);

    mel_counter_decrement(&counter);
    MEL_ASSERT_FLOAT_EQ(mel_progress_value(&progress), 1.0f, 0.001f);
    MEL_ASSERT(mel_progress_is_ready(&progress, 1.0f));

    mel_progress_destroy(&progress);
}

MEL_TEST(progress_counter_and_custom_mixed, .tags = "progress")
{
    Mel_Counter counter = MEL_COUNTER_INIT;
    mel_counter_increment(&counter);
    mel_counter_increment(&counter);

    Progress_Source_Ctx custom = { .value = 0.5f };

    Mel_Progress progress;
    mel_progress_init(&progress, mel_alloc_heap());
    mel_progress_add_counter(&progress, &counter, 2, 1.0f);
    mel_progress_add_custom(&progress, progress_source_state, &custom, 1.0f);

    MEL_ASSERT_FLOAT_EQ(mel_progress_value(&progress), 0.25f, 0.001f);

    mel_counter_decrement(&counter);
    mel_counter_decrement(&counter);
    custom.value = 1.0f;
    MEL_ASSERT_FLOAT_EQ(mel_progress_value(&progress), 1.0f, 0.001f);

    mel_progress_destroy(&progress);
}

MEL_TEST(progress_clear_resets, .tags = "progress")
{
    Progress_Source_Ctx a = { .value = 0.5f };

    Mel_Progress progress;
    mel_progress_init(&progress, mel_alloc_heap());
    mel_progress_add_custom(&progress, progress_source_state, &a, 1.0f);

    MEL_ASSERT_FLOAT_EQ(mel_progress_value(&progress), 0.5f, 0.001f);

    mel_progress_clear(&progress);
    MEL_ASSERT_FLOAT_EQ(mel_progress_value(&progress), 1.0f, 0.001f);

    mel_progress_destroy(&progress);
}
