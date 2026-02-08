#ifndef MEL_VK_TEXTURE_H
#define MEL_VK_TEXTURE_H

#include "vk_context.h"
#include "vk_image.h"

typedef struct
{
    Mel_VkImage image;
    VkSampler sampler;
    VkDescriptorSet descriptor;
} Mel_VkTexture;

typedef struct
{
    const char* path;
    const u8* data;
    u32 data_size;
    bool nearest_filter;
} Mel_VkTexture_Opt;

bool mel_vk_texture_init_opt(Mel_VkTexture* tex, Mel_VkContext* ctx, Mel_VkTexture_Opt opt);
#define mel_vk_texture_init(tex, ctx, ...) mel_vk_texture_init_opt((tex), (ctx), (Mel_VkTexture_Opt){__VA_ARGS__})

bool mel_vk_texture_init_white(Mel_VkTexture* tex, Mel_VkContext* ctx);

void mel_vk_texture_shutdown(Mel_VkTexture* tex, Mel_VkContext* ctx);

void mel_vk_texture_cleanup_immediate(Mel_VkContext* ctx);

bool mel_vk_submit_immediate(Mel_VkContext* ctx, void (*func)(VkCommandBuffer cmd, void* user), void* user);

#endif
