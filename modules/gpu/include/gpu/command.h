#pragma once

#include <core/types.h>
#include <gpu/format.h>
#include <gpu/buffer.h>
#include <gpu/texture.h>
#include <gpu/state.h>
#include <gpu/pipeline.h>

typedef struct Mel_Gpu_Device       Mel_Gpu_Device;
typedef struct Mel_Gpu_Swapchain    Mel_Gpu_Swapchain;
typedef struct Mel_Gpu_Queue        Mel_Gpu_Queue;
typedef struct Mel_Gpu_Command_List Mel_Gpu_Command_List;

void                  mel_gpu_frame_begin(Mel_Gpu_Swapchain* sc);
Mel_Gpu_Command_List* mel_gpu_frame_commands(Mel_Gpu_Swapchain* sc);
void                  mel_gpu_frame_end(Mel_Gpu_Swapchain* sc);

// U15: standalone single-use command lists recorded from a per-thread per-queue-family pool.
Mel_Gpu_Command_List* mel_gpu_command_list_create(Mel_Gpu_Queue* q);
void                  mel_gpu_command_list_begin(Mel_Gpu_Command_List* cmd);
void                  mel_gpu_command_list_end(Mel_Gpu_Command_List* cmd);
void                  mel_gpu_command_list_destroy(Mel_Gpu_Command_List* cmd);

// U17: D3D12-style state transitions. The command list tracks each touched subresource's state and
// asserts in debug when a declared source state disagrees with what it last recorded.
void mel_gpu_cmd_texture_barrier(Mel_Gpu_Command_List* cmd, Mel_Gpu_Texture tex, Mel_Gpu_Subresource_Range range, Mel_Gpu_Resource_State src, Mel_Gpu_Resource_State dst);
void mel_gpu_cmd_buffer_barrier(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer buf, Mel_Gpu_Resource_State src, Mel_Gpu_Resource_State dst);

// U10: copy a texture subresource into a buffer (readback). The texture must be in COPY_SOURCE state.
void mel_gpu_cmd_copy_texture_to_buffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Texture tex, Mel_Gpu_Subresource_Range subresource, Mel_Gpu_Buffer dst);

// U15: copy `size` bytes between buffers — the buffer-readback primitive. A device-local UAV/storage buffer
// cannot be host-mapped, so its computed result reaches the CPU through a copy into a READBACK buffer.
void mel_gpu_cmd_copy_buffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer src, Mel_Gpu_Buffer dst, usize bytes);

void mel_gpu_cmd_begin_pass(Mel_Gpu_Command_List* cmd, Mel_Gpu_Color clear);
void mel_gpu_cmd_end_pass(Mel_Gpu_Command_List* cmd);
void mel_gpu_cmd_bind_pipeline(Mel_Gpu_Command_List* cmd, Mel_Gpu_Pipeline pipe);
typedef enum
{
    MEL_GPU_INDEX_UINT16 = 0,
    MEL_GPU_INDEX_UINT32 = 1,
} Mel_Gpu_Index_Type;

void mel_gpu_cmd_bind_vertex_buffer(Mel_Gpu_Command_List* cmd, u32 slot, Mel_Gpu_Buffer buf);
void mel_gpu_cmd_bind_index_buffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer buf, Mel_Gpu_Index_Type type);
void mel_gpu_cmd_push_constants(Mel_Gpu_Command_List* cmd, u32 offset, u32 size, const void* data);
void mel_gpu_cmd_draw(Mel_Gpu_Command_List* cmd, u32 vertex_count, u32 instance_count);
void mel_gpu_cmd_draw_indexed(Mel_Gpu_Command_List* cmd, u32 index_count, u32 instance_count);

// U13 compute: dispatch a grid of workgroups against the bound compute pipeline.
void mel_gpu_cmd_dispatch(Mel_Gpu_Command_List* cmd, u32 groups_x, u32 groups_y, u32 groups_z);
