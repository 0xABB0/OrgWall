#include <SDL3/SDL.h>
#include <stb_image_write.h>
#include <math.h>
#include <stdio.h>

#include "gpu.device.h"
#include "swapchain.image.h"
#include "swapchain.h"
#include "render.graph.h"
#include "render.target.h"
#include "render.pass.h"
#include "string.str8.h"
#include "allocator.heap.h"
#include "log.h"

#define WIDTH 1280
#define HEIGHT 720
#define TOTAL_FRAMES 180

typedef struct {
    FILE* ffmpeg_pipe;
    bool screenshot_saved;
    u32 frame_number;
} Demo_Ctx;

static void on_present(void* pixels, u32 width, u32 height, u32 stride, void* user_data)
{
    Demo_Ctx* ctx = user_data;

    if (!ctx->screenshot_saved)
    {
        stbi_write_png("build/screenshot.png", (int)width, (int)height, 4, pixels, (int)stride);
        mel_log_info("headless", "Screenshot saved: build/screenshot.png (%ux%u)", width, height);
        ctx->screenshot_saved = true;
    }

    if (ctx->ffmpeg_pipe)
        fwrite(pixels, 1, (size_t)stride * height, ctx->ffmpeg_pipe);

    ctx->frame_number++;
}

static void headless_pass(Mel_Render_Pass_Ctx* ctx)
{
    (void)ctx;
}

int main(int argc, char* argv[])
{
    (void)argc; (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        mel_log_fatal("headless", "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("headless", WIDTH, HEIGHT,
        SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (!window)
    {
        mel_log_fatal("headless", "SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }

    Mel_Gpu_Device dev = {0};
    if (!mel_gpu_device_init(&dev,
        .enable_validation = true,
        .app_name = S8("headless-demo")))
    {
        mel_log_fatal("headless", "Failed to init GPU device");
        return 1;
    }

    Demo_Ctx ctx = {0};

    char ffmpeg_cmd[512];
    snprintf(ffmpeg_cmd, sizeof(ffmpeg_cmd),
        "ffmpeg -y -f rawvideo -pixel_format rgba -video_size %dx%d "
        "-framerate 60 -i - -c:v libx264 -pix_fmt yuv420p build/output.mp4 2>/dev/null",
        WIDTH, HEIGHT);

    ctx.ffmpeg_pipe = popen(ffmpeg_cmd, "w");
    if (!ctx.ffmpeg_pipe)
        mel_log_warn("headless", "ffmpeg not found, video will not be saved");

    Mel_Swapchain sc = {0};
    if (!mel_swapchain_image_init(&sc, &dev,
        .width = WIDTH,
        .height = HEIGHT,
        .format = MEL_GPU_FORMAT_R8G8B8A8_SRGB,
        .on_present = on_present,
        .user_data = &ctx))
    {
        mel_log_fatal("headless", "Failed to init image swapchain");
        return 1;
    }

    Mel_Render_Target target;
    mel_render_target_init_swapchain(&target, &sc, &dev, S8("headless"));

    Mel_Render_Graph graph;
    mel_render_graph_init(&graph, .dev = &dev, .alloc = mel_alloc_heap());
    u32 pass_id = mel_render_graph_add_pass(&graph, S8("clear"),
        .fn = headless_pass,
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &target, .load_op = MEL_GPU_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f } }));
    mel_render_graph_compile(&graph);

    mel_log_info("headless", "Rendering %d frames at %dx%d...", TOTAL_FRAMES, WIDTH, HEIGHT);

    for (u32 i = 0; i < TOTAL_FRAMES; i++)
    {
        f32 t = (f32)i / (f32)TOTAL_FRAMES;
        f32 r = 0.5f + 0.5f * sinf(t * 6.28318f);
        f32 g = 0.5f + 0.5f * sinf(t * 6.28318f + 2.09440f);
        f32 b = 0.5f + 0.5f * sinf(t * 6.28318f + 4.18879f);

        Mel_Pass_Write_Target* wt = &graph.passes.items[pass_id].write_targets[0];
        wt->clear.color.r = r;
        wt->clear.color.g = g;
        wt->clear.color.b = b;
        wt->clear.color.a = 1.0f;

        mel_render_graph_execute(&graph);

        if ((i + 1) % 60 == 0)
            mel_log_info("headless", "  %u/%u frames", i + 1, TOTAL_FRAMES);
    }

    mel_gpu_device_wait_idle(&dev);

    if (ctx.ffmpeg_pipe)
    {
        pclose(ctx.ffmpeg_pipe);
        mel_log_info("headless", "Video saved: build/output.mp4");
    }

    mel_log_info("headless", "Done! Screenshot: build/screenshot.png, Video: build/output.mp4");

    mel_render_graph_shutdown(&graph);
    mel_render_target_shutdown(&target);
    mel_swapchain_shutdown(&sc, &dev);
    mel_gpu_device_shutdown(&dev);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
