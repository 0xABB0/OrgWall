#include "test.visual.h"
#include "render.graph.h"
#include "render.pass.h"
#include "swapchain.image.h"
#include "gpu.device.vulkan.h"
#include "debug.backtrace.h"
#include "string.str8.h"
#include "allocator.heap.h"

#include <stb_image.h>
#include <stb_image_write.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MEL_VISUAL_TEST_CHANNEL_THRESHOLD 2
#define MEL_VISUAL_TEST_FAIL_PERCENT 0.1f

static Mel_Visual_Test_Render_Fn s_active_render_fn;

static void visual_test_pass(Mel_Render_Pass_Ctx* ctx)
{
    if (s_active_render_fn)
        s_active_render_fn(ctx);
}

static void on_present(void* pixels, u32 width, u32 height, u32 stride, void* user_data)
{
    Mel_Visual_Test_Ctx* ctx = user_data;
    usize size = (usize)stride * height;

    if (!ctx->captured_pixels)
        ctx->captured_pixels = malloc(size);

    memcpy(ctx->captured_pixels, pixels, size);
    ctx->captured_stride = stride;
    ctx->has_capture = true;
}

static void ensure_dir(const char* path)
{
    mkdir(path, 0755);
}

bool mel_visual_test_init_opt(Mel_Visual_Test_Ctx* ctx, Mel_Visual_Test_Init_Opt opt)
{
    assert(ctx != nullptr);

    u32 w = opt.width > 0 ? opt.width : 64;
    u32 h = opt.height > 0 ? opt.height : 64;

    *ctx = (Mel_Visual_Test_Ctx){ .width = w, .height = h };

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("visual test: SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    ctx->window = SDL_CreateWindow("visual_test", (int)w, (int)h,
        SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (!ctx->window)
    {
        SDL_Log("visual test: SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    if (!mel_gpu_device_init(&ctx->dev,
        .enable_validation = true,
        .app_name = S8("visual-test")))
    {
        SDL_Log("visual test: failed to init GPU device");
        return false;
    }

    if (!mel_swapchain_image_init(&ctx->sc, &ctx->dev,
        .width = w,
        .height = h,
        .format = MEL_GPU_FORMAT_R8G8B8A8_UNORM,
        .on_present = on_present,
        .user_data = ctx))
    {
        SDL_Log("visual test: failed to init image swapchain");
        return false;
    }

    mel_render_target_init_swapchain(&ctx->target, &ctx->sc, &ctx->dev, S8("visual_test"));

    return true;
}

void mel_visual_test_shutdown(Mel_Visual_Test_Ctx* ctx)
{
    assert(ctx != nullptr);

    vkDeviceWaitIdle(mel__gpu_device_vk(&ctx->dev)->device);

    free(ctx->captured_pixels);
    ctx->captured_pixels = nullptr;

    mel_render_target_shutdown(&ctx->target);
    mel_swapchain_shutdown(&ctx->sc, &ctx->dev);
    mel_gpu_device_shutdown(&ctx->dev);

    if (ctx->window)
        SDL_DestroyWindow(ctx->window);

    SDL_Quit();
}

static Mel_Visual_Test_Result compare_pixels(const u8* actual, const u8* reference,
                                              u32 width, u32 height, u32 stride,
                                              u8* diff_out)
{
    u32 total = width * height;
    u32 diff_count = 0;

    for (u32 y = 0; y < height; y++)
    {
        for (u32 x = 0; x < width; x++)
        {
            u32 offset = y * stride + x * 4;
            bool pixel_differs = false;

            for (u32 c = 0; c < 4; c++)
            {
                i32 d = (i32)actual[offset + c] - (i32)reference[offset + c];
                if (d < 0) d = -d;

                if (d > MEL_VISUAL_TEST_CHANNEL_THRESHOLD)
                    pixel_differs = true;

                if (diff_out)
                {
                    u8 v = (u8)(d > 0 ? (d < 255 ? d * 4 : 255) : 0);
                    diff_out[y * width * 4 + x * 4 + c] = (c < 3) ? v : 255;
                }
            }

            if (pixel_differs)
                diff_count++;
        }
    }

    f32 pct = total > 0 ? (f32)diff_count / (f32)total * 100.0f : 0.0f;

    return (Mel_Visual_Test_Result){
        .passed = pct <= MEL_VISUAL_TEST_FAIL_PERCENT,
        .diff_pixel_count = diff_count,
        .total_pixels = total,
        .diff_percent = pct,
    };
}

Mel_Visual_Test_Result mel_visual_test_check_opt(Mel_Visual_Test_Ctx* ctx, const char* test_name,
                                                  Mel_Visual_Test_Render_Fn render_fn, Mel_Visual_Test_Check_Opt opt)
{
    assert(ctx != nullptr);
    assert(test_name != nullptr);
    assert(render_fn != nullptr);

    ctx->has_capture = false;

    s_active_render_fn = render_fn;

    Mel_Render_Graph graph;
    mel_render_graph_init(&graph, .dev = &ctx->dev, .alloc = mel_alloc_heap());
    mel_render_graph_add_pass(&graph, S8("test"),
        .fn = visual_test_pass,
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &ctx->target, .load_op = MEL_GPU_LOAD_OP_CLEAR,
              .clear.color = { .r = opt.clear_r, .g = opt.clear_g, .b = opt.clear_b, .a = opt.clear_a } }));
    mel_render_graph_compile(&graph);

    mel_render_graph_execute(&graph);
    vkDeviceWaitIdle(mel__gpu_device_vk(&ctx->dev)->device);

    mel_render_graph_shutdown(&graph);
    s_active_render_fn = nullptr;

    if (!ctx->has_capture)
    {
        SDL_Log("visual test [%s]: no pixels captured", test_name);
        return (Mel_Visual_Test_Result){ .passed = false };
    }

    char ref_path[512];
    snprintf(ref_path, sizeof(ref_path), "tests/reference/%s.png", test_name);

    const char* update_env = getenv("MEL_UPDATE_REFERENCES");
    bool update_refs = update_env && strcmp(update_env, "1") == 0;

    int ref_w, ref_h, ref_channels;
    u8* ref_pixels = stbi_load(ref_path, &ref_w, &ref_h, &ref_channels, 4);

    if (!ref_pixels)
    {
        ensure_dir("tests/reference");
        stbi_write_png(ref_path, (int)ctx->width, (int)ctx->height, 4,
                       ctx->captured_pixels, (int)ctx->captured_stride);
        SDL_Log("visual test [%s]: no reference found, saved initial reference to %s", test_name, ref_path);

        return (Mel_Visual_Test_Result){
            .passed = true,
            .total_pixels = ctx->width * ctx->height,
        };
    }

    if (update_refs)
    {
        stbi_write_png(ref_path, (int)ctx->width, (int)ctx->height, 4,
                       ctx->captured_pixels, (int)ctx->captured_stride);
        stbi_image_free(ref_pixels);
        SDL_Log("visual test [%s]: reference updated", test_name);

        return (Mel_Visual_Test_Result){
            .passed = true,
            .total_pixels = ctx->width * ctx->height,
        };
    }

    if ((u32)ref_w != ctx->width || (u32)ref_h != ctx->height)
    {
        SDL_Log("visual test [%s]: reference size mismatch (%dx%d vs %ux%u)",
                test_name, ref_w, ref_h, ctx->width, ctx->height);
        stbi_image_free(ref_pixels);
        return (Mel_Visual_Test_Result){ .passed = false, .total_pixels = ctx->width * ctx->height };
    }

    u32 diff_size = ctx->width * ctx->height * 4;
    u8* diff_pixels = malloc(diff_size);

    Mel_Visual_Test_Result result = compare_pixels(
        ctx->captured_pixels, ref_pixels,
        ctx->width, ctx->height, ctx->captured_stride,
        diff_pixels);

    if (!result.passed)
    {
        ensure_dir("build/test_visual");

        char actual_path[512];
        snprintf(actual_path, sizeof(actual_path), "build/test_visual/%s_actual.png", test_name);
        stbi_write_png(actual_path, (int)ctx->width, (int)ctx->height, 4,
                       ctx->captured_pixels, (int)ctx->captured_stride);

        char diff_path[512];
        snprintf(diff_path, sizeof(diff_path), "build/test_visual/%s_diff.png", test_name);
        stbi_write_png(diff_path, (int)ctx->width, (int)ctx->height, 4,
                       diff_pixels, (int)(ctx->width * 4));

        SDL_Log("visual test [%s]: FAILED (%.2f%% pixels differ, %u/%u)",
                test_name, result.diff_percent, result.diff_pixel_count, result.total_pixels);
        SDL_Log("  actual: %s", actual_path);
        SDL_Log("  diff:   %s", diff_path);
    }

    free(diff_pixels);
    stbi_image_free(ref_pixels);

    return result;
}
