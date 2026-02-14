#pragma once

#include "core.types.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

u32 mel_gpu_format_size(VkFormat format);
bool mel_gpu_format_has_depth(VkFormat format);
bool mel_gpu_format_has_stencil(VkFormat format);
bool mel_gpu_format_is_compressed(VkFormat format);
VkImageAspectFlags mel_gpu_format_aspect(VkFormat format);
