#pragma once

#include <vulkan/vulkan.h>

#include <gpu/device.h>
#include <gpu/queue.h>
#include <gpu/buffer.h>
#include <gpu/texture.h>
#include <gpu/command.h>
#include <gpu/sync.h>

VkInstance       mel_gpu_vk_instance(Mel_Gpu_Instance* inst);
VkPhysicalDevice mel_gpu_vk_physical_device(Mel_Gpu_Device* dev);
VkDevice         mel_gpu_vk_device(Mel_Gpu_Device* dev);
VkQueue          mel_gpu_vk_queue(Mel_Gpu_Queue* q);
VkCommandBuffer  mel_gpu_vk_command_buffer(Mel_Gpu_Command_List* cmd);
VkBuffer         mel_gpu_vk_buffer(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf);
VkSemaphore      mel_gpu_vk_semaphore(Mel_Gpu_Device* dev, Mel_Gpu_Sync sync);

Mel_Gpu_Sync mel_gpu_sync_import(Mel_Gpu_Device* dev, VkSemaphore native, bool timeline);

void mel_gpu_cmd_assume_state(Mel_Gpu_Command_List* cmd);

// U17 P2 escape (gpu-rhi.md §7.3): an explicit synchronization-precision barrier with native stage/access/
// layout. Bypasses the state-enum lowering and the command list's state tracking — the caller owns
// correctness past the hatch (MEL-ENGINE-VIII).
void mel_gpu_vk_cmd_image_barrier(Mel_Gpu_Command_List* cmd, Mel_Gpu_Texture tex, VkImageSubresourceRange range,
                                  VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkImageLayout old_layout,
                                  VkPipelineStageFlags dst_stage, VkAccessFlags dst_access, VkImageLayout new_layout);
