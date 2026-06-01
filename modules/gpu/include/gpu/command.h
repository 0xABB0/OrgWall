#pragma once

#include <core/types.h>
#include <gpu/format.h>
#include <gpu/buffer.h>
#include <gpu/pipeline.h>

typedef struct Mel_Gpu_Device       Mel_Gpu_Device;
typedef struct Mel_Gpu_Swapchain    Mel_Gpu_Swapchain;
typedef struct Mel_Gpu_Command_List Mel_Gpu_Command_List;

void                  mel_gpu_frame_begin(Mel_Gpu_Swapchain* sc);
Mel_Gpu_Command_List* mel_gpu_frame_commands(Mel_Gpu_Swapchain* sc);
void                  mel_gpu_frame_end(Mel_Gpu_Swapchain* sc);

void mel_gpu_cmd_begin_pass(Mel_Gpu_Command_List* cmd, Mel_Gpu_Color clear);
void mel_gpu_cmd_end_pass(Mel_Gpu_Command_List* cmd);
void mel_gpu_cmd_bind_pipeline(Mel_Gpu_Command_List* cmd, Mel_Gpu_Pipeline pipe);
void mel_gpu_cmd_bind_vertex_buffer(Mel_Gpu_Command_List* cmd, u32 slot, Mel_Gpu_Buffer buf);
void mel_gpu_cmd_bind_index_buffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer buf);
void mel_gpu_cmd_push_constants(Mel_Gpu_Command_List* cmd, u32 offset, u32 size, const void* data);
void mel_gpu_cmd_draw(Mel_Gpu_Command_List* cmd, u32 vertex_count, u32 instance_count);
void mel_gpu_cmd_draw_indexed(Mel_Gpu_Command_List* cmd, u32 index_count, u32 instance_count);
