#pragma once

#include "gpu.tracy.fwd.h"
#include "gpu.device.fwd.h"
#include "string.str8.fwd.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

bool mel_gpu_tracy_init(Mel_Gpu_Tracy_Ctx** out_ctx, Mel_Gpu_Device* dev, VkQueue queue, VkCommandBuffer cmd, str8 name);
void mel_gpu_tracy_shutdown(Mel_Gpu_Tracy_Ctx* ctx);
void mel_gpu_tracy_collect(Mel_Gpu_Tracy_Ctx* ctx, VkCommandBuffer cmd);
Mel_Gpu_Tracy_Zone mel_gpu_tracy_zone_begin(Mel_Gpu_Tracy_Ctx* ctx, VkCommandBuffer cmd, str8 name);
void mel_gpu_tracy_zone_end(Mel_Gpu_Tracy_Zone zone);

#ifdef __cplusplus
}
#endif
