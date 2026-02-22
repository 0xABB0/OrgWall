#include "../melody/test.harness.h"
#include "../melody/async.coro.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"

MEL_TEST(coro_create_destroy, .tags = "async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Coro_Context* ctx = mel_coro_create(alloc, .num_initial = 4);
    MEL_ASSERT_NOT_NULL(ctx);
    mel_coro_destroy(ctx);
}

static volatile i32 g_invoke_ran = 0;

mel_coro_declare(invoke_end)
{
    Mel_Coro_Context* ctx = mel_coro_userdata();
    g_invoke_ran = 1;
    mel_coro_end(ctx);
}

MEL_TEST(coro_invoke_end, .tags = "async")
{
    g_invoke_ran = 0;
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Coro_Context* ctx = mel_coro_create(alloc, .num_initial = 4);

    mel_coro_invoke(ctx, invoke_end, ctx);
    MEL_ASSERT_EQ(g_invoke_ran, 1);

    mel_coro_destroy(ctx);
}

static volatile i32 g_yield_step = 0;

mel_coro_declare(yield_once)
{
    Mel_Coro_Context* ctx = mel_coro_userdata();
    g_yield_step = 1;
    mel_coro_yield(ctx);
    g_yield_step = 2;
    mel_coro_end(ctx);
}

MEL_TEST(coro_yield, .tags = "async")
{
    g_yield_step = 0;
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Coro_Context* ctx = mel_coro_create(alloc, .num_initial = 4);

    mel_coro_invoke(ctx, yield_once, ctx);
    MEL_ASSERT_EQ(g_yield_step, 1);

    mel_coro_update(ctx, 0.016f);
    MEL_ASSERT_EQ(g_yield_step, 2);

    mel_coro_destroy(ctx);
}

static volatile i32 g_yieldn_step = 0;

mel_coro_declare(yield_three)
{
    Mel_Coro_Context* ctx = mel_coro_userdata();
    g_yieldn_step = 1;
    mel_coro_yieldn(ctx, 3);
    g_yieldn_step = 2;
    mel_coro_end(ctx);
}

MEL_TEST(coro_yieldn, .tags = "async")
{
    g_yieldn_step = 0;
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Coro_Context* ctx = mel_coro_create(alloc, .num_initial = 4);

    mel_coro_invoke(ctx, yield_three, ctx);
    MEL_ASSERT_EQ(g_yieldn_step, 1);

    mel_coro_update(ctx, 0.016f);
    MEL_ASSERT_EQ(g_yieldn_step, 1);

    mel_coro_update(ctx, 0.016f);
    MEL_ASSERT_EQ(g_yieldn_step, 1);

    mel_coro_update(ctx, 0.016f);
    MEL_ASSERT_EQ(g_yieldn_step, 2);

    mel_coro_destroy(ctx);
}

static volatile i32 g_wait_step = 0;

mel_coro_declare(wait_100ms)
{
    Mel_Coro_Context* ctx = mel_coro_userdata();
    g_wait_step = 1;
    mel_coro_wait(ctx, 100);
    g_wait_step = 2;
    mel_coro_end(ctx);
}

MEL_TEST(coro_wait, .tags = "async")
{
    g_wait_step = 0;
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Coro_Context* ctx = mel_coro_create(alloc, .num_initial = 4);

    mel_coro_invoke(ctx, wait_100ms, ctx);
    MEL_ASSERT_EQ(g_wait_step, 1);

    mel_coro_update(ctx, 0.05f);
    MEL_ASSERT_EQ(g_wait_step, 1);

    mel_coro_update(ctx, 0.05f);
    MEL_ASSERT_EQ(g_wait_step, 2);

    mel_coro_destroy(ctx);
}

static volatile i32 g_multi_a = 0;
static volatile i32 g_multi_b = 0;

mel_coro_declare(multi_a)
{
    Mel_Coro_Context* ctx = mel_coro_userdata();
    g_multi_a = 1;
    mel_coro_yield(ctx);
    g_multi_a = 2;
    mel_coro_end(ctx);
}

mel_coro_declare(multi_b)
{
    Mel_Coro_Context* ctx = mel_coro_userdata();
    g_multi_b = 1;
    mel_coro_yield(ctx);
    g_multi_b = 2;
    mel_coro_end(ctx);
}

MEL_TEST(coro_multiple, .tags = "async")
{
    g_multi_a = 0;
    g_multi_b = 0;
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Coro_Context* ctx = mel_coro_create(alloc, .num_initial = 4);

    mel_coro_invoke(ctx, multi_a, ctx);
    mel_coro_invoke(ctx, multi_b, ctx);
    MEL_ASSERT_EQ(g_multi_a, 1);
    MEL_ASSERT_EQ(g_multi_b, 1);

    mel_coro_update(ctx, 0.016f);
    MEL_ASSERT_EQ(g_multi_a, 2);
    MEL_ASSERT_EQ(g_multi_b, 2);

    mel_coro_destroy(ctx);
}

typedef struct { i32 value; } Coro_Test_Data;

static volatile i32 g_userdata_val = 0;

mel_coro_declare(userdata_check)
{
    Mel_Coro_Context* ctx = ((void**)mel_coro_userdata())[0];
    Coro_Test_Data* data = ((void**)mel_coro_userdata())[1];
    g_userdata_val = data->value;
    mel_coro_end(ctx);
}

MEL_TEST(coro_userdata, .tags = "async")
{
    g_userdata_val = 0;
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Coro_Context* ctx = mel_coro_create(alloc, .num_initial = 4);

    Coro_Test_Data data = { .value = 777 };
    void* args[2] = { ctx, &data };

    mel_coro_invoke(ctx, userdata_check, args);
    MEL_ASSERT_EQ(g_userdata_val, 777);

    mel_coro_destroy(ctx);
}