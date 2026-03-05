#pragma once

#include "gpu.device.h"
#include "gpu.image.fwd.h"
#include "gpu.buffer.fwd.h"
#include "gpu.pipeline.fwd.h"

typedef struct Mel_Gpu_Cmd Mel_Gpu_Cmd;

struct Mel_Gpu_Cmd {
    VkCommandBuffer cmd;
    Mel_Gpu_Device* dev;
};

void mel_gpu_cmd_begin(Mel_Gpu_Cmd* c);
void mel_gpu_cmd_end(Mel_Gpu_Cmd* c);

typedef struct {
    VkImageView image_view;
    VkImageLayout layout;
    VkAttachmentLoadOp load_op;
    VkAttachmentStoreOp store_op;
    f32 clear_r;
    f32 clear_g;
    f32 clear_b;
    f32 clear_a;
} Mel_Gpu_Color_Attachment;

typedef struct {
    VkImageView image_view;
    VkImageLayout layout;
    VkAttachmentLoadOp load_op;
    VkAttachmentStoreOp store_op;
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
void mel_gpu_cmd_bind_vertex_buffer(Mel_Gpu_Cmd* c, Mel_Gpu_Buffer* buffer, VkDeviceSize offset);
void mel_gpu_cmd_bind_index_buffer(Mel_Gpu_Cmd* c, Mel_Gpu_Buffer* buffer, VkDeviceSize offset, VkIndexType type);
void mel_gpu_cmd_bind_descriptor_set(Mel_Gpu_Cmd* c, Mel_Gpu_Pipeline* pipeline, VkDescriptorSet set);

void mel_gpu_cmd_push_constants(Mel_Gpu_Cmd* c, Mel_Gpu_Pipeline* pipeline,
                                VkShaderStageFlags stages, u32 offset, u32 size, const void* data);

void mel_gpu_cmd_set_viewport(Mel_Gpu_Cmd* c, f32 x, f32 y, f32 w, f32 h, f32 min_depth, f32 max_depth);
void mel_gpu_cmd_set_scissor(Mel_Gpu_Cmd* c, i32 x, i32 y, u32 w, u32 h);

void mel_gpu_cmd_draw(Mel_Gpu_Cmd* c, u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance);
void mel_gpu_cmd_draw_indexed(Mel_Gpu_Cmd* c, u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset, u32 first_instance);

void mel_gpu_cmd_image_barrier(Mel_Gpu_Cmd* c,
                               VkImage image,
                               VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                               VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access,
                               VkImageLayout old_layout, VkImageLayout new_layout,
                               VkImageAspectFlags aspect);

void mel_gpu_cmd_buffer_barrier(Mel_Gpu_Cmd* c,
                                VkBuffer buffer,
                                VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                                VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access);

void mel_gpu_cmd_transition_image(Mel_Gpu_Cmd* c, Mel_Gpu_Image* image, VkImageLayout new_layout);
