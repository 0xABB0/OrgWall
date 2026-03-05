#include "../melody/test.harness.h"
#include "../melody/render.draw.h"
#include "../melody/gpu.texture.h"
#include "../melody/allocator.heap.h"

MEL_TEST(draw_ctx_init_shutdown, .tags = "render")
{
    Mel_Draw_Ctx ctx;
    mel_draw_ctx_init(&ctx, .alloc = mel_alloc_heap());

    MEL_ASSERT_EQ(ctx.vertex_count, 0u);
    MEL_ASSERT_EQ(ctx.index_count, 0u);
    MEL_ASSERT_EQ(ctx.vertex_capacity, 0u);
    MEL_ASSERT_EQ(ctx.index_capacity, 0u);
    MEL_ASSERT_NULL(ctx.vertices);
    MEL_ASSERT_NULL(ctx.indices);
    MEL_ASSERT_NULL(ctx.draws);
    MEL_ASSERT_EQ(ctx.draw_count, 0u);
    MEL_ASSERT(!ctx.committed);

    mel_draw_ctx_shutdown(&ctx);
}

MEL_TEST(draw_ctx_rect_vertices, .tags = "render")
{
    Mel_Draw_Ctx ctx;
    mel_draw_ctx_init(&ctx, .alloc = mel_alloc_heap());

    Mel_Vec4 color = mel_vec4(1.0f, 0.5f, 0.25f, 1.0f);
    mel_draw_ctx_rect(&ctx, 10.0f, 20.0f, 30.0f, 40.0f, color);

    MEL_ASSERT_EQ(ctx.vertex_count, 4u);
    MEL_ASSERT_EQ(ctx.index_count, 6u);

    MEL_ASSERT_FLOAT_EQ(ctx.vertices[0].x, 10.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[0].y, 20.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[1].x, 40.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[1].y, 20.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[2].x, 40.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[2].y, 60.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[3].x, 10.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[3].y, 60.0f, 1e-5f);

    MEL_ASSERT_FLOAT_EQ(ctx.vertices[0].r, 1.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[0].g, 0.5f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[0].b, 0.25f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[0].a, 1.0f, 1e-5f);

    MEL_ASSERT_EQ(ctx.indices[0], (u16)0);
    MEL_ASSERT_EQ(ctx.indices[1], (u16)1);
    MEL_ASSERT_EQ(ctx.indices[2], (u16)2);
    MEL_ASSERT_EQ(ctx.indices[3], (u16)2);
    MEL_ASSERT_EQ(ctx.indices[4], (u16)3);
    MEL_ASSERT_EQ(ctx.indices[5], (u16)0);

    mel_draw_ctx_shutdown(&ctx);
}

MEL_TEST(draw_ctx_line_vertices, .tags = "render")
{
    Mel_Draw_Ctx ctx;
    mel_draw_ctx_init(&ctx, .alloc = mel_alloc_heap());

    Mel_Vec2 from = mel_vec2(0.0f, 0.0f);
    Mel_Vec2 to = mel_vec2(10.0f, 0.0f);
    Mel_Vec4 color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);

    mel_draw_ctx_line(&ctx, from, to, 2.0f, color);

    MEL_ASSERT_EQ(ctx.vertex_count, 4u);
    MEL_ASSERT_EQ(ctx.index_count, 6u);

    MEL_ASSERT_FLOAT_EQ(ctx.vertices[0].x, 0.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[0].y, -1.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[1].x, 0.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[1].y, 1.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[2].x, 10.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[2].y, 1.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[3].x, 10.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[3].y, -1.0f, 1e-5f);

    mel_draw_ctx_shutdown(&ctx);
}

MEL_TEST(draw_ctx_clear_resets, .tags = "render")
{
    Mel_Draw_Ctx ctx;
    mel_draw_ctx_init(&ctx, .alloc = mel_alloc_heap());

    mel_draw_ctx_rect(&ctx, 0, 0, 10, 10, mel_vec4(1, 1, 1, 1));
    MEL_ASSERT_EQ(ctx.vertex_count, 4u);

    u32 vert_cap = ctx.vertex_capacity;
    u32 idx_cap = ctx.index_capacity;

    mel_draw_ctx_clear(&ctx);

    MEL_ASSERT_EQ(ctx.vertex_count, 0u);
    MEL_ASSERT_EQ(ctx.index_count, 0u);
    MEL_ASSERT_EQ(ctx.draw_count, 0u);
    MEL_ASSERT_EQ(ctx.vertex_capacity, vert_cap);
    MEL_ASSERT_EQ(ctx.index_capacity, idx_cap);
    MEL_ASSERT_NOT_NULL(ctx.vertices);
    MEL_ASSERT_NOT_NULL(ctx.indices);
    MEL_ASSERT(!ctx.committed);

    mel_draw_ctx_shutdown(&ctx);
}

MEL_TEST(draw_ctx_growth, .tags = "render")
{
    Mel_Draw_Ctx ctx;
    mel_draw_ctx_init(&ctx, .alloc = mel_alloc_heap());

    for (u32 i = 0; i < 65; i++)
        mel_draw_ctx_rect(&ctx, 0, 0, 1, 1, mel_vec4(1, 1, 1, 1));

    MEL_ASSERT_EQ(ctx.vertex_count, 65u * 4);
    MEL_ASSERT_GE(ctx.vertex_capacity, 65u * 4);
    MEL_ASSERT_GT(ctx.vertex_capacity, 256u);

    mel_draw_ctx_shutdown(&ctx);
}

MEL_TEST(draw_ctx_multiple_rects, .tags = "render")
{
    Mel_Draw_Ctx ctx;
    mel_draw_ctx_init(&ctx, .alloc = mel_alloc_heap());

    mel_draw_ctx_rect(&ctx, 0, 0, 10, 10, mel_vec4(1, 0, 0, 1));
    mel_draw_ctx_rect(&ctx, 20, 20, 10, 10, mel_vec4(0, 1, 0, 1));

    MEL_ASSERT_EQ(ctx.vertex_count, 8u);
    MEL_ASSERT_EQ(ctx.index_count, 12u);

    MEL_ASSERT_EQ(ctx.indices[6], (u16)4);
    MEL_ASSERT_EQ(ctx.indices[7], (u16)5);
    MEL_ASSERT_EQ(ctx.indices[8], (u16)6);
    MEL_ASSERT_EQ(ctx.indices[9], (u16)6);
    MEL_ASSERT_EQ(ctx.indices[10], (u16)7);
    MEL_ASSERT_EQ(ctx.indices[11], (u16)4);

    MEL_ASSERT_FLOAT_EQ(ctx.vertices[4].x, 20.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[4].y, 20.0f, 1e-5f);

    MEL_ASSERT_FLOAT_EQ(ctx.vertices[0].r, 1.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[0].g, 0.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[4].r, 0.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(ctx.vertices[4].g, 1.0f, 1e-5f);

    mel_draw_ctx_shutdown(&ctx);
}

MEL_TEST(draw_ctx_no_texture_no_draws, .tags = "render")
{
    Mel_Draw_Ctx ctx;
    mel_draw_ctx_init(&ctx, .alloc = mel_alloc_heap());

    MEL_ASSERT_EQ(ctx.draw_count, 0u);
    MEL_ASSERT_NULL(ctx.default_texture);
    MEL_ASSERT_NULL(ctx.current_texture);

    mel_draw_ctx_rect(&ctx, 0, 0, 10, 10, mel_vec4(1, 1, 1, 1));

    MEL_ASSERT_EQ(ctx.draw_count, 0u);
    MEL_ASSERT_EQ(ctx.vertex_count, 4u);

    mel_draw_ctx_shutdown(&ctx);
}

MEL_TEST(draw_ctx_with_texture_initial_draw, .tags = "render")
{
    Mel_Gpu_Texture fake_tex = {0};
    fake_tex.descriptor = (VkDescriptorSet)(uintptr_t)0xDEAD;

    Mel_Draw_Ctx ctx;
    mel_draw_ctx_init(&ctx, .texture = &fake_tex, .alloc = mel_alloc_heap());

    MEL_ASSERT_EQ(ctx.draw_count, 1u);
    MEL_ASSERT_NOT_NULL(ctx.draws);
    MEL_ASSERT_EQ(ctx.draws[0].index_offset, 0u);
    MEL_ASSERT_EQ(ctx.draws[0].index_count, 0u);
    MEL_ASSERT(ctx.draws[0].descriptor == (VkDescriptorSet)(uintptr_t)0xDEAD);

    mel_draw_ctx_rect(&ctx, 0, 0, 10, 10, mel_vec4(1, 1, 1, 1));
    mel_draw_ctx_rect(&ctx, 20, 0, 10, 10, mel_vec4(1, 1, 1, 1));

    MEL_ASSERT_EQ(ctx.draw_count, 1u);
    MEL_ASSERT_EQ(ctx.vertex_count, 8u);
    MEL_ASSERT_EQ(ctx.index_count, 12u);

    mel_draw_ctx_shutdown(&ctx);
}

MEL_TEST(draw_ctx_clear_with_texture_resets_draws, .tags = "render")
{
    Mel_Gpu_Texture fake_tex = {0};
    fake_tex.descriptor = (VkDescriptorSet)(uintptr_t)0xBEEF;

    Mel_Draw_Ctx ctx;
    mel_draw_ctx_init(&ctx, .texture = &fake_tex, .alloc = mel_alloc_heap());

    mel_draw_ctx_rect(&ctx, 0, 0, 10, 10, mel_vec4(1, 1, 1, 1));

    mel_draw_ctx_clear(&ctx);

    MEL_ASSERT_EQ(ctx.vertex_count, 0u);
    MEL_ASSERT_EQ(ctx.index_count, 0u);
    MEL_ASSERT_EQ(ctx.draw_count, 1u);
    MEL_ASSERT_EQ(ctx.draws[0].index_offset, 0u);
    MEL_ASSERT(ctx.draws[0].descriptor == (VkDescriptorSet)(uintptr_t)0xBEEF);
    MEL_ASSERT(ctx.current_texture == &fake_tex);

    mel_draw_ctx_shutdown(&ctx);
}
