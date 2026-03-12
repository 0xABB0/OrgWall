#include "../melody/test.harness.h"
#include "../melody/progress.h"
#include "../melody/stage.h"
#include "../melody/allocator.heap.h"
#include "../melody/string.str8.h"

typedef struct {
    u32 started;
    u32 ended;
    u32 ticked;
} Stage_Counters;

typedef struct {
    f32  value;
    bool failed;
} Progress_Ctx;

static void stage_on_start(Mel_Stage* stage, void* user)
{
    MEL_UNUSED(stage);
    Stage_Counters* counters = user;
    counters->started++;
}

static void stage_on_end(Mel_Stage* stage, void* user)
{
    MEL_UNUSED(stage);
    Stage_Counters* counters = user;
    counters->ended++;
}

static void stage_on_tick(Mel_Stage* stage, void* user)
{
    MEL_UNUSED(stage);
    Stage_Counters* counters = user;
    counters->ticked++;
}

static Mel_Progress_Status progress_source(void* user)
{
    Progress_Ctx* ctx = user;
    return (Mel_Progress_Status){
        .value = ctx->value,
        .failed = ctx->failed,
    };
}

MEL_TEST(stage_attach_and_detach_are_deferred, .tags = "stage")
{
    Stage_Counters counters = {0};
    Mel_Stage stage;
    mel_stage_init(&stage,
        .on_start = stage_on_start,
        .on_end = stage_on_end,
        .user = &counters,
        .start_enabled = true);

    mel_stage_attach(&stage);

    MEL_ASSERT(!mel_stage_is_attached(&stage));
    MEL_ASSERT_EQ(counters.started, 0u);

    mel_stage_tick();

    MEL_ASSERT(mel_stage_is_attached(&stage));
    MEL_ASSERT(mel_stage_is_enabled(&stage));
    MEL_ASSERT_EQ(counters.started, 1u);

    mel_stage_detach(&stage);
    MEL_ASSERT_EQ(counters.ended, 0u);

    mel_stage_tick();

    MEL_ASSERT(!mel_stage_is_attached(&stage));
    MEL_ASSERT(!mel_stage_is_enabled(&stage));
    MEL_ASSERT_EQ(counters.ended, 1u);

    mel_stage_shutdown(&stage);
}

MEL_TEST(stage_can_attach_disabled_then_enable, .tags = "stage")
{
    Stage_Counters counters = {0};
    Mel_Stage stage;
    mel_stage_init(&stage,
        .on_start = stage_on_start,
        .on_end = stage_on_end,
        .user = &counters,
        .start_enabled = false);

    mel_stage_attach(&stage);
    mel_stage_tick();

    MEL_ASSERT(mel_stage_is_attached(&stage));
    MEL_ASSERT(!mel_stage_is_enabled(&stage));
    MEL_ASSERT_EQ(counters.started, 0u);

    mel_stage_enable(&stage);
    mel_stage_tick();

    MEL_ASSERT(mel_stage_is_enabled(&stage));
    MEL_ASSERT_EQ(counters.started, 1u);

    mel_stage_disable(&stage);
    mel_stage_tick();

    MEL_ASSERT(!mel_stage_is_enabled(&stage));
    MEL_ASSERT_EQ(counters.ended, 1u);

    mel_stage_detach(&stage);
    mel_stage_tick();
    mel_stage_shutdown(&stage);
}

MEL_TEST(stage_tick_runs_only_when_enabled, .tags = "stage")
{
    Stage_Counters counters = {0};
    Mel_Stage stage;
    mel_stage_init(&stage,
        .on_start = stage_on_start,
        .on_end = stage_on_end,
        .on_tick = stage_on_tick,
        .user = &counters,
        .start_enabled = true);

    mel_stage_attach(&stage);
    mel_stage_tick();
    mel_stage_tick();

    MEL_ASSERT_EQ(counters.ticked, 2u);

    mel_stage_disable(&stage);
    mel_stage_tick();
    mel_stage_tick();

    MEL_ASSERT_EQ(counters.ticked, 2u);
    MEL_ASSERT_EQ(counters.ended, 1u);

    mel_stage_detach(&stage);
    mel_stage_tick();
    mel_stage_shutdown(&stage);
}

MEL_TEST(loading_stage_hands_off_when_progress_ready, .tags = "stage")
{
    Progress_Ctx progress_ctx = { .value = 0.5f };
    Mel_Progress progress;
    mel_progress_init(&progress, mel_alloc_heap());
    mel_progress_add_custom(&progress, progress_source, &progress_ctx, 1.0f);

    Stage_Counters next_counters = {0};
    Mel_Stage next;
    mel_stage_init(&next,
        .on_start = stage_on_start,
        .on_end = stage_on_end,
        .user = &next_counters,
        .start_enabled = false);

    Mel_Loading_Stage loading;
    mel_loading_stage_init(&loading,
        .progress = &progress,
        .next = &next,
        .ready_at = 0.8f,
        .attach_next = true,
        .enable_next = true,
        .detach_self = true);

    mel_stage_attach(&loading.stage);
    mel_stage_tick();

    MEL_ASSERT(mel_stage_is_attached(&loading.stage));
    MEL_ASSERT(mel_stage_is_enabled(&loading.stage));
    MEL_ASSERT(!mel_stage_is_attached(&next));

    progress_ctx.value = 0.9f;
    mel_stage_tick();

    MEL_ASSERT(!mel_stage_is_attached(&loading.stage));
    MEL_ASSERT(mel_stage_is_attached(&next));
    MEL_ASSERT(mel_stage_is_enabled(&next));
    MEL_ASSERT_EQ(next_counters.started, 1u);

    mel_stage_detach(&next);
    mel_stage_tick();

    mel_loading_stage_shutdown(&loading);
    mel_stage_shutdown(&next);
    mel_progress_destroy(&progress);
}

MEL_TEST(stage_registry_finds_and_enables_named_stages, .tags = "stage")
{
    Mel_Stage_Registry registry;
    mel_stage_registry_init(&registry, mel_alloc_heap());

    Stage_Counters counters = {0};
    Mel_Stage stage;
    mel_stage_init(&stage,
        .on_start = stage_on_start,
        .on_end = stage_on_end,
        .user = &counters,
        .start_enabled = false);

    MEL_ASSERT(mel_stage_registry_add(&registry,
        .name = S8("title"),
        .stage = &stage,
        .tags = 1u));
    MEL_ASSERT(!mel_stage_registry_add(&registry,
        .name = S8("title"),
        .stage = &stage,
        .tags = 1u));
    MEL_ASSERT_EQ(mel_stage_registry_find(&registry, S8("title")), &stage);

    MEL_ASSERT(mel_stage_registry_enable_named(&registry, S8("title")));
    mel_stage_tick();

    MEL_ASSERT(mel_stage_is_attached(&stage));
    MEL_ASSERT(mel_stage_is_enabled(&stage));
    MEL_ASSERT_EQ(counters.started, 1u);

    mel_stage_registry_detach_tagged(&registry, 1u);
    mel_stage_tick();

    mel_stage_shutdown(&stage);
    mel_stage_registry_shutdown(&registry);
}

MEL_TEST(stage_registry_enable_exclusive_disables_peer_stages, .tags = "stage")
{
    Mel_Stage_Registry registry;
    mel_stage_registry_init(&registry, mel_alloc_heap());

    Stage_Counters first_counters = {0};
    Stage_Counters second_counters = {0};

    Mel_Stage first;
    Mel_Stage second;
    mel_stage_init(&first,
        .on_start = stage_on_start,
        .on_end = stage_on_end,
        .user = &first_counters,
        .start_enabled = false);
    mel_stage_init(&second,
        .on_start = stage_on_start,
        .on_end = stage_on_end,
        .user = &second_counters,
        .start_enabled = false);

    MEL_ASSERT(mel_stage_registry_add(&registry, .name = S8("first"), .stage = &first, .tags = 1u));
    MEL_ASSERT(mel_stage_registry_add(&registry, .name = S8("second"), .stage = &second, .tags = 1u));

    MEL_ASSERT(mel_stage_registry_enable_exclusive(&registry, S8("first"), 1u));
    mel_stage_tick();

    MEL_ASSERT(mel_stage_is_enabled(&first));
    MEL_ASSERT(!mel_stage_is_enabled(&second));

    MEL_ASSERT(mel_stage_registry_enable_exclusive(&registry, S8("second"), 1u));
    mel_stage_tick();

    MEL_ASSERT(!mel_stage_is_enabled(&first));
    MEL_ASSERT(mel_stage_is_enabled(&second));
    MEL_ASSERT_EQ(first_counters.ended, 1u);
    MEL_ASSERT_EQ(second_counters.started, 1u);

    mel_stage_registry_detach_tagged(&registry, 1u);
    mel_stage_tick();

    mel_stage_shutdown(&first);
    mel_stage_shutdown(&second);
    mel_stage_registry_shutdown(&registry);
}
