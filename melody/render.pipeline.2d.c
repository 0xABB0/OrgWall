#include "render.pipeline.2d.h"
#include "render.pipeline.h"
#include "render.viewport.h"
#include "render.manager.2d.h"
#include "render.sprite2d.shader.h"
#include "event.channel.h"
#include "boot.registry.h"
#include "gpu.device.h"
#include "gpu.shader.h"
#include "gpu.pipeline.h"
#include "gpu.cmd.h"
#include "gpu.buffer.h"
#include "gpu.descriptor.h"
#include "collection.bitset.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include "math.mat4.h"
#include "async.job.h"
#include "async.signal.h"

#include <stdio.h>
#include <assert.h>

typedef struct {
    Mel_Render_Manager_2D* mgr;
} Pipeline_2D_Data;

static Mel_Gpu_Device* s_dev;
static Mel_Gpu_Shader s_shader;
static Mel_Gpu_Pipeline s_gpu_pipeline;
static bool s_ready;

Mel_Event_Channel mel_pipeline_2d_ready;

static str8 mel__read_shader_file(const char* path, const Mel_Alloc* alloc)
{
    FILE* f = fopen(path, "rb");
    assert(f != nullptr);
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8* data = mel_alloc(alloc, (usize)len);
    size_t read = fread(data, 1, (usize)len, f);
    assert((long)read == len);
    fclose(f);
    return (str8){ .data = data, .len = (size)len };
}

static void pipeline_2d_init(Mel_Render_Pipeline* self, Mel_Render_View* view)
{
    (void)view;
    (void)self;
}

static void pipeline_2d_draw(Mel_Render_Pipeline* self, Mel_Render_Manager* mgr)
{
    (void)mgr;

    assert(self != nullptr);

    if (!s_ready)
        return;

    Pipeline_2D_Data* data = mel_pipeline_instance(self);
    assert(data != nullptr);

    if (data->mgr == nullptr)
        return;

    Mel_Render_Manager_2D* mgr2d = data->mgr;
    Mel_Render_View* view = self->view;
    assert(view != nullptr);

    mel_mgr_2d_upload_dirty(mgr2d);

    u32 sprite_count = mel_mgr_2d_count(mgr2d);
    if (sprite_count == 0)
        return;

    Mel_Render_Target* target = view->target;
    assert(target != nullptr);

    u32 w = mel_render_target_width(target);
    u32 h = mel_render_target_height(target);

    Mel_Gpu_Cmd cmd = {0};
    cmd.dev = s_dev;
    cmd._cmd = nullptr;

    Mel_Gpu_Color_Attachment color_att = {
        ._image_view = mel_render_target_view(target),
        .layout = MEL_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .load_op = MEL_GPU_LOAD_OP_LOAD,
        .store_op = MEL_GPU_STORE_OP_STORE,
    };

    mel_gpu_cmd_begin_rendering(&cmd,
        .color_attachments = &color_att,
        .color_count = 1,
        .render_width = w,
        .render_height = h);

    mel_gpu_cmd_set_viewport(&cmd, 0, (f32)h, (f32)w, -(f32)h, 0.0f, 1.0f);
    mel_gpu_cmd_set_scissor(&cmd, 0, 0, w, h);

    mel_gpu_cmd_bind_pipeline(&cmd, &s_gpu_pipeline);

    Mel_Gpu_Buffer* transform_buf = mel_mgr_2d_transform_buffer(mgr2d);
    Mel_Gpu_Buffer* sprite_info_buf = mel_mgr_2d_sprite_info_buffer(mgr2d);

    void* desc = mel_gpu_pipeline_alloc_descriptor(&s_gpu_pipeline, s_dev);
    mel_gpu_pipeline_write_buffer_binding(&s_gpu_pipeline, s_dev, desc,
        0, transform_buf, 0, transform_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
    mel_gpu_pipeline_write_buffer_binding(&s_gpu_pipeline, s_dev, desc,
        1, sprite_info_buf, 0, sprite_info_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);

    mel_gpu_cmd_bind_descriptor_set(&cmd, &s_gpu_pipeline, desc);

    Mel_Sprite2D_Push_Constants pc = {
        .projection = view->camera.projection,
    };
    mel_gpu_cmd_push_constants(&cmd, &s_gpu_pipeline,
        MEL_GPU_SHADER_STAGE_VERTEX, 0, sizeof(pc), &pc);

    mel_gpu_cmd_draw(&cmd, 6, sprite_count, 0, 0);

    mel_gpu_cmd_end_rendering(&cmd);
}

static void pipeline_2d_shutdown(Mel_Render_Pipeline* self)
{
    (void)self;
}

static const Mel_Render_Pipeline_Type s_pipeline_2d_type = {
    .name = { .data = (u8*)"default_2d", .len = 10 },
    .init = pipeline_2d_init,
    .draw = pipeline_2d_draw,
    .shutdown = pipeline_2d_shutdown,
    .instance_size = sizeof(Pipeline_2D_Data),
};

static void mel__pipeline_2d_compile(void* data)
{
    Mel_Counter* counter = data;
    (void)counter;

    mel_job_move_to_worker(0);

    const Mel_Alloc* alloc = mel_alloc_heap();
    str8 source = mel__read_shader_file("shaders/sprite_2d.slang", alloc);

    mel_gpu_shader_init(&s_shader, s_dev, .source = source);

    Mel_Gpu_Descriptor_Binding bindings[] = {
        {
            .binding = 0,
            .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER,
            .count = 1,
            .stages = MEL_GPU_SHADER_STAGE_VERTEX,
        },
        {
            .binding = 1,
            .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER,
            .count = 1,
            .stages = MEL_GPU_SHADER_STAGE_VERTEX,
        },
    };

    mel_gpu_pipeline_init(&s_gpu_pipeline, s_dev,
        .shader = &s_shader,
        .color_format = MEL_GPU_FORMAT_B8G8R8A8_SRGB,
        .blend_mode = MEL_GPU_BLEND_ALPHA,
        .cull_mode = MEL_GPU_CULL_NONE,
        .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
        .depth_test = false,
        .depth_write = false,
        .push_constant_size = sizeof(Mel_Sprite2D_Push_Constants),
        .push_constant_stages = MEL_GPU_SHADER_STAGE_VERTEX,
        .descriptor_bindings = bindings,
        .descriptor_binding_count = 2,
        .max_descriptor_sets = 16);

    mel_dealloc(alloc, source.data);

    mel_pipeline_register(&s_pipeline_2d_type);

    s_ready = true;
    mel_event_channel_fire(&mel_pipeline_2d_ready, nullptr);
}

static void mel__pipeline_2d_on_gpu_ready(void* ctx, const void* event)
{
    (void)ctx;
    const Mel_Gpu_Ready_Event* e = event;
    s_dev = e->dev;
    mel_job_run(e->phase_counter, mel__pipeline_2d_compile, e->phase_counter);
}

static void mel__pipeline_2d_wire(void)
{
    mel_event_channel_on(&mel_gpu_device_ready, mel__pipeline_2d_on_gpu_ready, nullptr);
}

__attribute__((constructor))
static void mel__pipeline_2d_register(void)
{
    mel_event_channel_init(&mel_pipeline_2d_ready, mel_alloc_heap());
    mel__boot_register_wire(mel__pipeline_2d_wire);
}

__attribute__((destructor))
static void mel__pipeline_2d_unregister(void)
{
    mel_event_channel_destroy(&mel_pipeline_2d_ready);
}

void mel_pipeline_2d_set_manager(Mel_Render_Pipeline* pipeline, Mel_Render_Manager_2D* mgr2d)
{
    assert(pipeline != nullptr);
    assert(pipeline->type == &s_pipeline_2d_type);
    Pipeline_2D_Data* data = mel_pipeline_instance(pipeline);
    data->mgr = mgr2d;
}
