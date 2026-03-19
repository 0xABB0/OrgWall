#include "../melody/test.harness.h"
#include "../melody/render.manager.h"
#include "../melody/render.types.2d.h"
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

static void init_2d_mgr(Mel_Render_Manager* mgr, Mel_Gpu_Device* dev)
{
    Mel_Mgr_Pool_Desc pools[] = {
        { .item_size = sizeof(Mel_Render_Transform_2D) },
        { .item_size = sizeof(Mel_Render_Sprite_Info) },
    };
    mel_mgr_init(mgr, .dev = dev, .alloc = mel_alloc_heap(), .pools = pools, .pool_count = MEL_2D_POOL_COUNT);
}

MEL_TEST(mgr_2d_init_shutdown, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    Mel_Render_Manager mgr = {0};
    init_2d_mgr(&mgr, dev);

    MEL_ASSERT_EQ(mel_mgr_count(&mgr), 0);
    MEL_ASSERT_NOT_NULL(mel_mgr_pool_buffer(&mgr, MEL_2D_POOL_TRANSFORMS));
    MEL_ASSERT_NOT_NULL(mel_mgr_pool_buffer(&mgr, MEL_2D_POOL_INFOS));

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_2d_alloc_free, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    Mel_Render_Manager mgr = {0};
    init_2d_mgr(&mgr, dev);

    Mel_Render_Handle h1 = mel_mgr_alloc(&mgr, 0);
    Mel_Render_Handle h2 = mel_mgr_alloc(&mgr, 0);
    Mel_Render_Handle h3 = mel_mgr_alloc(&mgr, 0);

    MEL_ASSERT(mel_render_handle_valid(h1));
    MEL_ASSERT(mel_render_handle_valid(h2));
    MEL_ASSERT(mel_render_handle_valid(h3));
    MEL_ASSERT_EQ(mel_mgr_count(&mgr), 3);

    mel_mgr_free(&mgr, h2);
    MEL_ASSERT_EQ(mel_mgr_count(&mgr), 2);

    mel_mgr_free(&mgr, h1);
    mel_mgr_free(&mgr, h3);
    MEL_ASSERT_EQ(mel_mgr_count(&mgr), 0);

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_2d_set_transform, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    Mel_Render_Manager mgr = {0};
    init_2d_mgr(&mgr, dev);

    Mel_Render_Handle h = mel_mgr_alloc(&mgr, 0);

    Mel_Render_Transform_2D t = {
        .pos = mel_vec2(10.0f, 20.0f),
        .scale = mel_vec2(2.0f, 3.0f),
        .rotation = 1.5f,
        .depth = 5.0f,
        .flags = MEL_RF2D_FLIP_X,
    };
    mel_mgr_set(&mgr, MEL_2D_POOL_TRANSFORMS, h, &t);

    Mel_Render_Transform_2D* tp = mel_mgr_get(&mgr, MEL_2D_POOL_TRANSFORMS, h);
    MEL_ASSERT_NOT_NULL(tp);
    MEL_ASSERT_FLOAT_EQ(tp->pos.x, 10.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(tp->pos.y, 20.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(tp->scale.x, 2.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(tp->scale.y, 3.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(tp->rotation, 1.5f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(tp->depth, 5.0f, 0.001f);
    MEL_ASSERT_EQ(tp->flags, MEL_RF2D_FLIP_X);

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_2d_set_sprite_info, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    Mel_Render_Manager mgr = {0};
    init_2d_mgr(&mgr, dev);

    Mel_Render_Handle h = mel_mgr_alloc(&mgr, 7);

    Mel_Render_Sprite_Info info = {
        .uv = mel_rect(0.0f, 0.0f, 1.0f, 1.0f),
        .color = mel_vec4(1.0f, 0.5f, 0.25f, 1.0f),
        .texture_idx = 42,
        .material_base_id = 7,
        .layer = 3,
    };
    mel_mgr_set(&mgr, MEL_2D_POOL_INFOS, h, &info);

    Mel_Render_Sprite_Info* ip = mel_mgr_get(&mgr, MEL_2D_POOL_INFOS, h);
    MEL_ASSERT_NOT_NULL(ip);
    MEL_ASSERT_FLOAT_EQ(ip->uv.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ip->uv.w, 1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ip->color.r, 1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ip->color.g, 0.5f, 0.001f);
    MEL_ASSERT_EQ(ip->texture_idx, 42);
    MEL_ASSERT_EQ(ip->material_base_id, 7);
    MEL_ASSERT_EQ(ip->layer, 3);

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_2d_upload_dirty, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    Mel_Render_Manager mgr = {0};
    init_2d_mgr(&mgr, dev);

    Mel_Render_Handle h = mel_mgr_alloc(&mgr, 0);
    mel_mgr_set(&mgr, MEL_2D_POOL_TRANSFORMS, h, &(Mel_Render_Transform_2D){
        .pos = mel_vec2(1.0f, 2.0f),
        .scale = MEL_VEC2_ONE,
    });
    mel_mgr_set(&mgr, MEL_2D_POOL_INFOS, h, &(Mel_Render_Sprite_Info){
        .uv = mel_rect(0, 0, 1, 1),
        .color = mel_vec4(1, 1, 1, 1),
        .texture_idx = 0,
        .layer = 0,
    });

    mel_mgr_upload_dirty(&mgr);

    MEL_ASSERT_EQ(mel_mgr_count(&mgr), 1);

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_2d_group_ranges, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    Mel_Render_Manager mgr = {0};
    init_2d_mgr(&mgr, dev);

    mel_mgr_alloc(&mgr, 0);
    mel_mgr_alloc(&mgr, 0);
    mel_mgr_alloc(&mgr, 1);

    mel_mgr_upload_dirty(&mgr);

    MEL_ASSERT_EQ(mel_mgr_group_count(&mgr), 2);

    const Mel_Mgr_Range* ranges = mel_mgr_group_ranges(&mgr);
    MEL_ASSERT_NOT_NULL(ranges);

    u32 total = 0;
    for (u32 i = 0; i < mel_mgr_group_count(&mgr); i++)
        total += ranges[i].count;
    MEL_ASSERT_EQ(total, 3);

    mel_mgr_shutdown(&mgr);
}
