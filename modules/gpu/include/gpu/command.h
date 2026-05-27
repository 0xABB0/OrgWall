#pragma once

#include <gpu/types.h>

// Per-frame flow: acquire the next drawable, hand back a command list to record
// into, then submit and present. Mirrors a begin/record/end frame.
void                  mel_gpu_frame_begin   (Mel_Gpu_Swapchain* sc);
Mel_Gpu_Command_List* mel_gpu_frame_commands(Mel_Gpu_Swapchain* sc);
void                  mel_gpu_frame_end     (Mel_Gpu_Swapchain* sc);

// A render pass targeting the swapchain's current drawable.
void mel_gpu_cmd_begin_pass(Mel_Gpu_Command_List* cmd, Mel_Gpu_Color clear);
void mel_gpu_cmd_end_pass  (Mel_Gpu_Command_List* cmd);

void mel_gpu_cmd_bind_pipeline     (Mel_Gpu_Command_List* cmd, Mel_Gpu_Pipeline* pipe);
void mel_gpu_cmd_bind_vertex_buffer(Mel_Gpu_Command_List* cmd, u32 slot, Mel_Gpu_Buffer* buf);
void mel_gpu_cmd_draw              (Mel_Gpu_Command_List* cmd, u32 vertex_count, u32 instance_count);
