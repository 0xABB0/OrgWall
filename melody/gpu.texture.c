#define VK_NO_PROTOTYPES
#include "gpu.texture.h"
#include "string.str8.h"
#include "gpu.buffer.h"
#include "gpu.submit.h"
#include <stb_image.h>

typedef struct {
    Mel_Gpu_Image* image;
    Mel_Gpu_Buffer* staging;
    u32 width;
    u32 height;
} Gpu_Texture_Upload;

static void upload_cmd(VkCommandBuffer cmd, void* user)
{
    Gpu_Texture_Upload* data = (Gpu_Texture_Upload*)user;

    mel_gpu_image_transition(data->image, cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

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

    mel_gpu_image_transition(data->image, cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

static void create_sampler(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, bool nearest)
{
    VkFilter filter = nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;

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

    VkResult r = vkCreateSampler(dev->device, &sampler_info, nullptr, &tex->sampler);
    assert(r == VK_SUCCESS);
}

void mel_gpu_texture_init_opt(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, Mel_Gpu_Texture_Opt opt)
{
    assert(tex != nullptr);
    assert(dev != nullptr);
    assert(!str8_is_empty(opt.path) || opt.data != nullptr || opt.pixels != nullptr);

    *tex = (Mel_Gpu_Texture){0};

    u32 w, h;
    const u8* pixel_data = nullptr;
    bool free_pixels = false;

    if (opt.pixels)
    {
        assert(opt.width > 0 && opt.height > 0);
        pixel_data = opt.pixels;
        w = opt.width;
        h = opt.height;
    }
    else
    {
        int iw, ih, channels;
        u8* decoded = nullptr;

        if (!str8_is_empty(opt.path))
        {
            char path_buf[512];
            str8_to_buf(opt.path, path_buf, sizeof(path_buf));
            decoded = stbi_load(path_buf, &iw, &ih, &channels, 4);
        }
        else
            decoded = stbi_load_from_memory(opt.data, (int)opt.data_size, &iw, &ih, &channels, 4);

        assert(decoded != nullptr);
        pixel_data = decoded;
        w = (u32)iw;
        h = (u32)ih;
        free_pixels = true;
    }

    u32 image_size = w * h * 4;

    Mel_Gpu_Buffer staging;
    mel_gpu_buffer_init(&staging, dev,
        .size = image_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_ONLY);

    mel_gpu_buffer_upload(&staging, dev, pixel_data, image_size, 0);
    if (free_pixels)
        stbi_image_free((void*)pixel_data);

    mel_gpu_image_init(&tex->image, dev,
        .width = w,
        .height = h,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
        .alloc = opt.alloc);

    Gpu_Texture_Upload upload = {
        .image = &tex->image,
        .staging = &staging,
        .width = w,
        .height = h,
    };

    mel_gpu_submit_immediate(dev, upload_cmd, &upload);
    mel_gpu_buffer_shutdown(&staging, dev);

    create_sampler(tex, dev, opt.nearest_filter);

    SDL_Log("Texture loaded: %ux%u", w, h);
}

void mel_gpu_texture_init_white(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev)
{
    assert(tex != nullptr);
    assert(dev != nullptr);

    *tex = (Mel_Gpu_Texture){0};

    u8 white_pixel[4] = { 255, 255, 255, 255 };

    Mel_Gpu_Buffer staging;
    mel_gpu_buffer_init(&staging, dev,
        .size = 4,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_ONLY);

    mel_gpu_buffer_upload(&staging, dev, white_pixel, 4, 0);

    mel_gpu_image_init(&tex->image, dev,
        .width = 1,
        .height = 1,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT);

    Gpu_Texture_Upload upload = {
        .image = &tex->image,
        .staging = &staging,
        .width = 1,
        .height = 1,
    };

    mel_gpu_submit_immediate(dev, upload_cmd, &upload);
    mel_gpu_buffer_shutdown(&staging, dev);

    create_sampler(tex, dev, true);
}

void mel_gpu_texture_shutdown(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev)
{
    assert(tex != nullptr);
    assert(dev != nullptr);

    if (tex->sampler)
    {
        vkDestroySampler(dev->device, tex->sampler, nullptr);
        tex->sampler = VK_NULL_HANDLE;
    }

    mel_gpu_image_shutdown(&tex->image, dev);
}
