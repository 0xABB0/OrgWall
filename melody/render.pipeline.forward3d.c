#include "render.pipeline.forward3d.h"
#include "render.pipeline.h"
#include "render.viewport.h"
#include "render.manager.h"
#include "render.types.3d.h"
#include "render.material_base.h"
#include "render.target.h"
#include "gpu.geometry_pool.h"
#include "event.channel.h"
#include "boot.registry.h"
#include "core.engine.h"
#include "gpu.device.h"
#include "gpu.shader.h"
#include "gpu.pipeline.h"
#include "gpu.cmd.h"
#include "gpu.buffer.h"
#include "gpu.image.h"
#include "gpu.descriptor.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include "math.mat4.h"
#include "async.job.h"

#include <assert.h>

typedef struct {
    Mel_Mat4 mvp;
    u32 vertex_offset;
    u32 index_offset;
    u32 material_idx;
    u32 _pad;
} Forward3D_Push;

_Static_assert(sizeof(Forward3D_Push) == 80, "Forward3D_Push must be 80 bytes (within 128 push constant limit)");

typedef struct {
    Mel_Gpu_Image depth_image;
    u32 last_width;
    u32 last_height;
} Pipeline_Forward3D_Data;

static Mel_Gpu_Device* s_dev;
static Mel_Gpu_Shader s_shader;
static Mel_Gpu_Pipeline s_gpu_pipeline;
static Mel_Geometry_Pool* s_geometry_pool;
static bool s_ready;

Mel_Event_Channel mel_pipeline_forward3d_ready;

static void ensure_depth(Pipeline_Forward3D_Data* d, u32 w, u32 h)
{
    if (d->last_width == w && d->last_height == h && d->depth_image._handle != nullptr)
        return;

    if (d->depth_image._handle != nullptr)
        mel_gpu_image_shutdown(&d->depth_image, s_dev);

    mel_gpu_image_init(&d->depth_image, s_dev,
        .width = w, .height = h,
        .format = MEL_GPU_FORMAT_D32_SFLOAT,
        .usage = MEL_GPU_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT,
        .aspect = MEL_GPU_ASPECT_DEPTH,
        .mip_levels = 1, .layer_count = 1);

    d->last_width = w;
    d->last_height = h;
}

static void forward3d_init(Mel_Render_Pipeline* self, Mel_Render_View* view)
{
    (void)self;
    (void)view;
}

static void forward3d_draw(Mel_Render_Pipeline* self, void* mgr, Mel_Render_Draw_Ctx* ctx)
{
    assert(self != nullptr);
    assert(ctx != nullptr && ctx->cmd != nullptr);

    if (!s_ready || s_geometry_pool == nullptr)
        return;

    Mel_Render_Manager* m = mgr;
    if (m == nullptr)
        return;

    mel_mgr_upload_dirty(m);

    u32 count = mel_mgr_count(m);
    if (count == 0)
        return;

    Mel_Render_View* view = self->view;
    assert(view != nullptr);

    Pipeline_Forward3D_Data* data = mel_pipeline_instance(self);
    ensure_depth(data, ctx->target_width, ctx->target_height);

    mel_gpu_cmd_transition_image(ctx->cmd, &data->depth_image, MEL_GPU_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT);

    Mel_Gpu_Color_Attachment color_att = {
        ._image_view = mel_render_target_image_view(ctx->target),
        .layout = MEL_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .load_op = MEL_GPU_LOAD_OP_CLEAR,
        .store_op = MEL_GPU_STORE_OP_STORE,
        .clear_r = 0.06f, .clear_g = 0.07f, .clear_b = 0.10f, .clear_a = 1.0f,
    };

    Mel_Gpu_Depth_Attachment depth_att = {
        ._image_view = data->depth_image._view,
        .layout = MEL_GPU_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT,
        .load_op = MEL_GPU_LOAD_OP_CLEAR,
        .store_op = MEL_GPU_STORE_OP_STORE,
        .clear_depth = 1.0f,
    };

    mel_gpu_cmd_begin_rendering(ctx->cmd,
        .color_attachments = &color_att, .color_count = 1,
        .depth_attachment = &depth_att,
        .render_width = ctx->target_width, .render_height = ctx->target_height);

    mel_gpu_cmd_set_viewport(ctx->cmd,
        0, (f32)ctx->target_height, (f32)ctx->target_width, -(f32)ctx->target_height, 0, 1);
    mel_gpu_cmd_set_scissor(ctx->cmd, 0, 0, ctx->target_width, ctx->target_height);

    mel_gpu_cmd_bind_pipeline(ctx->cmd, &s_gpu_pipeline);

    Mel_Gpu_Buffer* vert_buf = mel_geometry_pool_vertex_buffer(s_geometry_pool);
    Mel_Gpu_Buffer* idx_buf = mel_geometry_pool_index_buffer_u32(s_geometry_pool);

    if (vert_buf == nullptr || idx_buf == nullptr)
    {
        mel_gpu_cmd_end_rendering(ctx->cmd);
        return;
    }

    void* desc = mel_gpu_pipeline_alloc_descriptor(&s_gpu_pipeline, s_dev);
    assert(desc != nullptr);

    mel_gpu_pipeline_write_buffer_binding(&s_gpu_pipeline, s_dev, desc, 0,
        vert_buf, 0, vert_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
    mel_gpu_pipeline_write_buffer_binding(&s_gpu_pipeline, s_dev, desc, 1,
        idx_buf, 0, idx_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);

    u32 base_count = mel_material_base_count();
    for (u32 bi = 0; bi < base_count; bi++)
    {
        Mel_Material_Base* base = mel_material_base_get(bi);
        if (!(base->compat & MEL_COMPAT_FORWARD))
            continue;
        if (base->instance_count == 0)
            continue;

        mel_material_base_upload_dirty(bi, s_dev);

        Mel_Gpu_Buffer* mat_buf = mel_material_base_param_buffer(bi);
        if (mat_buf == nullptr || mat_buf->_handle == nullptr)
            continue;

        mel_gpu_pipeline_write_buffer_binding(&s_gpu_pipeline, s_dev, desc, 2,
            mat_buf, 0, mat_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
    }

    mel_gpu_cmd_bind_descriptor_set(ctx->cmd, &s_gpu_pipeline, desc);

    const Mel_Render_Transform* transforms = mel_mgr_pool_data(m, MEL_3D_POOL_TRANSFORMS);
    const Mel_Render_Info* infos = mel_mgr_pool_data(m, MEL_3D_POOL_INFOS);
    u32 packed = mel_mgr_count(m);

    Mel_Mat4 vp = mel_mat4_mul(view->camera.projection, view->camera.view);

    for (u32 i = 0; i < packed; i++)
    {
        if (infos[i].flags & MEL_RF_HIDDEN)
            continue;

        Mel_Mat4 mvp = mel_mat4_mul(vp, transforms[i].model);

        Mel_Geometry_Handle geo_h = infos[i].mesh;
        Mel_Geometry_Region region = mel_geometry_pool_region(s_geometry_pool, geo_h);

        Forward3D_Push pc = {
            .mvp = mvp,
            .vertex_offset = region.vertex_offset,
            .index_offset = region.index_offset,
            .material_idx = infos[i].material_idx,
        };

        mel_gpu_cmd_push_constants(ctx->cmd, &s_gpu_pipeline,
            MEL_GPU_SHADER_STAGE_VERTEX, 0, sizeof(pc), &pc);

        mel_gpu_cmd_draw(ctx->cmd, region.index_count, 1, 0, 0);
    }

    mel_gpu_cmd_end_rendering(ctx->cmd);
}

static void forward3d_shutdown(Mel_Render_Pipeline* self)
{
    Pipeline_Forward3D_Data* d = mel_pipeline_instance(self);
    if (d->depth_image._handle != nullptr)
        mel_gpu_image_shutdown(&d->depth_image, s_dev);
}

void mel_pipeline_forward3d_set_geometry_pool(Mel_Geometry_Pool* pool)
{
    assert(pool != nullptr);
    s_geometry_pool = pool;
}

static const Mel_Render_Pipeline_Type s_forward3d_type = {
    .name = { .data = (u8*)"forward_3d", .len = 10 },
    .init = forward3d_init,
    .draw = forward3d_draw,
    .shutdown = forward3d_shutdown,
    .instance_size = sizeof(Pipeline_Forward3D_Data),
};

static void mel__forward3d_compile(void* data)
{
    (void)data;

    mel_gpu_shader_load(&s_shader, .path = S8("shaders/mesh_forward.slang"), .dev = s_dev);

    Mel_Gpu_Descriptor_Binding bindings[] = {
        { .binding = 0, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX },
        { .binding = 1, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX },
        { .binding = 2, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX },
    };

    mel_gpu_pipeline_init(&s_gpu_pipeline, s_dev,
        .shader = &s_shader,
        .color_format = MEL_GPU_FORMAT_B8G8R8A8_SRGB,
        .depth_format = MEL_GPU_FORMAT_D32_SFLOAT,
        .blend_mode = MEL_GPU_BLEND_NONE,
        .cull_mode = MEL_GPU_CULL_BACK,
        .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
        .depth_test = true,
        .depth_write = true,
        .push_constant_size = sizeof(Forward3D_Push),
        .push_constant_stages = MEL_GPU_SHADER_STAGE_VERTEX,
        .descriptor_bindings = bindings,
        .descriptor_binding_count = 3,
        .max_descriptor_sets = 16);

    mel_pipeline_register(&s_forward3d_type);
    s_ready = true;
    mel_event_channel_fire(&mel_pipeline_forward3d_ready, nullptr);
}

static void mel__forward3d_on_gpu_ready(void* ctx, const void* event)
{
    (void)ctx;
    const Mel_Gpu_Ready_Event* e = event;
    s_dev = e->dev;
    mel_job_run(e->phase_counter, mel__forward3d_compile, e->phase_counter);
}

static void mel__forward3d_on_shutdown(void* ctx, const void* event)
{
    (void)ctx;
    (void)event;

    if (s_ready)
    {
        mel_gpu_pipeline_shutdown(&s_gpu_pipeline, s_dev);
        mel_gpu_shader_shutdown(&s_shader, s_dev);
        s_ready = false;
    }
}

static void mel__forward3d_wire(void)
{
    mel_event_channel_on(&mel_gpu_device_ready, mel__forward3d_on_gpu_ready, nullptr);
    mel_event_channel_on(&mel_shutdown_begin, mel__forward3d_on_shutdown, nullptr);
}

__attribute__((constructor))
static void mel__forward3d_register(void)
{
    mel_event_channel_init(&mel_pipeline_forward3d_ready, mel_alloc_heap());
    mel__boot_register_wire(mel__forward3d_wire);
}

__attribute__((destructor))
static void mel__forward3d_unregister(void)
{
    mel_event_channel_destroy(&mel_pipeline_forward3d_ready);
}
