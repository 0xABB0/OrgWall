#include "../melody/core.app.h"
#include "../melody/core.engine.h"
#include "../melody/window.h"
#include "../melody/swapchain.h"
#include "../melody/gpu.device.h"
#include "../melody/gpu.swapchain.h"
#include "../melody/gpu.shader.h"
#include "../melody/gpu.pipeline.h"
#include "../melody/gpu.cmd.h"
#include "../melody/render.viewport.h"
#include "../melody/render.target.h"
#include "../melody/render.pipeline.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/math.mat4.h"
#include "../melody/string.str8.h"

#include <assert.h>

#define WIN_W 800
#define WIN_H 600

static Mel_Window_Handle    s_window;
static Mel_Swapchain_Handle s_swapchain;
static Mel_Render_Target_Handle s_target;
static Mel_Render_View*     s_view;
static Mel_Gpu_Shader       s_shader;
static Mel_Gpu_Pipeline     s_gpu_pipeline;
static Mel_Gpu_Device*      s_dev;

typedef struct {
    Mel_Mat4 projection;
} Hello_Triangle_Push_Constants;

static void triangle_init(Mel_Render_Pipeline* self, Mel_Render_View* view)
{
    (void)self;
    (void)view;
}

static void triangle_draw(Mel_Render_Pipeline* self, void* mgr, Mel_Render_Draw_Ctx* ctx)
{
    (void)mgr;

    Mel_Gpu_Color_Attachment color_att = {
        ._image_view = mel_render_target_image_view(ctx->target),
        .layout      = MEL_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .load_op     = MEL_GPU_LOAD_OP_CLEAR,
        .store_op    = MEL_GPU_STORE_OP_STORE,
        .clear_r     = 0.05f,
        .clear_g     = 0.05f,
        .clear_b     = 0.15f,
        .clear_a     = 1.0f,
    };

    mel_gpu_cmd_begin_rendering(ctx->cmd,
        .color_attachments = &color_att,
        .color_count       = 1,
        .render_width      = ctx->target_width,
        .render_height     = ctx->target_height);

    mel_gpu_cmd_set_viewport(ctx->cmd,
        0.0f,
        (f32)ctx->target_height,
        (f32)ctx->target_width,
        -(f32)ctx->target_height,
        0.0f,
        1.0f);

    mel_gpu_cmd_set_scissor(ctx->cmd, 0, 0, ctx->target_width, ctx->target_height);

    mel_gpu_cmd_bind_pipeline(ctx->cmd, &s_gpu_pipeline);

    Hello_Triangle_Push_Constants pc = { .projection = self->view->camera.projection };
    mel_gpu_cmd_push_constants(ctx->cmd, &s_gpu_pipeline,
        MEL_GPU_SHADER_STAGE_VERTEX, 0, sizeof(pc), &pc);

    mel_gpu_cmd_draw(ctx->cmd, 3, 1, 0, 0);

    mel_gpu_cmd_end_rendering(ctx->cmd);
}

static void triangle_shutdown(Mel_Render_Pipeline* self)
{
    (void)self;
    mel_gpu_pipeline_shutdown(&s_gpu_pipeline, s_dev);
    mel_gpu_shader_shutdown(&s_shader, s_dev);
}

static const Mel_Render_Pipeline_Type s_hello_triangle_type = {
    .name          = { .data = (u8*)"hello_triangle", .len = 14 },
    .init          = triangle_init,
    .draw          = triangle_draw,
    .shutdown      = triangle_shutdown,
    .instance_size = 0,
};

void app_init(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    s_dev = mel_gpu_dev();

    s_window    = mel_window_create(S8("Hello Triangle"), .width = WIN_W, .height = WIN_H);
    s_swapchain = mel_gpu_swapchain_create_for_window(s_dev, s_window);
    s_target    = mel_render_target_from_swapchain(s_swapchain);

    mel_gpu_shader_load(&s_shader, .path = S8("shaders/hello_triangle.slang"), .dev = s_dev, .alloc = alloc);

    mel_gpu_pipeline_init(&s_gpu_pipeline, s_dev,
        .shader               = &s_shader,
        .color_format         = MEL_GPU_FORMAT_B8G8R8A8_SRGB,
        .blend_mode           = MEL_GPU_BLEND_NONE,
        .cull_mode            = MEL_GPU_CULL_NONE,
        .topology             = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
        .depth_test           = false,
        .depth_write          = false,
        .push_constant_size   = sizeof(Hello_Triangle_Push_Constants),
        .push_constant_stages = MEL_GPU_SHADER_STAGE_VERTEX);

    mel_pipeline_register(&s_hello_triangle_type);

    Mel_Render_Camera camera = {
        .view       = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
        .viewport   = {{ 0, 0, 1, 1 }},
        .visibility_mask = 0xFFFFFFFF,
        .lod_bias   = 1.0f,
        .view_count  = 1,
    };

    s_view = mel_render_view_create(
        .source   = nullptr,
        .camera   = camera,
        .target   = s_target,
        .pipeline = S8("hello_triangle"),
        .dev      = s_dev,
        .alloc    = alloc);
}

void app_shutdown(void)
{
    mel_gpu_device_wait_idle(s_dev);
    mel_render_view_destroy(s_view);
    mel_render_target_destroy(s_target);
}
