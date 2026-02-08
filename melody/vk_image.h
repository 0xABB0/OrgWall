#ifndef MEL_VK_IMAGE_H
#define MEL_VK_IMAGE_H

#include "vk_context.h"

typedef struct
{
    VkImage image;
    VkImageView view;
    VmaAllocation allocation;
    VkFormat format;
    u32 width;
    u32 height;
} Mel_VkImage;

typedef struct
{
    u32 width;
    u32 height;
    VkFormat format;
    VkImageUsageFlags usage;
    VkImageAspectFlags aspect;
} Mel_VkImage_Opt;

bool mel_vk_image_init_opt(Mel_VkImage* img, Mel_VkContext* ctx, Mel_VkImage_Opt opt);
#define mel_vk_image_init(img, ctx, ...) mel_vk_image_init_opt((img), (ctx), (Mel_VkImage_Opt){__VA_ARGS__})

void mel_vk_image_shutdown(Mel_VkImage* img, Mel_VkContext* ctx);

void mel_vk_image_transition(Mel_VkImage* img, VkCommandBuffer cmd, VkImageLayout old_layout, VkImageLayout new_layout);

#endif
