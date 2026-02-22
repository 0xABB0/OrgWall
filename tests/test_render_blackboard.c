#include "../melody/test.harness.h"
#include "../melody/render.blackboard.h"
#include "../melody/string.str8.h"
#include "../melody/allocator.heap.h"
#include <string.h>

MEL_TEST(blackboard_set_get, .tags = "render")
{
    Mel_Render_Blackboard bb;
    mel_render_blackboard_init(&bb, .alloc = mel_alloc_heap());

    u32 color = 0xFF00FF00;
    mel_render_blackboard_set(&bb, S8("color"), &color, sizeof(color));

    u32* result = (u32*)mel_render_blackboard_get(&bb, S8("color"));
    MEL_ASSERT_NOT_NULL(result);
    MEL_ASSERT_EQ(*result, 0xFF00FF00u);

    mel_render_blackboard_shutdown(&bb);
}

MEL_TEST(blackboard_overwrite, .tags = "render")
{
    Mel_Render_Blackboard bb;
    mel_render_blackboard_init(&bb, .alloc = mel_alloc_heap());

    u32 v1 = 42;
    mel_render_blackboard_set(&bb, S8("x"), &v1, sizeof(v1));
    u32 v2 = 99;
    mel_render_blackboard_set(&bb, S8("x"), &v2, sizeof(v2));

    u32* result = (u32*)mel_render_blackboard_get(&bb, S8("x"));
    MEL_ASSERT_EQ(*result, 99u);

    mel_render_blackboard_shutdown(&bb);
}

MEL_TEST(blackboard_has_true, .tags = "render")
{
    Mel_Render_Blackboard bb;
    mel_render_blackboard_init(&bb, .alloc = mel_alloc_heap());

    u32 v = 1;
    mel_render_blackboard_set(&bb, S8("a"), &v, sizeof(v));
    MEL_ASSERT(mel_render_blackboard_has(&bb, S8("a")));

    mel_render_blackboard_shutdown(&bb);
}

MEL_TEST(blackboard_has_false, .tags = "render")
{
    Mel_Render_Blackboard bb;
    mel_render_blackboard_init(&bb, .alloc = mel_alloc_heap());

    MEL_ASSERT(!mel_render_blackboard_has(&bb, S8("nonexistent")));

    mel_render_blackboard_shutdown(&bb);
}

MEL_TEST(blackboard_clear, .tags = "render")
{
    Mel_Render_Blackboard bb;
    mel_render_blackboard_init(&bb, .alloc = mel_alloc_heap());

    u32 v = 1;
    mel_render_blackboard_set(&bb, S8("a"), &v, sizeof(v));
    mel_render_blackboard_set(&bb, S8("b"), &v, sizeof(v));
    mel_render_blackboard_set(&bb, S8("c"), &v, sizeof(v));

    mel_render_blackboard_clear(&bb);

    MEL_ASSERT(!mel_render_blackboard_has(&bb, S8("a")));
    MEL_ASSERT(!mel_render_blackboard_has(&bb, S8("b")));
    MEL_ASSERT(!mel_render_blackboard_has(&bb, S8("c")));

    mel_render_blackboard_shutdown(&bb);
}

MEL_TEST(blackboard_multiple_keys, .tags = "render")
{
    Mel_Render_Blackboard bb;
    mel_render_blackboard_init(&bb, .alloc = mel_alloc_heap());

    for (u32 i = 0; i < 10; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "key_%u", i);
        mel_render_blackboard_set(&bb, str8_from_cstr(name), &i, sizeof(i));
    }

    for (u32 i = 0; i < 10; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "key_%u", i);
        u32* result = (u32*)mel_render_blackboard_get(&bb, str8_from_cstr(name));
        MEL_ASSERT_EQ(*result, i);
    }

    mel_render_blackboard_shutdown(&bb);
}

MEL_TEST(blackboard_different_sizes, .tags = "render")
{
    Mel_Render_Blackboard bb;
    mel_render_blackboard_init(&bb, .alloc = mel_alloc_heap());

    u32 small = 42;
    mel_render_blackboard_set(&bb, S8("small"), &small, sizeof(small));

    u64 big = 0xDEADBEEFCAFEBABEULL;
    mel_render_blackboard_set(&bb, S8("big"), &big, sizeof(big));

    typedef struct { f32 x; f32 y; f32 z; } Vec3;
    Vec3 pos = { 1.0f, 2.0f, 3.0f };
    mel_render_blackboard_set(&bb, S8("pos"), &pos, sizeof(pos));

    MEL_ASSERT_EQ(*(u32*)mel_render_blackboard_get(&bb, S8("small")), 42u);
    MEL_ASSERT_EQ(*(u64*)mel_render_blackboard_get(&bb, S8("big")), 0xDEADBEEFCAFEBABEULL);

    Vec3* rpos = (Vec3*)mel_render_blackboard_get(&bb, S8("pos"));
    MEL_ASSERT_FLOAT_EQ(rpos->x, 1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(rpos->y, 2.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(rpos->z, 3.0f, 0.001f);

    mel_render_blackboard_shutdown(&bb);
}

MEL_TEST(blackboard_capacity_growth, .tags = "render")
{
    Mel_Render_Blackboard bb;
    mel_render_blackboard_init(&bb, .alloc = mel_alloc_heap(), .initial_capacity = 2);

    for (u32 i = 0; i < 50; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "item_%u", i);
        mel_render_blackboard_set(&bb, str8_from_cstr(name), &i, sizeof(i));
    }

    MEL_ASSERT_EQ(bb.count, 50u);
    MEL_ASSERT_GE(bb.capacity, 50u);

    for (u32 i = 0; i < 50; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "item_%u", i);
        u32* result = (u32*)mel_render_blackboard_get(&bb, str8_from_cstr(name));
        MEL_ASSERT_EQ(*result, i);
    }

    mel_render_blackboard_shutdown(&bb);
}

MEL_TEST(blackboard_overwrite_different_size, .tags = "render")
{
    Mel_Render_Blackboard bb;
    mel_render_blackboard_init(&bb, .alloc = mel_alloc_heap());

    u32 small = 42;
    mel_render_blackboard_set(&bb, S8("data"), &small, sizeof(small));

    u64 big = 0xCAFEBABEDEADBEEFULL;
    mel_render_blackboard_set(&bb, S8("data"), &big, sizeof(big));

    u64* result = (u64*)mel_render_blackboard_get(&bb, S8("data"));
    MEL_ASSERT_EQ(*result, 0xCAFEBABEDEADBEEFULL);

    mel_render_blackboard_shutdown(&bb);
}

MEL_TEST(blackboard_set_after_clear, .tags = "render")
{
    Mel_Render_Blackboard bb;
    mel_render_blackboard_init(&bb, .alloc = mel_alloc_heap());

    u32 v = 1;
    mel_render_blackboard_set(&bb, S8("x"), &v, sizeof(v));
    mel_render_blackboard_clear(&bb);

    u32 v2 = 2;
    mel_render_blackboard_set(&bb, S8("x"), &v2, sizeof(v2));
    MEL_ASSERT_EQ(*(u32*)mel_render_blackboard_get(&bb, S8("x")), 2u);

    mel_render_blackboard_shutdown(&bb);
}
