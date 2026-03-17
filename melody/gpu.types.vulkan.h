#pragma once

#include "gpu.types.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

static inline VkFormat mel__gpu_format_to_vk(Mel_Gpu_Format f)
{
    return (VkFormat)f;
}

static inline Mel_Gpu_Format mel__gpu_format_from_vk(VkFormat f)
{
    return (Mel_Gpu_Format)f;
}

static inline VkBufferUsageFlags mel__gpu_buffer_usage_to_vk(Mel_Gpu_Buffer_Usage u)
{
    return (VkBufferUsageFlags)u;
}

static inline Mel_Gpu_Buffer_Usage mel__gpu_buffer_usage_from_vk(VkBufferUsageFlags u)
{
    return (Mel_Gpu_Buffer_Usage)u;
}

static inline VkImageUsageFlags mel__gpu_image_usage_to_vk(Mel_Gpu_Image_Usage u)
{
    return (VkImageUsageFlags)u;
}

static inline Mel_Gpu_Image_Usage mel__gpu_image_usage_from_vk(VkImageUsageFlags u)
{
    return (Mel_Gpu_Image_Usage)u;
}

static inline VkImageAspectFlags mel__gpu_aspect_to_vk(Mel_Gpu_Aspect a)
{
    return (VkImageAspectFlags)a;
}

static inline Mel_Gpu_Aspect mel__gpu_aspect_from_vk(VkImageAspectFlags a)
{
    return (Mel_Gpu_Aspect)a;
}

static inline VkImageLayout mel__gpu_image_layout_to_vk(Mel_Gpu_Image_Layout l)
{
    return (VkImageLayout)l;
}

static inline Mel_Gpu_Image_Layout mel__gpu_image_layout_from_vk(VkImageLayout l)
{
    return (Mel_Gpu_Image_Layout)l;
}

static inline VkAttachmentLoadOp mel__gpu_load_op_to_vk(Mel_Gpu_Load_Op op)
{
    return (VkAttachmentLoadOp)op;
}

static inline Mel_Gpu_Load_Op mel__gpu_load_op_from_vk(VkAttachmentLoadOp op)
{
    return (Mel_Gpu_Load_Op)op;
}

static inline VkAttachmentStoreOp mel__gpu_store_op_to_vk(Mel_Gpu_Store_Op op)
{
    return (VkAttachmentStoreOp)op;
}

static inline Mel_Gpu_Store_Op mel__gpu_store_op_from_vk(VkAttachmentStoreOp op)
{
    return (Mel_Gpu_Store_Op)op;
}

static inline VkIndexType mel__gpu_index_type_to_vk(Mel_Gpu_Index_Type t)
{
    return (VkIndexType)t;
}

static inline Mel_Gpu_Index_Type mel__gpu_index_type_from_vk(VkIndexType t)
{
    return (Mel_Gpu_Index_Type)t;
}

static inline VkShaderStageFlags mel__gpu_shader_stage_to_vk(Mel_Gpu_Shader_Stage s)
{
    return (VkShaderStageFlags)s;
}

static inline Mel_Gpu_Shader_Stage mel__gpu_shader_stage_from_vk(VkShaderStageFlags s)
{
    return (Mel_Gpu_Shader_Stage)s;
}

static inline VmaMemoryUsage mel__gpu_memory_usage_to_vma(Mel_Gpu_Memory_Usage u)
{
    switch (u) {
        case MEL_GPU_MEMORY_USAGE_GPU_ONLY:   return VMA_MEMORY_USAGE_GPU_ONLY;
        case MEL_GPU_MEMORY_USAGE_CPU_TO_GPU: return VMA_MEMORY_USAGE_CPU_TO_GPU;
        case MEL_GPU_MEMORY_USAGE_GPU_TO_CPU: return VMA_MEMORY_USAGE_GPU_TO_CPU;
        default:                               return VMA_MEMORY_USAGE_AUTO;
    }
}

static inline Mel_Gpu_Memory_Usage mel__gpu_memory_usage_from_vma(VmaMemoryUsage u)
{
    switch (u) {
        case VMA_MEMORY_USAGE_GPU_ONLY:   return MEL_GPU_MEMORY_USAGE_GPU_ONLY;
        case VMA_MEMORY_USAGE_CPU_TO_GPU: return MEL_GPU_MEMORY_USAGE_CPU_TO_GPU;
        case VMA_MEMORY_USAGE_GPU_TO_CPU: return MEL_GPU_MEMORY_USAGE_GPU_TO_CPU;
        default:                           return MEL_GPU_MEMORY_USAGE_AUTO;
    }
}

static inline VkPresentModeKHR mel__gpu_present_mode_to_vk(Mel_Gpu_Present_Mode m)
{
    return (VkPresentModeKHR)m;
}

static inline Mel_Gpu_Present_Mode mel__gpu_present_mode_from_vk(VkPresentModeKHR m)
{
    return (Mel_Gpu_Present_Mode)m;
}

static inline VkPipelineStageFlags2 mel__gpu_stage_to_vk(Mel_Gpu_Stage s)
{
    VkPipelineStageFlags2 result = 0;
    if (s & MEL_GPU_STAGE_TOP_OF_PIPE)            result |= VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    if (s & MEL_GPU_STAGE_DRAW_INDIRECT)           result |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    if (s & MEL_GPU_STAGE_VERTEX_INPUT)            result |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    if (s & MEL_GPU_STAGE_VERTEX_SHADER)           result |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    if (s & MEL_GPU_STAGE_FRAGMENT_SHADER)         result |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    if (s & MEL_GPU_STAGE_EARLY_FRAGMENT_TESTS)    result |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
    if (s & MEL_GPU_STAGE_LATE_FRAGMENT_TESTS)     result |= VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    if (s & MEL_GPU_STAGE_COLOR_ATTACHMENT_OUTPUT)  result |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (s & MEL_GPU_STAGE_COMPUTE_SHADER)          result |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    if (s & MEL_GPU_STAGE_TRANSFER)                result |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    if (s & MEL_GPU_STAGE_BOTTOM_OF_PIPE)          result |= VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    if (s & MEL_GPU_STAGE_ALL_GRAPHICS)            result |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
    if (s & MEL_GPU_STAGE_ALL_COMMANDS)            result |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    return result;
}

static inline Mel_Gpu_Stage mel__gpu_stage_from_vk(VkPipelineStageFlags2 s)
{
    Mel_Gpu_Stage result = 0;
    if (s & VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT)            result |= MEL_GPU_STAGE_TOP_OF_PIPE;
    if (s & VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT)           result |= MEL_GPU_STAGE_DRAW_INDIRECT;
    if (s & VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT)            result |= MEL_GPU_STAGE_VERTEX_INPUT;
    if (s & VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT)           result |= MEL_GPU_STAGE_VERTEX_SHADER;
    if (s & VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT)         result |= MEL_GPU_STAGE_FRAGMENT_SHADER;
    if (s & VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT)    result |= MEL_GPU_STAGE_EARLY_FRAGMENT_TESTS;
    if (s & VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT)     result |= MEL_GPU_STAGE_LATE_FRAGMENT_TESTS;
    if (s & VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT)  result |= MEL_GPU_STAGE_COLOR_ATTACHMENT_OUTPUT;
    if (s & VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)          result |= MEL_GPU_STAGE_COMPUTE_SHADER;
    if (s & VK_PIPELINE_STAGE_2_TRANSFER_BIT)                result |= MEL_GPU_STAGE_TRANSFER;
    if (s & VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT)          result |= MEL_GPU_STAGE_BOTTOM_OF_PIPE;
    if (s & VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT)            result |= MEL_GPU_STAGE_ALL_GRAPHICS;
    if (s & VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)            result |= MEL_GPU_STAGE_ALL_COMMANDS;
    return result;
}

static inline VkAccessFlags2 mel__gpu_access_to_vk(Mel_Gpu_Access a)
{
    VkAccessFlags2 result = 0;
    if (a & MEL_GPU_ACCESS_INDIRECT_COMMAND_READ)      result |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    if (a & MEL_GPU_ACCESS_INDEX_READ)                 result |= VK_ACCESS_2_INDEX_READ_BIT;
    if (a & MEL_GPU_ACCESS_VERTEX_ATTRIBUTE_READ)      result |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    if (a & MEL_GPU_ACCESS_UNIFORM_READ)               result |= VK_ACCESS_2_UNIFORM_READ_BIT;
    if (a & MEL_GPU_ACCESS_SHADER_READ)                result |= VK_ACCESS_2_SHADER_READ_BIT;
    if (a & MEL_GPU_ACCESS_SHADER_WRITE)               result |= VK_ACCESS_2_SHADER_WRITE_BIT;
    if (a & MEL_GPU_ACCESS_COLOR_ATTACHMENT_READ)       result |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
    if (a & MEL_GPU_ACCESS_COLOR_ATTACHMENT_WRITE)      result |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    if (a & MEL_GPU_ACCESS_DEPTH_STENCIL_READ)         result |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    if (a & MEL_GPU_ACCESS_DEPTH_STENCIL_WRITE)        result |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    if (a & MEL_GPU_ACCESS_TRANSFER_READ)              result |= VK_ACCESS_2_TRANSFER_READ_BIT;
    if (a & MEL_GPU_ACCESS_TRANSFER_WRITE)             result |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
    if (a & MEL_GPU_ACCESS_MEMORY_READ)                result |= VK_ACCESS_2_MEMORY_READ_BIT;
    if (a & MEL_GPU_ACCESS_MEMORY_WRITE)               result |= VK_ACCESS_2_MEMORY_WRITE_BIT;
    return result;
}

static inline Mel_Gpu_Access mel__gpu_access_from_vk(VkAccessFlags2 a)
{
    Mel_Gpu_Access result = 0;
    if (a & VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT)              result |= MEL_GPU_ACCESS_INDIRECT_COMMAND_READ;
    if (a & VK_ACCESS_2_INDEX_READ_BIT)                         result |= MEL_GPU_ACCESS_INDEX_READ;
    if (a & VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT)              result |= MEL_GPU_ACCESS_VERTEX_ATTRIBUTE_READ;
    if (a & VK_ACCESS_2_UNIFORM_READ_BIT)                       result |= MEL_GPU_ACCESS_UNIFORM_READ;
    if (a & VK_ACCESS_2_SHADER_READ_BIT)                        result |= MEL_GPU_ACCESS_SHADER_READ;
    if (a & VK_ACCESS_2_SHADER_WRITE_BIT)                       result |= MEL_GPU_ACCESS_SHADER_WRITE;
    if (a & VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT)               result |= MEL_GPU_ACCESS_COLOR_ATTACHMENT_READ;
    if (a & VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT)              result |= MEL_GPU_ACCESS_COLOR_ATTACHMENT_WRITE;
    if (a & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT)       result |= MEL_GPU_ACCESS_DEPTH_STENCIL_READ;
    if (a & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)      result |= MEL_GPU_ACCESS_DEPTH_STENCIL_WRITE;
    if (a & VK_ACCESS_2_TRANSFER_READ_BIT)                       result |= MEL_GPU_ACCESS_TRANSFER_READ;
    if (a & VK_ACCESS_2_TRANSFER_WRITE_BIT)                      result |= MEL_GPU_ACCESS_TRANSFER_WRITE;
    if (a & VK_ACCESS_2_MEMORY_READ_BIT)                         result |= MEL_GPU_ACCESS_MEMORY_READ;
    if (a & VK_ACCESS_2_MEMORY_WRITE_BIT)                        result |= MEL_GPU_ACCESS_MEMORY_WRITE;
    return result;
}
