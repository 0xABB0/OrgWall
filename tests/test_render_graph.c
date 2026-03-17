#include "../melody/test.harness.h"
#include "../melody/render.graph.h"
#include "../melody/gpu.types.h"
#include "../melody/render.target.h"
#include "../melody/render.list.h"
#include "../melody/string.str8.h"
#include "../melody/allocator.heap.h"

static u32 s_execution_order[64];
static u32 s_execution_count;

static void record_execute(Mel_Render_Pass_Ctx* ctx)
{
    u32 id = (u32)(uintptr_t)ctx->user;
    s_execution_order[s_execution_count++] = id;
}

MEL_TEST(graph_empty, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g, .alloc = mel_alloc_heap());
    MEL_ASSERT(mel_render_graph_compile(&g));
    MEL_ASSERT_EQ((u32)g.sorted_order.count, 0u);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_single_pass, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g, .alloc = mel_alloc_heap());
    u32 p = mel_render_graph_add_pass(&g, S8("only"));
    MEL_ASSERT(mel_render_graph_compile(&g));
    MEL_ASSERT_EQ((u32)g.sorted_order.count, 1u);
    MEL_ASSERT_EQ(g.sorted_order.items[0], p);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_linear_chain, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g, .alloc = mel_alloc_heap());

    Mel_Render_Target t1 = {0};
    Mel_Render_Target t2 = {0};

    u32 a = mel_render_graph_add_pass(&g, S8("A"), .write_targets = MEL_WRITE_TARGETS({ .target = &t1 }));
    u32 b = mel_render_graph_add_pass(&g, S8("B"),
        .read_targets = MEL_TARGETS(&t1),
        .write_targets = MEL_WRITE_TARGETS({ .target = &t2 }));
    u32 c = mel_render_graph_add_pass(&g, S8("C"), .read_targets = MEL_TARGETS(&t2));

    MEL_ASSERT(mel_render_graph_compile(&g));
    MEL_ASSERT_EQ((u32)g.sorted_order.count, 3u);
    MEL_ASSERT_EQ(g.sorted_order.items[0], a);
    MEL_ASSERT_EQ(g.sorted_order.items[1], b);
    MEL_ASSERT_EQ(g.sorted_order.items[2], c);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_diamond, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g, .alloc = mel_alloc_heap());

    Mel_Render_Target t1 = {0};
    Mel_Render_Target t2 = {0};
    Mel_Render_Target t3 = {0};

    u32 a = mel_render_graph_add_pass(&g, S8("A"), .write_targets = MEL_WRITE_TARGETS({ .target = &t1 }));
    mel_render_graph_add_pass(&g, S8("B"),
        .read_targets = MEL_TARGETS(&t1),
        .write_targets = MEL_WRITE_TARGETS({ .target = &t2 }));
    mel_render_graph_add_pass(&g, S8("C"),
        .read_targets = MEL_TARGETS(&t1),
        .write_targets = MEL_WRITE_TARGETS({ .target = &t3 }));
    u32 d = mel_render_graph_add_pass(&g, S8("D"), .read_targets = MEL_TARGETS(&t2, &t3));

    MEL_ASSERT(mel_render_graph_compile(&g));
    MEL_ASSERT_EQ((u32)g.sorted_order.count, 4u);
    MEL_ASSERT_EQ(g.sorted_order.items[0], a);
    MEL_ASSERT_EQ(g.sorted_order.items[3], d);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_independent_passes, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g, .alloc = mel_alloc_heap());
    mel_render_graph_add_pass(&g, S8("A"));
    mel_render_graph_add_pass(&g, S8("B"));
    MEL_ASSERT(mel_render_graph_compile(&g));
    MEL_ASSERT_EQ((u32)g.sorted_order.count, 2u);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_write_read_dep, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g, .alloc = mel_alloc_heap());

    Mel_Render_Target color = {0};

    u32 writer = mel_render_graph_add_pass(&g, S8("writer"), .write_targets = MEL_WRITE_TARGETS({ .target = &color }));
    u32 reader = mel_render_graph_add_pass(&g, S8("reader"), .read_targets = MEL_TARGETS(&color));

    MEL_ASSERT(mel_render_graph_compile(&g));
    MEL_ASSERT_EQ((u32)g.sorted_order.count, 2u);
    MEL_ASSERT_EQ(g.sorted_order.items[0], writer);
    MEL_ASSERT_EQ(g.sorted_order.items[1], reader);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_cycle_fails, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g, .alloc = mel_alloc_heap());

    Mel_Render_Target t1 = {0};
    Mel_Render_Target t2 = {0};

    mel_render_graph_add_pass(&g, S8("A"),
        .read_targets = MEL_TARGETS(&t2),
        .write_targets = MEL_WRITE_TARGETS({ .target = &t1 }));
    mel_render_graph_add_pass(&g, S8("B"),
        .read_targets = MEL_TARGETS(&t1),
        .write_targets = MEL_WRITE_TARGETS({ .target = &t2 }));

    MEL_ASSERT(!mel_render_graph_compile(&g));
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_cycle_failure_leaves_graph_dirty, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g, .alloc = mel_alloc_heap());

    Mel_Render_Target t1 = {0};
    Mel_Render_Target t2 = {0};

    mel_render_graph_add_pass(&g, S8("A"),
        .read_targets = MEL_TARGETS(&t2),
        .write_targets = MEL_WRITE_TARGETS({ .target = &t1 }));
    mel_render_graph_add_pass(&g, S8("B"),
        .read_targets = MEL_TARGETS(&t1),
        .write_targets = MEL_WRITE_TARGETS({ .target = &t2 }));

    MEL_ASSERT(!mel_render_graph_compile(&g));
    MEL_ASSERT(g.dirty);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_explicit_dep, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g, .alloc = mel_alloc_heap());

    u32 a = mel_render_graph_add_pass(&g, S8("A"));
    u32 b = mel_render_graph_add_pass(&g, S8("B"));
    u32 c = mel_render_graph_add_pass(&g, S8("C"));

    mel_render_graph_pass_depends_on(&g, b, a);
    mel_render_graph_pass_depends_on(&g, c, b);

    MEL_ASSERT(mel_render_graph_compile(&g));
    MEL_ASSERT_EQ((u32)g.sorted_order.count, 3u);
    MEL_ASSERT_EQ(g.sorted_order.items[0], a);
    MEL_ASSERT_EQ(g.sorted_order.items[1], b);
    MEL_ASSERT_EQ(g.sorted_order.items[2], c);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_multiple_writers, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g, .alloc = mel_alloc_heap());

    Mel_Render_Target target = {0};

    u32 w1 = mel_render_graph_add_pass(&g, S8("w1"), .write_targets = MEL_WRITE_TARGETS({ .target = &target }));
    u32 w2 = mel_render_graph_add_pass(&g, S8("w2"), .write_targets = MEL_WRITE_TARGETS({ .target = &target }));

    MEL_ASSERT(mel_render_graph_compile(&g));
    MEL_ASSERT_EQ((u32)g.sorted_order.count, 2u);
    MEL_ASSERT_EQ(g.sorted_order.items[0], w1);
    MEL_ASSERT_EQ(g.sorted_order.items[1], w2);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_isolated_subgraphs, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g, .alloc = mel_alloc_heap());

    u32 a = mel_render_graph_add_pass(&g, S8("A"));
    u32 b = mel_render_graph_add_pass(&g, S8("B"));
    u32 c = mel_render_graph_add_pass(&g, S8("C"));
    u32 d = mel_render_graph_add_pass(&g, S8("D"));

    mel_render_graph_pass_depends_on(&g, b, a);
    mel_render_graph_pass_depends_on(&g, d, c);

    MEL_ASSERT(mel_render_graph_compile(&g));
    MEL_ASSERT_EQ((u32)g.sorted_order.count, 4u);

    u32 pos_a = 0, pos_b = 0, pos_c = 0, pos_d = 0;
    for (u32 i = 0; i < (u32)g.sorted_order.count; i++)
    {
        if (g.sorted_order.items[i] == a) pos_a = i;
        if (g.sorted_order.items[i] == b) pos_b = i;
        if (g.sorted_order.items[i] == c) pos_c = i;
        if (g.sorted_order.items[i] == d) pos_d = i;
    }
    MEL_ASSERT(pos_a < pos_b);
    MEL_ASSERT(pos_c < pos_d);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_execution_order, .tags = "render")
{
    s_execution_count = 0;

    Mel_Render_Graph g;
    mel_render_graph_init(&g, .alloc = mel_alloc_heap());
    u32 a = mel_render_graph_add_pass(&g, S8("A"),
        .fn = record_execute, .user = (void*)(uintptr_t)0);
    u32 b = mel_render_graph_add_pass(&g, S8("B"),
        .fn = record_execute, .user = (void*)(uintptr_t)1);
    u32 c = mel_render_graph_add_pass(&g, S8("C"),
        .fn = record_execute, .user = (void*)(uintptr_t)2);

    mel_render_graph_pass_depends_on(&g, b, a);
    mel_render_graph_pass_depends_on(&g, c, b);
    MEL_ASSERT(mel_render_graph_compile(&g));

    mel_render_graph_execute(&g);

    MEL_ASSERT_EQ(s_execution_count, 3u);
    MEL_ASSERT_EQ(s_execution_order[0], 0u);
    MEL_ASSERT_EQ(s_execution_order[1], 1u);
    MEL_ASSERT_EQ(s_execution_order[2], 2u);

    MEL_UNUSED(a);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_barrier_computation, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g, .alloc = mel_alloc_heap());

    Mel_Render_Target color = {.kind = MEL_RENDER_TARGET_COLOR};

    mel_render_graph_add_pass(&g, S8("writer"),
        .write_targets = MEL_WRITE_TARGETS({ .target = &color }));
    mel_render_graph_add_pass(&g, S8("reader"), .read_targets = MEL_TARGETS(&color));

    MEL_ASSERT(mel_render_graph_compile(&g));

    MEL_ASSERT_GE((u32)g.barriers.count, 1u);

    bool found_write_barrier = false;
    bool found_read_barrier = false;
    for (usize i = 0; i < g.barriers.count; i++)
    {
        Mel_Render_Graph_Barrier* b = &g.barriers.items[i];
        if (b->new_layout == MEL_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT)
            found_write_barrier = true;
        if (b->new_layout == MEL_GPU_IMAGE_LAYOUT_SHADER_READ_ONLY)
            found_read_barrier = true;
    }
    MEL_ASSERT(found_write_barrier);
    MEL_ASSERT(found_read_barrier);

    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_compute_target_read_uses_compute_stage, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g, .alloc = mel_alloc_heap());

    Mel_Render_Target color = {.kind = MEL_RENDER_TARGET_COLOR};

    mel_render_graph_add_pass(&g, S8("writer"),
        .type = MEL_PASS_GRAPHICS,
        .write_targets = MEL_WRITE_TARGETS({ .target = &color }));
    u32 compute_reader = mel_render_graph_add_pass(&g, S8("compute_reader"),
        .type = MEL_PASS_COMPUTE,
        .read_targets = MEL_TARGETS(&color));

    MEL_ASSERT(mel_render_graph_compile(&g));

    bool found_compute_read_barrier = false;
    for (usize i = 0; i < g.barriers.count; i++)
    {
        Mel_Render_Graph_Barrier* b = &g.barriers.items[i];
        if (b->pass_index == compute_reader && b->target == &color &&
            b->new_layout == MEL_GPU_IMAGE_LAYOUT_SHADER_READ_ONLY)
        {
            found_compute_read_barrier = true;
            MEL_ASSERT((b->dst_stage & MEL_GPU_STAGE_COMPUTE_SHADER) != 0);
        }
    }

    MEL_ASSERT(found_compute_read_barrier);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_list_write_read_generates_barrier, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g, .alloc = mel_alloc_heap());

    Mel_Render_List list = {0};

    mel_render_graph_add_pass(&g, S8("writer"),
        .type = MEL_PASS_COMPUTE,
        .write_lists = MEL_LISTS(&list));
    mel_render_graph_add_pass(&g, S8("reader"),
        .type = MEL_PASS_GRAPHICS,
        .read_lists = MEL_LISTS(&list));

    MEL_ASSERT(mel_render_graph_compile(&g));
    MEL_ASSERT_GT((u32)g.barriers.count, 0u);

    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_remove_pass, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g, .alloc = mel_alloc_heap());

    mel_render_graph_add_pass(&g, S8("A"));
    mel_render_graph_add_pass(&g, S8("B"));
    mel_render_graph_add_pass(&g, S8("C"));
    MEL_ASSERT_EQ((u32)g.passes.count, 3u);

    mel_render_graph_remove_pass(&g, S8("B"));
    MEL_ASSERT_EQ((u32)g.passes.count, 2u);
    MEL_ASSERT(g.dirty);

    MEL_ASSERT(mel_render_graph_compile(&g));
    MEL_ASSERT_EQ((u32)g.sorted_order.count, 2u);

    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_explicit_cycle_fails, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g, .alloc = mel_alloc_heap());

    u32 a = mel_render_graph_add_pass(&g, S8("A"));
    u32 b = mel_render_graph_add_pass(&g, S8("B"));

    mel_render_graph_pass_depends_on(&g, a, b);
    mel_render_graph_pass_depends_on(&g, b, a);

    MEL_ASSERT(!mel_render_graph_compile(&g));
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_remove_preserves_deps, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g, .alloc = mel_alloc_heap());

    u32 a = mel_render_graph_add_pass(&g, S8("A"));
    mel_render_graph_add_pass(&g, S8("B"));
    u32 c = mel_render_graph_add_pass(&g, S8("C"));

    mel_render_graph_pass_depends_on(&g, c, a);

    mel_render_graph_remove_pass(&g, S8("B"));

    MEL_ASSERT(mel_render_graph_compile(&g));
    MEL_ASSERT_EQ((u32)g.sorted_order.count, 2u);
    MEL_ASSERT_EQ(g.sorted_order.items[0], 0u);
    MEL_ASSERT_EQ(g.sorted_order.items[1], 1u);

    mel_render_graph_shutdown(&g);
}
