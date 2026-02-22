#include <SDL3/SDL.h>
#include <stb_image_write.h>
#include <math.h>
#include <stdio.h>

#include "gpu.device.h"
#include "swapchain.image.h"
#include "swapchain.h"
#include "render.frame.h"
#include "gpu.cmd.h"
#include "debug.backtrace.h"
#include "string.str8.h"

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
        SDL_Log("Screenshot saved: build/screenshot.png (%ux%u)", width, height);
        ctx->screenshot_saved = true;
    }

    if (ctx->ffmpeg_pipe)
        fwrite(pixels, 1, (size_t)stride * height, ctx->ffmpeg_pipe);

    ctx->frame_number++;
}

int main(int argc, char* argv[])
{
    (void)argc; (void)argv;

    mel_backtrace_init();

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("headless", WIDTH, HEIGHT,
        SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (!window)
    {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }

    Mel_Gpu_Device dev = {0};
    if (!mel_gpu_device_init(&dev,
        .window = window,
        .enable_validation = true,
        .app_name = S8("headless-demo")))
    {
        SDL_Log("Failed to init GPU device");
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
        SDL_Log("Warning: ffmpeg not found, video will not be saved");

    Mel_Swapchain sc = {0};
    if (!mel_swapchain_image_init(&sc, &dev,
        .width = WIDTH,
        .height = HEIGHT,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .on_present = on_present,
        .user_data = &ctx))
    {
        SDL_Log("Failed to init image swapchain");
        return 1;
    }

    Mel_Render_Frame frame = {0};
    if (!mel_render_frame_init(&frame, .dev = &dev, .swapchain = &sc))
    {
        SDL_Log("Failed to init render frame");
        return 1;
    }

    SDL_Log("Rendering %d frames at %dx%d...", TOTAL_FRAMES, WIDTH, HEIGHT);

    for (u32 i = 0; i < TOTAL_FRAMES; i++)
    {
        if (!mel_render_frame_begin(&frame))
        {
            SDL_Log("Frame begin failed at frame %u", i);
            continue;
        }

        Mel_Gpu_Cmd c = {
            .cmd = mel_render_frame_cmd(&frame),
            .dev = &dev,
        };

        mel_gpu_cmd_image_barrier(&c,
            sc.images[sc.current_image],
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);

        f32 t = (f32)i / (f32)TOTAL_FRAMES;
        f32 r = 0.5f + 0.5f * sinf(t * 6.28318f);
        f32 g = 0.5f + 0.5f * sinf(t * 6.28318f + 2.09440f);
        f32 b = 0.5f + 0.5f * sinf(t * 6.28318f + 4.18879f);

        Mel_Gpu_Color_Attachment color_att = {
            .image_view = sc.image_views[sc.current_image],
            .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .clear_r = r,
            .clear_g = g,
            .clear_b = b,
            .clear_a = 1.0f,
        };

        mel_gpu_cmd_begin_rendering(&c,
            .color_attachments = &color_att,
            .color_count = 1,
            .render_width = sc.extent.width,
            .render_height = sc.extent.height);

        mel_gpu_cmd_end_rendering(&c);

        mel_render_frame_end(&frame);

        if ((i + 1) % 60 == 0)
            SDL_Log("  %u/%u frames", i + 1, TOTAL_FRAMES);
    }

    vkDeviceWaitIdle(dev.device);

    if (ctx.ffmpeg_pipe)
    {
        pclose(ctx.ffmpeg_pipe);
        SDL_Log("Video saved: build/output.mp4");
    }

    SDL_Log("Done! Screenshot: build/screenshot.png, Video: build/output.mp4");

    mel_render_frame_shutdown(&frame);
    mel_swapchain_shutdown(&sc, &dev);
    mel_gpu_device_shutdown(&dev);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
