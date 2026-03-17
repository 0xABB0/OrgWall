#include "../melody/test.harness.h"
#include "../melody/ecs.2d.text.h"
#include "../melody/ecs.2d.transform.h"
#include "../melody/string.str8.h"
#include "../melody/font.atlas.h"
#include "../melody/render.list.h"
#include "../melody/sprite.pass.h"
#include "../melody/collection.slotmap.h"
#include "../melody/allocator.heap.h"

#include <flecs.h>

MEL_TEST(ecs_text_component_register, .tags = "render")
{
    ecs_world_t* world = ecs_init();

    mel_component_transform_register(world);
    mel_component_text_register(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Mel_CTransform, { .pos = mel_vec2(10, 20) });
    ecs_set(world, e, Mel_CText, {
        .text = S8("hello"),
        .color = mel_vec4(1, 1, 1, 1),
    });

    const Mel_CText* txt = ecs_get(world, e, Mel_CText);
    MEL_ASSERT_NOT_NULL(txt);
    MEL_ASSERT(str8_equals(txt->text, S8("hello")));
    MEL_ASSERT_FLOAT_EQ(txt->color.r, 1.0f, 1e-5f);

    const Mel_CTransform* t = ecs_get(world, e, Mel_CTransform);
    MEL_ASSERT_NOT_NULL(t);
    MEL_ASSERT_FLOAT_EQ(t->pos.x, 10.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(t->pos.y, 20.0f, 1e-5f);

    ecs_fini(world);
}

static Mel_Font_Atlas_Handle mel__test_make_fake_font(Mel_Font_Glyph* glyphs, u32 count, f32 ascent, f32 line_height)
{
    Mel_Font_Atlas_Entry fake_entry = {
        .desc = {
            .glyphs = glyphs,
            .glyph_count = count,
            .first_codepoint = 32,
            .ascent = ascent,
            .line_height = line_height,
        },
    };
    return mel__font_atlas_insert_entry(&fake_entry);
}

MEL_TEST(ecs_text_system_pushes_glyphs, .tags = "render")
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Font_Glyph glyphs[96] = {0};
    glyphs['A' - 32] = (Mel_Font_Glyph){ .x0 = 0, .y0 = -10, .x1 = 8, .y1 = 0, .u0 = 0, .v0 = 0, .u1 = 0.1f, .v1 = 0.1f, .xadvance = 10 };
    glyphs['B' - 32] = (Mel_Font_Glyph){ .x0 = 0, .y0 = -10, .x1 = 8, .y1 = 0, .u0 = 0.1f, .v0 = 0, .u1 = 0.2f, .v1 = 0.1f, .xadvance = 10 };

    Mel_Font_Atlas_Handle font_handle = mel__test_make_fake_font(glyphs, 96, 12.0f, 16.0f);

    Mel_Render_List list;
    mel_render_list_init(&list, .entry_stride = sizeof(Mel_Sprite_Entry), .alloc = alloc);

    ecs_world_t* world = ecs_init();
    mel_component_transform_register(world);
    mel_component_text_register(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Mel_CTransform, { .pos = mel_vec2(100, 200) });
    ecs_set(world, e, Mel_CText, {
        .text = S8("AB"),
        .font = font_handle,
        .color = mel_vec4(1, 0, 0, 1),
    });

    ecs_progress(world, 0);

    mel_text_system_run(world, .list = &list);

    MEL_ASSERT_EQ(list.count, 2u);

    Mel_Sprite_Entry* first = mel_render_list_get(&list, 0);
    MEL_ASSERT_NOT_NULL(first);
    MEL_ASSERT_FLOAT_EQ(first->color.r, 1.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(first->color.g, 0.0f, 1e-5f);

    ecs_fini(world);
    mel_render_list_shutdown(&list);
}

MEL_TEST(ecs_text_system_skips_empty_text, .tags = "render")
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Font_Glyph glyphs[96] = {0};
    glyphs['X' - 32] = (Mel_Font_Glyph){ .x0 = 0, .y0 = -8, .x1 = 6, .y1 = 0, .xadvance = 8 };

    Mel_Font_Atlas_Handle font_handle = mel__test_make_fake_font(glyphs, 96, 10.0f, 14.0f);

    Mel_Render_List list;
    mel_render_list_init(&list, .entry_stride = sizeof(Mel_Sprite_Entry), .alloc = alloc);

    ecs_world_t* world = ecs_init();
    mel_component_transform_register(world);
    mel_component_text_register(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Mel_CTransform, { .pos = mel_vec2(0, 0) });
    ecs_set(world, e, Mel_CText, {
        .text = (str8){0},
        .font = font_handle,
        .color = mel_vec4(1, 1, 1, 1),
    });

    ecs_progress(world, 0);

    mel_text_system_run(world, .list = &list);

    MEL_ASSERT_EQ(list.count, 0u);

    ecs_fini(world);
    mel_render_list_shutdown(&list);
}

MEL_TEST(ecs_text_system_multiple_entities, .tags = "render")
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Font_Glyph glyphs[96] = {0};
    glyphs['A' - 32] = (Mel_Font_Glyph){ .x0 = 0, .y0 = -10, .x1 = 8, .y1 = 0, .xadvance = 10 };

    Mel_Font_Atlas_Handle font_handle = mel__test_make_fake_font(glyphs, 96, 12.0f, 16.0f);

    Mel_Render_List list;
    mel_render_list_init(&list, .entry_stride = sizeof(Mel_Sprite_Entry), .alloc = alloc);

    ecs_world_t* world = ecs_init();
    mel_component_transform_register(world);
    mel_component_text_register(world);

    ecs_entity_t e1 = ecs_new(world);
    ecs_set(world, e1, Mel_CTransform, { .pos = mel_vec2(0, 0) });
    ecs_set(world, e1, Mel_CText, {
        .text = S8("AAA"),
        .font = font_handle,
        .color = mel_vec4(1, 1, 1, 1),
    });

    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e2, Mel_CTransform, { .pos = mel_vec2(50, 50) });
    ecs_set(world, e2, Mel_CText, {
        .text = S8("AA"),
        .font = font_handle,
        .color = mel_vec4(0, 1, 0, 1),
    });

    ecs_progress(world, 0);

    mel_text_system_run(world, .list = &list);

    MEL_ASSERT_EQ(list.count, 5u);

    ecs_fini(world);
    mel_render_list_shutdown(&list);
}
