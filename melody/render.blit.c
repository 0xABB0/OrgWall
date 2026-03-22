#include "render.blit.h"
#include "render.target.h"
#include "render.viewport.h"
#include "gpu.device.h"
#include "gpu.shader.h"
#include "gpu.pipeline.h"
#include "gpu.cmd.h"
#include "gpu.image.h"
#include "gpu.texture.h"
#include "boot.registry.h"
#include "event.channel.h"
#include "core.engine.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include "async.job.h"

#include <assert.h>

static Mel_Gpu_Device* s_dev;
static Mel_Gpu_Shader s_blit_shader;
static Mel_Gpu_Pipeline s_blit_pipeline;
static void* s_blit_sampler;
static bool s_blit_ready;

Mel_Rect mel__compute_scaled_viewport(u32 src_w, u32 src_h,
                                       u32 dst_w, u32 dst_h,
                                       u32 scale_mode)
{
    if (scale_mode == MEL_SCALE_STRETCH || scale_mode == MEL_SCALE_NONE)
        return mel_rect(0, 0, (f32)dst_w, (f32)dst_h);

    f32 src_aspect = (f32)src_w / (f32)src_h;
    f32 dst_aspect = (f32)dst_w / (f32)dst_h;

    f32 vp_w, vp_h, vp_x, vp_y;

    if (scale_mode == MEL_SCALE_FIT)
    {
        if (dst_aspect > src_aspect)
        {
            vp_h = (f32)dst_h;
            vp_w = vp_h * src_aspect;
            vp_x = ((f32)dst_w - vp_w) * 0.5f;
            vp_y = 0;
        }
        else
        {
            vp_w = (f32)dst_w;
            vp_h = vp_w / src_aspect;
            vp_x = 0;
            vp_y = ((f32)dst_h - vp_h) * 0.5f;
        }
    }
    else
    {
        if (dst_aspect > src_aspect)
        {
            vp_w = (f32)dst_w;
            vp_h = vp_w / src_aspect;
            vp_x = 0;
            vp_y = ((f32)dst_h - vp_h) * 0.5f;
        }
        else
        {
            vp_h = (f32)dst_h;
            vp_w = vp_h * src_aspect;
            vp_x = ((f32)dst_w - vp_w) * 0.5f;
            vp_y = 0;
        }
    }

    return mel_rect(vp_x, vp_y, vp_w, vp_h);
}

void mel__blit_to_target(Mel_Gpu_Cmd* cmd, Mel_Gpu_Device* dev,
                          Mel_Render_Target* src, Mel_Render_Target* dst,
                          u32 dst_width, u32 dst_height,
                          u32 scale_mode)
{
    assert(cmd != nullptr);
    assert(src != nullptr);
    assert(dst != nullptr);

    if (!s_blit_ready)
    {
        return;
    }

    u32 src_w = mel_render_target_width(src);
    u32 src_h = mel_render_target_height(src);

    mel_gpu_cmd_transition_image(cmd, &src->offscreen_image, MEL_GPU_IMAGE_LAYOUT_SHADER_READ_ONLY);

    Mel_Gpu_Color_Attachment color_att = {
        ._image_view = mel_render_target_image_view(dst),
        .layout = MEL_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .load_op = MEL_GPU_LOAD_OP_CLEAR,
        .store_op = MEL_GPU_STORE_OP_STORE,
        .clear_r = 0, .clear_g = 0, .clear_b = 0, .clear_a = 1,
    };

    mel_gpu_cmd_begin_rendering(cmd,
        .color_attachments = &color_att, .color_count = 1,
        .render_width = dst_width, .render_height = dst_height);

    Mel_Rect vp = mel__compute_scaled_viewport(src_w, src_h, dst_width, dst_height, scale_mode);

    mel_gpu_cmd_set_viewport(cmd, vp.x, vp.y + vp.h, vp.w, -vp.h, 0, 1);
    mel_gpu_cmd_set_scissor(cmd, (i32)vp.x, (i32)vp.y, (u32)vp.w, (u32)vp.h);

    mel_gpu_cmd_bind_pipeline(cmd, &s_blit_pipeline);

    void* desc = mel_gpu_pipeline_alloc_descriptor(&s_blit_pipeline, dev);
    assert(desc != nullptr);

    assert(s_blit_sampler != nullptr);
    mel_gpu_pipeline_write_texture(&s_blit_pipeline, dev, desc,
        src->offscreen_image._view, s_blit_sampler);

    mel_gpu_cmd_bind_descriptor_set(cmd, &s_blit_pipeline, desc);

    mel_gpu_cmd_draw(cmd, 6, 1, 0, 0);

    mel_gpu_cmd_end_rendering(cmd);
}

static void mel__blit_compile(void* data)
{
    (void)data;

    mel_gpu_shader_load(&s_blit_shader, .path = S8("shaders/blit.slang"), .dev = s_dev);

    Mel_Gpu_Descriptor_Binding binding = {
        .binding = 0,
        .type = MEL_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,
        .count = 1,
        .stages = MEL_GPU_SHADER_STAGE_FRAGMENT,
    };

    mel_gpu_pipeline_init(&s_blit_pipeline, s_dev,
        .shader = &s_blit_shader,
        .color_format = MEL_GPU_FORMAT_B8G8R8A8_SRGB,
        .blend_mode = MEL_GPU_BLEND_NONE,
        .cull_mode = MEL_GPU_CULL_NONE,
        .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
        .depth_test = false,
        .depth_write = false,
        .descriptor_bindings = &binding,
        .descriptor_binding_count = 1,
        .max_descriptor_sets = 16);

    s_blit_sampler = mel_gpu_sampler_create(s_dev,
        .nearest_filter = false,
        .address_mode_u = MEL_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE,
        .address_mode_v = MEL_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE,
        .address_mode_w = MEL_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE,
        .compare_enable = false,
        .compare_op = MEL_GPU_COMPARE_ALWAYS,
        .min_lod = 0.0f,
        .max_lod = 0.0f);

    s_blit_ready = true;
}

static void mel__blit_on_gpu_ready(void* ctx, const void* event)
{
    (void)ctx;
    const Mel_Gpu_Ready_Event* e = event;
    s_dev = e->dev;
    mel_job_run(e->phase_counter, mel__blit_compile, e->phase_counter);
}

static void mel__blit_on_shutdown(void* ctx, const void* event)
{
    (void)ctx; (void)event;
    if (s_blit_ready)
    {
        if (s_blit_sampler)
            mel_gpu_sampler_destroy(s_dev, s_blit_sampler);
        mel_gpu_pipeline_shutdown(&s_blit_pipeline, s_dev);
        mel_gpu_shader_shutdown(&s_blit_shader, s_dev);
        s_blit_sampler = nullptr;
        s_blit_ready = false;
    }
}

static void mel__blit_wire(void)
{
    mel_event_channel_on(&mel_gpu_device_ready, mel__blit_on_gpu_ready, nullptr);
    mel_event_channel_on(&mel_shutdown_begin, mel__blit_on_shutdown, nullptr);
}

__attribute__((constructor))
static void mel__blit_register(void)
{
    mel__boot_register_wire(mel__blit_wire);
}
