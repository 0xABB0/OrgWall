#ifndef MEL_RENDER_FRAME_H
#define MEL_RENDER_FRAME_H

#include "gpu.device.h"
#include "gpu.swapchain.fwd.h"
#include "gpu.cmd.fwd.h"

#define MEL_MAX_FRAMES_IN_FLIGHT 3

typedef struct Mel_Render_Frame_Data Mel_Render_Frame_Data;
typedef struct Mel_Render_Frame Mel_Render_Frame;

struct Mel_Render_Frame_Data {
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence in_flight;
};

struct Mel_Render_Frame {
    Mel_Render_Frame_Data frames[MEL_MAX_FRAMES_IN_FLIGHT];
    u32 frame_count;
    u32 current_frame;
    Mel_Gpu_Device* dev;
    Mel_Gpu_Swapchain* swapchain;
};

typedef struct {
    Mel_Gpu_Device* dev;
    Mel_Gpu_Swapchain* swapchain;
    u32 frame_count;
} Mel_Render_Frame_Opt;

void mel_render_frame_init_opt(Mel_Render_Frame* rf, Mel_Render_Frame_Opt opt);
#define mel_render_frame_init(rf, ...) mel_render_frame_init_opt((rf), (Mel_Render_Frame_Opt){__VA_ARGS__})

void mel_render_frame_shutdown(Mel_Render_Frame* rf);

bool mel_render_frame_begin(Mel_Render_Frame* rf);
void mel_render_frame_end(Mel_Render_Frame* rf);

VkCommandBuffer mel_render_frame_cmd(Mel_Render_Frame* rf);

Mel_Render_Frame_Data* mel_render_frame_current(Mel_Render_Frame* rf);

#endif
