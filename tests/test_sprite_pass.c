#include "../melody/test.harness.h"
#include "../melody/sprite.pass.h"
#include "../melody/render.list.h"
#include "../melody/allocator.heap.h"

MEL_TEST(sort_key_layer_priority, .tags = "render")
{
    u64 layer0_deep = mel_sort_key_sprite(0, 100.0f, 0, 0);
    u64 layer1_shallow = mel_sort_key_sprite(1, 0.0f, 0, 0);
    MEL_ASSERT(layer0_deep < layer1_shallow);
}

MEL_TEST(sort_key_depth_within_layer, .tags = "render")
{
    u64 shallow = mel_sort_key_sprite(0, 0.0f, 0, 0);
    u64 deep = mel_sort_key_sprite(0, 10.0f, 0, 0);
    MEL_ASSERT(shallow < deep);
}

MEL_TEST(sort_key_negative_depth, .tags = "render")
{
    u64 neg = mel_sort_key_sprite(0, -5.0f, 0, 0);
    u64 zero = mel_sort_key_sprite(0, 0.0f, 0, 0);
    u64 pos = mel_sort_key_sprite(0, 5.0f, 0, 0);
    MEL_ASSERT(neg < zero);
    MEL_ASSERT(zero < pos);
}

MEL_TEST(sort_key_material_ordering, .tags = "render")
{
    u64 mat0 = mel_sort_key_sprite(0, 0.0f, 0, 0);
    u64 mat1 = mel_sort_key_sprite(0, 0.0f, 1, 0);
    u64 mat2 = mel_sort_key_sprite(0, 0.0f, 2, 0);
    MEL_ASSERT(mat0 < mat1);
    MEL_ASSERT(mat1 < mat2);
}

MEL_TEST(sort_key_texture_bucket_ordering, .tags = "render")
{
    u64 tex0 = mel_sort_key_sprite(0, 0.0f, 0, 0);
    u64 tex1 = mel_sort_key_sprite(0, 0.0f, 0, 1);
    u64 tex5 = mel_sort_key_sprite(0, 0.0f, 0, 5);
    MEL_ASSERT(tex0 < tex1);
    MEL_ASSERT(tex1 < tex5);
}

MEL_TEST(sort_key_texture_groups_within_depth, .tags = "render")
{
    u64 d0_t0 = mel_sort_key_sprite(0, 0.0f, 0, 0);
    u64 d0_t1 = mel_sort_key_sprite(0, 0.0f, 0, 1);
    u64 d1_t0 = mel_sort_key_sprite(0, 1.0f, 0, 0);
    MEL_ASSERT(d0_t0 < d0_t1);
    MEL_ASSERT(d0_t1 < d1_t0);
}

MEL_TEST(sprite_list_sort_ordering, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    Mel_Sprite_Entry* a = mel_render_list_push(&list, mel_sort_key_sprite(1, 0.0f, 0, 0));
    a->pos = mel_vec2(0, 0);
    a->size = mel_vec2(10, 10);
    a->uv = MEL_UV_FULL;
    a->color = mel_vec4(1, 0, 0, 1);
    a->tex = MEL_TEXTURE_HANDLE_NULL;

    Mel_Sprite_Entry* b = mel_render_list_push(&list, mel_sort_key_sprite(0, 5.0f, 0, 0));
    b->pos = mel_vec2(20, 20);
    b->size = mel_vec2(10, 10);
    b->uv = MEL_UV_FULL;
    b->color = mel_vec4(0, 1, 0, 1);
    b->tex = MEL_TEXTURE_HANDLE_NULL;

    Mel_Sprite_Entry* c = mel_render_list_push(&list, mel_sort_key_sprite(0, 1.0f, 0, 0));
    c->pos = mel_vec2(40, 40);
    c->size = mel_vec2(10, 10);
    c->uv = MEL_UV_FULL;
    c->color = mel_vec4(0, 0, 1, 1);
    c->tex = MEL_TEXTURE_HANDLE_NULL;

    mel_render_list_sort(&list);

    Mel_Sprite_Entry* first = mel_render_list_get(&list, list.packets[0].entry_index);
    Mel_Sprite_Entry* second = mel_render_list_get(&list, list.packets[1].entry_index);
    Mel_Sprite_Entry* third = mel_render_list_get(&list, list.packets[2].entry_index);

    MEL_ASSERT_FLOAT_EQ(first->pos.x, 40.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(second->pos.x, 20.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(third->pos.x, 0.0f, 1e-5f);

    mel_render_list_shutdown(&list);
}

MEL_TEST(sprite_list_ephemeral_clear_push, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    for (u32 i = 0; i < 10; i++)
    {
        Mel_Sprite_Entry* e = mel_render_list_push(&list, mel_sort_key_sprite(0, (f32)i, 0, 0));
        e->pos = mel_vec2((f32)i, (f32)i);
        e->size = mel_vec2(1, 1);
        e->uv = MEL_UV_FULL;
        e->color = mel_vec4(1, 1, 1, 1);
        e->tex = MEL_TEXTURE_HANDLE_NULL;
    }
    MEL_ASSERT_EQ(list.count, 10u);

    mel_render_list_clear(&list);
    MEL_ASSERT_EQ(list.count, 0u);
    MEL_ASSERT_EQ(list.slot_count, 0u);

    for (u32 i = 0; i < 5; i++)
    {
        Mel_Sprite_Entry* e = mel_render_list_push(&list, mel_sort_key_sprite(0, (f32)i, 0, 0));
        e->pos = mel_vec2((f32)(i * 10), (f32)(i * 10));
        e->size = mel_vec2(2, 2);
        e->uv = MEL_UV_FULL;
        e->color = mel_vec4(0, 1, 0, 1);
        e->tex = MEL_TEXTURE_HANDLE_NULL;
    }
    MEL_ASSERT_EQ(list.count, 5u);

    Mel_Sprite_Entry* e = mel_render_list_get(&list, 0);
    MEL_ASSERT_FLOAT_EQ(e->pos.x, 0.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(e->size.x, 2.0f, 1e-5f);

    mel_render_list_shutdown(&list);
}

MEL_TEST(sort_key_texture_bucket_groups_sprites, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    Mel_Sprite_Entry* a = mel_render_list_push(&list, mel_sort_key_sprite(0, 0.0f, 0, 3));
    a->pos = mel_vec2(0, 0);
    a->size = mel_vec2(10, 10);
    a->uv = MEL_UV_FULL;
    a->color = mel_vec4(1, 1, 1, 1);
    a->tex = MEL_TEXTURE_HANDLE_NULL;

    Mel_Sprite_Entry* b = mel_render_list_push(&list, mel_sort_key_sprite(0, 0.0f, 0, 1));
    b->pos = mel_vec2(10, 0);
    b->size = mel_vec2(10, 10);
    b->uv = MEL_UV_FULL;
    b->color = mel_vec4(1, 1, 1, 1);
    b->tex = MEL_TEXTURE_HANDLE_NULL;

    Mel_Sprite_Entry* c = mel_render_list_push(&list, mel_sort_key_sprite(0, 0.0f, 0, 1));
    c->pos = mel_vec2(20, 0);
    c->size = mel_vec2(10, 10);
    c->uv = MEL_UV_FULL;
    c->color = mel_vec4(1, 1, 1, 1);
    c->tex = MEL_TEXTURE_HANDLE_NULL;

    Mel_Sprite_Entry* d = mel_render_list_push(&list, mel_sort_key_sprite(0, 0.0f, 0, 3));
    d->pos = mel_vec2(30, 0);
    d->size = mel_vec2(10, 10);
    d->uv = MEL_UV_FULL;
    d->color = mel_vec4(1, 1, 1, 1);
    d->tex = MEL_TEXTURE_HANDLE_NULL;

    mel_render_list_sort(&list);

    Mel_Sprite_Entry* first = mel_render_list_get(&list, list.packets[0].entry_index);
    Mel_Sprite_Entry* second = mel_render_list_get(&list, list.packets[1].entry_index);
    Mel_Sprite_Entry* third = mel_render_list_get(&list, list.packets[2].entry_index);
    Mel_Sprite_Entry* fourth = mel_render_list_get(&list, list.packets[3].entry_index);

    MEL_ASSERT(list.packets[0].sort_key < list.packets[2].sort_key || list.packets[0].sort_key == list.packets[2].sort_key);
    MEL_ASSERT(list.packets[1].sort_key <= list.packets[2].sort_key);
    MEL_ASSERT(list.packets[2].sort_key <= list.packets[3].sort_key);

    MEL_ASSERT(first->pos.x == 10.0f || first->pos.x == 20.0f);
    MEL_ASSERT(second->pos.x == 10.0f || second->pos.x == 20.0f);
    MEL_ASSERT(third->pos.x == 0.0f || third->pos.x == 30.0f);
    MEL_ASSERT(fourth->pos.x == 0.0f || fourth->pos.x == 30.0f);

    mel_render_list_shutdown(&list);
}
