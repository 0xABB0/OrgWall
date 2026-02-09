#ifndef MEL_GPU_IMAGE_H
#define MEL_GPU_IMAGE_H

#include "gpu.device.h"

typedef struct Mel_Gpu_Image_State Mel_Gpu_Image_State;
typedef struct Mel_Gpu_Image Mel_Gpu_Image;

struct Mel_Gpu_Image_State {
    VkImageLayout layout;
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
};

struct Mel_Gpu_Image {
    VkImage image;
    VkImageView view;
    VmaAllocation allocation;
    VkFormat format;
    u32 width;
    u32 height;
    u32 mip_levels;
    u32 layer_count;
    VkImageAspectFlags aspect;
    Mel_Gpu_Image_State* subresource_states;
    const Mel_Alloc* alloc;
};

typedef struct {
    u32 width;
    u32 height;
    VkFormat format;
    VkImageUsageFlags usage;
    VkImageAspectFlags aspect;
    u32 mip_levels;
    u32 layer_count;
    const Mel_Alloc* alloc;
} Mel_Gpu_Image_Opt;

void mel_gpu_image_init_opt(Mel_Gpu_Image* img, Mel_Gpu_Device* dev, Mel_Gpu_Image_Opt opt);
#define mel_gpu_image_init(img, dev, ...) mel_gpu_image_init_opt((img), (dev), (Mel_Gpu_Image_Opt){__VA_ARGS__})

void mel_gpu_image_shutdown(Mel_Gpu_Image* img, Mel_Gpu_Device* dev);

void mel_gpu_image_transition(Mel_Gpu_Image* img, VkCommandBuffer cmd,
                              VkImageLayout new_layout);

void mel_gpu_image_transition_subresource(Mel_Gpu_Image* img, VkCommandBuffer cmd,
                                          u32 mip, u32 layer, VkImageLayout new_layout);

Mel_Gpu_Image_State mel_gpu_image_state(Mel_Gpu_Image* img, u32 mip, u32 layer);

#endif
