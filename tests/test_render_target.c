#include "../melody/test.harness.h"
#include "../melody/render.target.h"
#include "../melody/allocator.heap.h"
#include "../melody/gpu.device.h"
#include "../melody/swapchain.h"

#include <SDL3/SDL.h>
#include <string.h>

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

MEL_TEST(render_target_type_constants, .tags = "render")
{
    MEL_ASSERT_EQ(MEL_TARGET_WINDOW,    0u);
    MEL_ASSERT_EQ(MEL_TARGET_OFFSCREEN, 1u);
    MEL_ASSERT_EQ(MEL_TARGET_ARRAY,     2u);
}

MEL_TEST(render_target_handle_null_is_invalid, .tags = "render")
{
    Mel_Render_Target_Handle h = MEL_RENDER_TARGET_HANDLE_NULL;
    MEL_ASSERT(!mel_render_target_handle_valid(h));
    MEL_ASSERT(!mel_render_target_alive(h));
}

MEL_TEST(render_target_offscreen_create_destroy, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) MEL_FAIL("GPU device unavailable");

    Mel_Render_Target_Handle handle = mel_render_target_offscreen(
        .width  = 512,
        .height = 512,
        .format = MEL_GPU_FORMAT_R8G8B8A8_UNORM,
        .dev    = dev,
        .alloc  = mel_alloc_heap());

    MEL_ASSERT(mel_render_target_handle_valid(handle));
    MEL_ASSERT(mel_render_target_alive(handle));

    Mel_Render_Target* target = mel_render_target_get(handle);
    MEL_ASSERT_NOT_NULL(target);
    MEL_ASSERT_EQ(mel_render_target_width(target),  512u);
    MEL_ASSERT_EQ(mel_render_target_height(target), 512u);
    MEL_ASSERT_EQ(mel_render_target_format(target), (Mel_Gpu_Format)MEL_GPU_FORMAT_R8G8B8A8_UNORM);
    MEL_ASSERT_EQ(target->type, (u32)MEL_TARGET_OFFSCREEN);
    MEL_ASSERT_NOT_NULL(mel_render_target_image_view(target));

    mel_render_target_destroy(handle);
    MEL_ASSERT(!mel_render_target_alive(handle));
}

MEL_TEST(render_target_destroy_idempotent, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) MEL_FAIL("GPU device unavailable");

    Mel_Render_Target_Handle handle = mel_render_target_offscreen(
        .width  = 64,
        .height = 64,
        .format = MEL_GPU_FORMAT_R8G8B8A8_UNORM,
        .dev    = dev);

    mel_render_target_destroy(handle);
    mel_render_target_destroy(handle);
}
