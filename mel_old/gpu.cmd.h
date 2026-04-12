#pragma once

#include "gpu.types.h"
#include "gpu.device.fwd.h"
#include "gpu.image.fwd.h"
#include "gpu.buffer.fwd.h"
#include "gpu.pipeline.fwd.h"

typedef struct Mel_Gpu_Cmd Mel_Gpu_Cmd;

struct Mel_Gpu_Cmd {
    void* _cmd;
    Mel_Gpu_Device* dev;
};

void mel_gpu_cmd_begin(Mel_Gpu_Cmd* c);
void mel_gpu_cmd_end(Mel_Gpu_Cmd* c);

typedef struct {
    void* _image_view;
    Mel_Gpu_Image_Layout layout;
    Mel_Gpu_Load_Op load_op;
    Mel_Gpu_Store_Op store_op;
    f32 clear_r;
    f32 clear_g;
    f32 clear_b;
    f32 clear_a;
} Mel_Gpu_Color_Attachment;

typedef struct {
    void* _image_view;
    Mel_Gpu_Image_Layout layout;
    Mel_Gpu_Load_Op load_op;
    Mel_Gpu_Store_Op store_op;
    f32 clear_depth;
    u32 clear_stencil;
} Mel_Gpu_Depth_Attachment;

typedef struct {
    Mel_Gpu_Color_Attachment* color_attachments;
    u32 color_count;
    Mel_Gpu_Depth_Attachment* depth_attachment;
    u32 render_width;
    u32 render_height;
} Mel_Gpu_Rendering_Opt;

void mel_gpu_cmd_begin_rendering_opt(Mel_Gpu_Cmd* c, Mel_Gpu_Rendering_Opt opt);
#define mel_gpu_cmd_begin_rendering(c, ...) mel_gpu_cmd_begin_rendering_opt((c), (Mel_Gpu_Rendering_Opt){__VA_ARGS__})

void mel_gpu_cmd_end_rendering(Mel_Gpu_Cmd* c);

void mel_gpu_cmd_bind_pipeline(Mel_Gpu_Cmd* c, Mel_Gpu_Pipeline* pipeline);
void mel_gpu_cmd_bind_vertex_buffer(Mel_Gpu_Cmd* c, Mel_Gpu_Buffer* buffer, u64 offset);
void mel_gpu_cmd_bind_index_buffer(Mel_Gpu_Cmd* c, Mel_Gpu_Buffer* buffer, u64 offset, Mel_Gpu_Index_Type type);
void mel_gpu_cmd_bind_descriptor_set(Mel_Gpu_Cmd* c, Mel_Gpu_Pipeline* pipeline, void* descriptor_set);

void mel_gpu_cmd_push_constants(Mel_Gpu_Cmd* c, Mel_Gpu_Pipeline* pipeline,
                                Mel_Gpu_Shader_Stage stages, u32 offset, u32 size, const void* data);

void mel_gpu_cmd_set_viewport(Mel_Gpu_Cmd* c, f32 x, f32 y, f32 w, f32 h, f32 min_depth, f32 max_depth);
void mel_gpu_cmd_set_scissor(Mel_Gpu_Cmd* c, i32 x, i32 y, u32 w, u32 h);
void mel_gpu_cmd_set_cull_mode(Mel_Gpu_Cmd* c, u32 cull_mode);

void mel_gpu_cmd_draw(Mel_Gpu_Cmd* c, u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance);
void mel_gpu_cmd_draw_indexed(Mel_Gpu_Cmd* c, u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset, u32 first_instance);
void mel_gpu_cmd_draw_indexed_indirect(Mel_Gpu_Cmd* c, Mel_Gpu_Buffer* buffer, u64 offset, u32 draw_count, u32 stride);
void mel_gpu_cmd_dispatch(Mel_Gpu_Cmd* c, u32 group_count_x, u32 group_count_y, u32 group_count_z);
void mel_gpu_cmd_draw_mesh_tasks(Mel_Gpu_Cmd* c, u32 group_count_x, u32 group_count_y, u32 group_count_z);
void mel_gpu_cmd_draw_mesh_tasks_indirect(Mel_Gpu_Cmd* c, Mel_Gpu_Buffer* buffer, u64 offset, u32 draw_count, u32 stride);

void mel_gpu_cmd_image_barrier(Mel_Gpu_Cmd* c,
                               void* image,
                               Mel_Gpu_Stage src_stage, Mel_Gpu_Access src_access,
                               Mel_Gpu_Stage dst_stage, Mel_Gpu_Access dst_access,
                               Mel_Gpu_Image_Layout old_layout, Mel_Gpu_Image_Layout new_layout,
                               Mel_Gpu_Aspect aspect);

void mel_gpu_cmd_buffer_barrier(Mel_Gpu_Cmd* c,
                                Mel_Gpu_Buffer* buffer,
                                Mel_Gpu_Stage src_stage, Mel_Gpu_Access src_access,
                                Mel_Gpu_Stage dst_stage, Mel_Gpu_Access dst_access);

void mel_gpu_cmd_transition_image(Mel_Gpu_Cmd* c, Mel_Gpu_Image* image, Mel_Gpu_Image_Layout new_layout);
