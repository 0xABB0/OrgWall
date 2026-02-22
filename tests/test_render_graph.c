#include "../melody/test.harness.h"
#include "../melody/render.graph.h"
#include "../melody/string.str8.h"

static u32 s_execution_order[64];
static u32 s_execution_count;

static void record_execute(Mel_Gpu_Cmd* cmd, void* user)
{
    MEL_UNUSED(cmd);
    u32 id = (u32)(uintptr_t)user;
    s_execution_order[s_execution_count++] = id;
}

MEL_TEST(graph_empty, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g);
    MEL_ASSERT(mel_render_graph_build(&g));
    MEL_ASSERT_EQ(g.sorted_count, 0u);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_single_pass, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g);
    u32 p = mel_render_graph_add_pass(&g, S8("only"), nullptr, nullptr);
    MEL_ASSERT(mel_render_graph_build(&g));
    MEL_ASSERT_EQ(g.sorted_count, 1u);
    MEL_ASSERT_EQ(g.sorted_order[0], p);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_linear_chain, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g);
    u32 a = mel_render_graph_add_pass(&g, S8("A"), nullptr, nullptr);
    u32 b = mel_render_graph_add_pass(&g, S8("B"), nullptr, nullptr);
    u32 c = mel_render_graph_add_pass(&g, S8("C"), nullptr, nullptr);
    mel_render_graph_pass_depends_on(&g, b, a);
    mel_render_graph_pass_depends_on(&g, c, b);
    MEL_ASSERT(mel_render_graph_build(&g));
    MEL_ASSERT_EQ(g.sorted_count, 3u);
    MEL_ASSERT_EQ(g.sorted_order[0], a);
    MEL_ASSERT_EQ(g.sorted_order[1], b);
    MEL_ASSERT_EQ(g.sorted_order[2], c);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_diamond, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g);
    u32 a = mel_render_graph_add_pass(&g, S8("A"), nullptr, nullptr);
    u32 b = mel_render_graph_add_pass(&g, S8("B"), nullptr, nullptr);
    u32 c = mel_render_graph_add_pass(&g, S8("C"), nullptr, nullptr);
    u32 d = mel_render_graph_add_pass(&g, S8("D"), nullptr, nullptr);
    mel_render_graph_pass_depends_on(&g, b, a);
    mel_render_graph_pass_depends_on(&g, c, a);
    mel_render_graph_pass_depends_on(&g, d, b);
    mel_render_graph_pass_depends_on(&g, d, c);
    MEL_ASSERT(mel_render_graph_build(&g));
    MEL_ASSERT_EQ(g.sorted_count, 4u);
    MEL_ASSERT_EQ(g.sorted_order[0], a);
    MEL_ASSERT_EQ(g.sorted_order[3], d);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_independent_passes, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g);
    mel_render_graph_add_pass(&g, S8("A"), nullptr, nullptr);
    mel_render_graph_add_pass(&g, S8("B"), nullptr, nullptr);
    MEL_ASSERT(mel_render_graph_build(&g));
    MEL_ASSERT_EQ(g.sorted_count, 2u);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_write_read_dep, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g);
    u32 res = mel_render_graph_add_resource(&g, S8("color"), VK_FORMAT_R8G8B8A8_UNORM, 100, 100, false);
    u32 writer = mel_render_graph_add_pass(&g, S8("writer"), nullptr, nullptr);
    u32 reader = mel_render_graph_add_pass(&g, S8("reader"), nullptr, nullptr);
    mel_render_graph_pass_writes(&g, writer, res);
    mel_render_graph_pass_reads(&g, reader, res);
    MEL_ASSERT(mel_render_graph_build(&g));
    MEL_ASSERT_EQ(g.sorted_count, 2u);
    MEL_ASSERT_EQ(g.sorted_order[0], writer);
    MEL_ASSERT_EQ(g.sorted_order[1], reader);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_cycle_fails, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g);
    u32 a = mel_render_graph_add_pass(&g, S8("A"), nullptr, nullptr);
    u32 b = mel_render_graph_add_pass(&g, S8("B"), nullptr, nullptr);
    mel_render_graph_pass_depends_on(&g, a, b);
    mel_render_graph_pass_depends_on(&g, b, a);
    MEL_ASSERT(!mel_render_graph_build(&g));
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_max_passes, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g);
    for (u32 i = 0; i < MEL_RENDER_GRAPH_MAX_PASSES; i++)
    {
        mel_render_graph_add_pass(&g, S8("pass"), nullptr, nullptr);
    }
    MEL_ASSERT_EQ(g.pass_count, (u32)MEL_RENDER_GRAPH_MAX_PASSES);
    MEL_ASSERT(mel_render_graph_build(&g));
    MEL_ASSERT_EQ(g.sorted_count, (u32)MEL_RENDER_GRAPH_MAX_PASSES);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_isolated_subgraphs, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g);
    u32 a = mel_render_graph_add_pass(&g, S8("A"), nullptr, nullptr);
    u32 b = mel_render_graph_add_pass(&g, S8("B"), nullptr, nullptr);
    u32 c = mel_render_graph_add_pass(&g, S8("C"), nullptr, nullptr);
    u32 d = mel_render_graph_add_pass(&g, S8("D"), nullptr, nullptr);

    mel_render_graph_pass_depends_on(&g, b, a);
    mel_render_graph_pass_depends_on(&g, d, c);

    MEL_ASSERT(mel_render_graph_build(&g));
    MEL_ASSERT_EQ(g.sorted_count, 4u);

    bool a_before_b = false;
    bool c_before_d = false;
    u32 pos_a = 0, pos_b = 0, pos_c = 0, pos_d = 0;
    for (u32 i = 0; i < g.sorted_count; i++)
    {
        if (g.sorted_order[i] == a) pos_a = i;
        if (g.sorted_order[i] == b) pos_b = i;
        if (g.sorted_order[i] == c) pos_c = i;
        if (g.sorted_order[i] == d) pos_d = i;
    }
    a_before_b = pos_a < pos_b;
    c_before_d = pos_c < pos_d;
    MEL_ASSERT(a_before_b);
    MEL_ASSERT(c_before_d);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_write_after_read, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g);
    u32 res = mel_render_graph_add_resource(&g, S8("buf"), VK_FORMAT_R32_SFLOAT, 1, 1, false);
    u32 reader = mel_render_graph_add_pass(&g, S8("reader"), nullptr, nullptr);
    u32 writer = mel_render_graph_add_pass(&g, S8("writer"), nullptr, nullptr);
    mel_render_graph_pass_reads(&g, reader, res);
    mel_render_graph_pass_writes(&g, writer, res);
    MEL_ASSERT(mel_render_graph_build(&g));
    MEL_ASSERT_EQ(g.sorted_count, 2u);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_multiple_writers, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g);
    u32 res = mel_render_graph_add_resource(&g, S8("target"), VK_FORMAT_R8G8B8A8_UNORM, 100, 100, false);
    u32 w1 = mel_render_graph_add_pass(&g, S8("writer1"), nullptr, nullptr);
    u32 w2 = mel_render_graph_add_pass(&g, S8("writer2"), nullptr, nullptr);
    mel_render_graph_pass_writes(&g, w1, res);
    mel_render_graph_pass_writes(&g, w2, res);
    MEL_ASSERT(mel_render_graph_build(&g));
    MEL_ASSERT_EQ(g.sorted_count, 2u);
    MEL_ASSERT_EQ(g.sorted_order[0], w1);
    MEL_ASSERT_EQ(g.sorted_order[1], w2);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_backbuffer_resource, .tags = "render")
{
    Mel_Render_Graph g;
    mel_render_graph_init(&g);
    u32 bb = mel_render_graph_add_resource(&g, S8("backbuffer"), VK_FORMAT_B8G8R8A8_SRGB, 800, 600, true);
    MEL_ASSERT(g.resources[bb].is_backbuffer);
    MEL_ASSERT_EQ(g.resources[bb].width, 800u);
    MEL_ASSERT_EQ(g.resources[bb].height, 600u);
    mel_render_graph_shutdown(&g);
}

MEL_TEST(graph_execution_order, .tags = "render")
{
    s_execution_count = 0;

    Mel_Render_Graph g;
    mel_render_graph_init(&g);
    u32 a = mel_render_graph_add_pass(&g, S8("A"), record_execute, (void*)(uintptr_t)0);
    u32 b = mel_render_graph_add_pass(&g, S8("B"), record_execute, (void*)(uintptr_t)1);
    u32 c = mel_render_graph_add_pass(&g, S8("C"), record_execute, (void*)(uintptr_t)2);
    mel_render_graph_pass_depends_on(&g, b, a);
    mel_render_graph_pass_depends_on(&g, c, b);
    MEL_ASSERT(mel_render_graph_build(&g));

    mel_render_graph_execute(&g, nullptr);

    MEL_ASSERT_EQ(s_execution_count, 3u);
    MEL_ASSERT_EQ(s_execution_order[0], 0u);
    MEL_ASSERT_EQ(s_execution_order[1], 1u);
    MEL_ASSERT_EQ(s_execution_order[2], 2u);

    MEL_UNUSED(a);
    MEL_UNUSED(b);
    MEL_UNUSED(c);
    mel_render_graph_shutdown(&g);
}
