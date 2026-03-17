#include "../melody/test.harness.h"
#include "../melody/render.manager.2d.h"
#include "../melody/gpu.device.h"
#include "../melody/allocator.heap.h"
#include "../melody/math.vec2.h"
#include "../melody/math.vec4.h"
#include "../melody/math.geo.rect.h"

#include <SDL3/SDL.h>

static Mel_Gpu_Device s_dev;
static bool s_dev_ready = false;

static Mel_Gpu_Device* test_gpu_dev(void)
{
    if (!s_dev_ready)
    {
        SDL_Init(SDL_INIT_VIDEO);
        if (!mel_gpu_device_init(&s_dev, .allocator = mel_alloc_heap()))
            return nullptr;
        s_dev_ready = true;
    }
    return &s_dev;
}

_Static_assert(sizeof(Mel_Render_Transform_2D) == 32, "Mel_Render_Transform_2D must be 32 bytes");
_Static_assert(sizeof(Mel_Render_Bounds_2D) == 16, "Mel_Render_Bounds_2D must be 16 bytes");
_Static_assert(sizeof(Mel_Render_Sprite_Info) == 48, "Mel_Render_Sprite_Info must be 48 bytes");

MEL_TEST(mgr_2d_init_shutdown, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    Mel_Render_Manager_2D mgr = {0};
    mel_mgr_2d_init(&mgr, .dev = dev, .alloc = mel_alloc_heap());

    MEL_ASSERT_EQ(mel_mgr_2d_count(&mgr), 0);
    MEL_ASSERT_NOT_NULL(mel_mgr_2d_transform_buffer(&mgr));
    MEL_ASSERT_NOT_NULL(mel_mgr_2d_bounds_buffer(&mgr));
    MEL_ASSERT_NOT_NULL(mel_mgr_2d_sprite_info_buffer(&mgr));

    mel_mgr_2d_shutdown(&mgr);
}

MEL_TEST(mgr_2d_alloc_free, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    Mel_Render_Manager_2D mgr = {0};
    mel_mgr_2d_init(&mgr, .dev = dev, .alloc = mel_alloc_heap());

    Mel_Render_Handle_2D h1 = mel_mgr_2d_alloc(&mgr);
    Mel_Render_Handle_2D h2 = mel_mgr_2d_alloc(&mgr);
    Mel_Render_Handle_2D h3 = mel_mgr_2d_alloc(&mgr);

    MEL_ASSERT(mel_render_handle_2d_valid(h1));
    MEL_ASSERT(mel_render_handle_2d_valid(h2));
    MEL_ASSERT(mel_render_handle_2d_valid(h3));
    MEL_ASSERT_EQ(mel_mgr_2d_count(&mgr), 3);

    mel_mgr_2d_free(&mgr, h2);
    MEL_ASSERT_EQ(mel_mgr_2d_count(&mgr), 2);

    mel_mgr_2d_free(&mgr, h1);
    mel_mgr_2d_free(&mgr, h3);
    MEL_ASSERT_EQ(mel_mgr_2d_count(&mgr), 0);

    mel_mgr_2d_shutdown(&mgr);
}

MEL_TEST(mgr_2d_set_transform, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    Mel_Render_Manager_2D mgr = {0};
    mel_mgr_2d_init(&mgr, .dev = dev, .alloc = mel_alloc_heap());

    Mel_Render_Handle_2D h = mel_mgr_2d_alloc(&mgr);

    Mel_Render_Transform_2D t = {
        .pos = mel_vec2(10.0f, 20.0f),
        .scale = mel_vec2(2.0f, 3.0f),
        .rotation = 1.5f,
        .depth = 5.0f,
        .flags = MEL_RF2D_FLIP_X,
    };
    mel_mgr_2d_set_transform(&mgr, h, t);

    Mel_Render_Transform_2D* tp = mel_storage_pool_get(&mgr.transforms, h.handle);
    MEL_ASSERT_NOT_NULL(tp);
    MEL_ASSERT_FLOAT_EQ(tp->pos.x, 10.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(tp->pos.y, 20.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(tp->scale.x, 2.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(tp->scale.y, 3.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(tp->rotation, 1.5f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(tp->depth, 5.0f, 0.001f);
    MEL_ASSERT_EQ(tp->flags, MEL_RF2D_FLIP_X);

    mel_mgr_2d_shutdown(&mgr);
}

MEL_TEST(mgr_2d_set_bounds, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    Mel_Render_Manager_2D mgr = {0};
    mel_mgr_2d_init(&mgr, .dev = dev, .alloc = mel_alloc_heap());

    Mel_Render_Handle_2D h = mel_mgr_2d_alloc(&mgr);

    Mel_Render_Bounds_2D b = {
        .center = mel_vec2(5.0f, 10.0f),
        .half_extents = mel_vec2(2.0f, 3.0f),
    };
    mel_mgr_2d_set_bounds(&mgr, h, b);

    Mel_Render_Bounds_2D* bp = mel_storage_pool_get(&mgr.bounds, h.handle);
    MEL_ASSERT_NOT_NULL(bp);
    MEL_ASSERT_FLOAT_EQ(bp->center.x, 5.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(bp->center.y, 10.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(bp->half_extents.x, 2.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(bp->half_extents.y, 3.0f, 0.001f);

    mel_mgr_2d_shutdown(&mgr);
}

MEL_TEST(mgr_2d_set_sprite_info, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    Mel_Render_Manager_2D mgr = {0};
    mel_mgr_2d_init(&mgr, .dev = dev, .alloc = mel_alloc_heap());

    Mel_Render_Handle_2D h = mel_mgr_2d_alloc(&mgr);

    Mel_Render_Sprite_Info info = {
        .uv = mel_rect(0.0f, 0.0f, 1.0f, 1.0f),
        .color = mel_vec4(1.0f, 0.5f, 0.25f, 1.0f),
        .texture_idx = 42,
        .material_base_id = 7,
        .layer = 3,
    };
    mel_mgr_2d_set_sprite_info(&mgr, h, info);

    Mel_Render_Sprite_Info* ip = mel_storage_pool_get(&mgr.sprite_infos, h.handle);
    MEL_ASSERT_NOT_NULL(ip);
    MEL_ASSERT_FLOAT_EQ(ip->uv.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ip->uv.w, 1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ip->color.r, 1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ip->color.g, 0.5f, 0.001f);
    MEL_ASSERT_EQ(ip->texture_idx, 42);
    MEL_ASSERT_EQ(ip->material_base_id, 7);
    MEL_ASSERT_EQ(ip->layer, 3);

    mel_mgr_2d_shutdown(&mgr);
}

MEL_TEST(mgr_2d_cull_rect, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    Mel_Render_Manager_2D mgr = {0};
    mel_mgr_2d_init(&mgr, .dev = dev, .alloc = mel_alloc_heap());

    Mel_Render_Handle_2D h_visible = mel_mgr_2d_alloc(&mgr);
    mel_mgr_2d_set_bounds(&mgr, h_visible, (Mel_Render_Bounds_2D){
        .center = mel_vec2(50.0f, 50.0f),
        .half_extents = mel_vec2(10.0f, 10.0f),
    });

    Mel_Render_Handle_2D h_offscreen = mel_mgr_2d_alloc(&mgr);
    mel_mgr_2d_set_bounds(&mgr, h_offscreen, (Mel_Render_Bounds_2D){
        .center = mel_vec2(5000.0f, 5000.0f),
        .half_extents = mel_vec2(10.0f, 10.0f),
    });

    Mel_Rect viewport = mel_rect(0.0f, 0.0f, 100.0f, 100.0f);
    Mel_BitSet visibility = {0};
    mel_bitset_init(&visibility, 64, mel_alloc_heap());

    mel_mgr_2d_cull_rect(&mgr, viewport, &visibility);

    MEL_ASSERT(mel_bitset_get(&visibility, 0));
    MEL_ASSERT(!mel_bitset_get(&visibility, 1));

    mel_bitset_free(&visibility);
    mel_mgr_2d_shutdown(&mgr);
}

MEL_TEST(mgr_2d_upload_dirty, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    Mel_Render_Manager_2D mgr = {0};
    mel_mgr_2d_init(&mgr, .dev = dev, .alloc = mel_alloc_heap());

    Mel_Render_Handle_2D h = mel_mgr_2d_alloc(&mgr);
    mel_mgr_2d_set_transform(&mgr, h, (Mel_Render_Transform_2D){
        .pos = mel_vec2(1.0f, 2.0f),
        .scale = MEL_VEC2_ONE,
    });
    mel_mgr_2d_set_bounds(&mgr, h, (Mel_Render_Bounds_2D){
        .center = mel_vec2(1.0f, 2.0f),
        .half_extents = mel_vec2(1.0f, 1.0f),
    });
    mel_mgr_2d_set_sprite_info(&mgr, h, (Mel_Render_Sprite_Info){
        .uv = mel_rect(0, 0, 1, 1),
        .color = mel_vec4(1, 1, 1, 1),
        .texture_idx = 0,
        .layer = 0,
    });

    mel_mgr_2d_upload_dirty(&mgr);

    MEL_ASSERT(!mel_storage_pool_is_dirty(&mgr.transforms));
    MEL_ASSERT(!mel_storage_pool_is_dirty(&mgr.bounds));
    MEL_ASSERT(!mel_storage_pool_is_dirty(&mgr.sprite_infos));

    mel_mgr_2d_shutdown(&mgr);
}
