#define VK_NO_PROTOTYPES
#include "vk_texture.h"
#include "vk_buffer.h"
#include <stb_image.h>

typedef struct
{
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
} ImmediateContext;

static ImmediateContext s_immediate = {0};

static bool ensure_immediate_context(Mel_VkContext* ctx)
{
    if (s_immediate.pool) return true;

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx->graphics_family,
    };

    VkResult result = vkCreateCommandPool(ctx->device, &pool_info, nullptr, &s_immediate.pool);
    if (result != VK_SUCCESS) return false;

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = s_immediate.pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    result = vkAllocateCommandBuffers(ctx->device, &alloc_info, &s_immediate.cmd);
    if (result != VK_SUCCESS) return false;

    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };

    result = vkCreateFence(ctx->device, &fence_info, nullptr, &s_immediate.fence);
    if (result != VK_SUCCESS) return false;

    return true;
}

bool mel_vk_submit_immediate(Mel_VkContext* ctx, void (*func)(VkCommandBuffer cmd, void* user), void* user)
{
    assert(ctx != nullptr);
    assert(func != nullptr);

    if (!ensure_immediate_context(ctx)) return false;

    vkResetCommandBuffer(s_immediate.cmd, 0);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(s_immediate.cmd, &begin_info);
    func(s_immediate.cmd, user);
    vkEndCommandBuffer(s_immediate.cmd);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &s_immediate.cmd,
    };

    vkResetFences(ctx->device, 1, &s_immediate.fence);
    vkQueueSubmit(ctx->graphics_queue, 1, &submit_info, s_immediate.fence);
    vkWaitForFences(ctx->device, 1, &s_immediate.fence, VK_TRUE, UINT64_MAX);

    return true;
}

typedef struct
{
    Mel_VkImage* image;
    Mel_VkBuffer* staging;
    u32 width;
    u32 height;
} UploadData;

static void upload_texture_cmd(VkCommandBuffer cmd, void* user)
{
    UploadData* data = (UploadData*)user;

    mel_vk_image_transition(data->image, cmd, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { data->width, data->height, 1 },
    };

    vkCmdCopyBufferToImage(cmd, data->staging->buffer, data->image->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    mel_vk_image_transition(data->image, cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

bool mel_vk_texture_init_opt(Mel_VkTexture* tex, Mel_VkContext* ctx, Mel_VkTexture_Opt opt)
{
    assert(tex != nullptr);
    assert(ctx != nullptr);
    assert(opt.path != nullptr || opt.data != nullptr);

    *tex = (Mel_VkTexture){0};

    int width, height, channels;
    u8* pixels = nullptr;

    if (opt.path)
    {
        pixels = stbi_load(opt.path, &width, &height, &channels, 4);
    }
    else
    {
        pixels = stbi_load_from_memory(opt.data, (int)opt.data_size, &width, &height, &channels, 4);
    }

    if (!pixels)
    {
        SDL_Log("Failed to load texture: %s", opt.path ? opt.path : "(from memory)");
        return false;
    }

    u32 image_size = (u32)(width * height * 4);

    Mel_VkBuffer staging;
    if (!mel_vk_buffer_init(&staging, ctx, image_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY))
    {
        stbi_image_free(pixels);
        return false;
    }

    mel_vk_buffer_upload(&staging, ctx, pixels, image_size, 0);
    stbi_image_free(pixels);

    if (!mel_vk_image_init(&tex->image, ctx,
        .width = (u32)width,
        .height = (u32)height,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT))
    {
        mel_vk_buffer_shutdown(&staging, ctx);
        return false;
    }

    UploadData upload = {
        .image = &tex->image,
        .staging = &staging,
        .width = (u32)width,
        .height = (u32)height,
    };

    if (!mel_vk_submit_immediate(ctx, upload_texture_cmd, &upload))
    {
        mel_vk_image_shutdown(&tex->image, ctx);
        mel_vk_buffer_shutdown(&staging, ctx);
        return false;
    }

    mel_vk_buffer_shutdown(&staging, ctx);

    VkFilter filter = opt.nearest_filter ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;

    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = filter,
        .minFilter = filter,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_FALSE,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    VkResult result = vkCreateSampler(ctx->device, &sampler_info, nullptr, &tex->sampler);
    if (result != VK_SUCCESS)
    {
        mel_vk_image_shutdown(&tex->image, ctx);
        return false;
    }

    SDL_Log("Texture loaded: %dx%d", width, height);

    return true;
}

void mel_vk_texture_shutdown(Mel_VkTexture* tex, Mel_VkContext* ctx)
{
    assert(tex != nullptr);
    assert(ctx != nullptr);

    if (tex->sampler)
    {
        vkDestroySampler(ctx->device, tex->sampler, nullptr);
        tex->sampler = VK_NULL_HANDLE;
    }

    mel_vk_image_shutdown(&tex->image, ctx);
}

void mel_vk_texture_cleanup_immediate(Mel_VkContext* ctx)
{
    if (s_immediate.fence)
    {
        vkDestroyFence(ctx->device, s_immediate.fence, nullptr);
        s_immediate.fence = VK_NULL_HANDLE;
    }

    if (s_immediate.pool)
    {
        vkDestroyCommandPool(ctx->device, s_immediate.pool, nullptr);
        s_immediate.pool = VK_NULL_HANDLE;
        s_immediate.cmd = VK_NULL_HANDLE;
    }
}

bool mel_vk_texture_init_white(Mel_VkTexture* tex, Mel_VkContext* ctx)
{
    assert(tex != nullptr);
    assert(ctx != nullptr);

    *tex = (Mel_VkTexture){0};

    u8 white_pixel[4] = { 255, 255, 255, 255 };
    u32 image_size = 4;

    Mel_VkBuffer staging;
    if (!mel_vk_buffer_init(&staging, ctx, image_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY))
    {
        return false;
    }

    mel_vk_buffer_upload(&staging, ctx, white_pixel, image_size, 0);

    if (!mel_vk_image_init(&tex->image, ctx,
        .width = 1,
        .height = 1,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT))
    {
        mel_vk_buffer_shutdown(&staging, ctx);
        return false;
    }

    UploadData upload = {
        .image = &tex->image,
        .staging = &staging,
        .width = 1,
        .height = 1,
    };

    if (!mel_vk_submit_immediate(ctx, upload_texture_cmd, &upload))
    {
        mel_vk_image_shutdown(&tex->image, ctx);
        mel_vk_buffer_shutdown(&staging, ctx);
        return false;
    }

    mel_vk_buffer_shutdown(&staging, ctx);

    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_FALSE,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    VkResult result = vkCreateSampler(ctx->device, &sampler_info, nullptr, &tex->sampler);
    if (result != VK_SUCCESS)
    {
        mel_vk_image_shutdown(&tex->image, ctx);
        return false;
    }

    return true;
}
