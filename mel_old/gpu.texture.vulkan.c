#include "gpu.texture.h"
#include "gpu.device.vulkan.h"
#include "gpu.cmd.h"
#include "gpu.types.vulkan.h"
#include "str8.h"
#include "gpu.buffer.h"
#include "gpu.submit.h"
#include "log.h"
#include <stb_image.h>

typedef struct {
    Mel_Gpu_Image* image;
    Mel_Gpu_Buffer* staging;
    u32 width;
    u32 height;
    bool generate_mips;
} Gpu_Texture_Upload;

static void upload_cmd(Mel_Gpu_Cmd* cmd, void* user)
{
    Gpu_Texture_Upload* data = (Gpu_Texture_Upload*)user;

    mel_gpu_image_transition_subresource(data->image, cmd, 0, 0, MEL_GPU_IMAGE_LAYOUT_TRANSFER_DST);

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

    vkCmdCopyBufferToImage((VkCommandBuffer)cmd->_cmd,
        (VkBuffer)data->staging->_handle, (VkImage)data->image->_handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    if (!data->generate_mips)
    {
        mel_gpu_image_transition_subresource(data->image, cmd, 0, 0, MEL_GPU_IMAGE_LAYOUT_SHADER_READ_ONLY);
        return;
    }

    mel_gpu_image_transition_subresource(data->image, cmd, 0, 0, MEL_GPU_IMAGE_LAYOUT_TRANSFER_SRC);

    u32 src_w = data->width;
    u32 src_h = data->height;
    for (u32 mip = 1; mip < data->image->mip_levels; mip++)
    {
        u32 dst_w = src_w > 1 ? src_w / 2 : 1;
        u32 dst_h = src_h > 1 ? src_h / 2 : 1;

        mel_gpu_image_transition_subresource(data->image, cmd, mip, 0, MEL_GPU_IMAGE_LAYOUT_TRANSFER_DST);

        VkImageBlit region_blit = {
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = mip - 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .srcOffsets = {
                { 0, 0, 0 },
                { (i32)src_w, (i32)src_h, 1 },
            },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = mip,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .dstOffsets = {
                { 0, 0, 0 },
                { (i32)dst_w, (i32)dst_h, 1 },
            },
        };

        vkCmdBlitImage((VkCommandBuffer)cmd->_cmd,
            (VkImage)data->image->_handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            (VkImage)data->image->_handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &region_blit, VK_FILTER_LINEAR);

        mel_gpu_image_transition_subresource(data->image, cmd, mip - 1, 0, MEL_GPU_IMAGE_LAYOUT_SHADER_READ_ONLY);
        if (mip + 1 < data->image->mip_levels)
            mel_gpu_image_transition_subresource(data->image, cmd, mip, 0, MEL_GPU_IMAGE_LAYOUT_TRANSFER_SRC);
        else
            mel_gpu_image_transition_subresource(data->image, cmd, mip, 0, MEL_GPU_IMAGE_LAYOUT_SHADER_READ_ONLY);

        src_w = dst_w;
        src_h = dst_h;
    }
}

static VkSamplerAddressMode mel__gpu_texture_vk_address_mode(u32 mode)
{
    switch (mode)
    {
        case MEL_GPU_SAMPLER_ADDRESS_REPEAT:          return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case MEL_GPU_SAMPLER_ADDRESS_MIRRORED_REPEAT: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case MEL_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE:
        case MEL_GPU_SAMPLER_ADDRESS_DEFAULT:
        default:                                      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
}

static bool mel__gpu_texture_can_generate_mips(Mel_Gpu_Device* dev, Mel_Gpu_Format format)
{
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(mel__gpu_device_vk(dev)->physical_device,
        mel__gpu_format_to_vk(format), &props);
    return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
}

static u32 mel__gpu_texture_mip_count(u32 width, u32 height)
{
    u32 levels = 1;
    u32 dim = width > height ? width : height;
    while (dim > 1)
    {
        dim >>= 1;
        levels++;
    }
    return levels;
}

void* mel_gpu_sampler_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Sampler_Opt opt)
{
    assert(dev != nullptr);

    VkFilter filter = opt.nearest_filter ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    f32 max_lod = opt.max_lod;
    if (max_lod < opt.min_lod)
        max_lod = opt.min_lod;

    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = filter,
        .minFilter = filter,
        .mipmapMode = opt.nearest_filter ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = mel__gpu_texture_vk_address_mode(opt.address_mode_u),
        .addressModeV = mel__gpu_texture_vk_address_mode(opt.address_mode_v),
        .addressModeW = mel__gpu_texture_vk_address_mode(opt.address_mode_w),
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = opt.compare_enable ? VK_TRUE : VK_FALSE,
        .compareOp = (VkCompareOp[]){ 
            [MEL_GPU_COMPARE_LESS] = VK_COMPARE_OP_LESS,
            [MEL_GPU_COMPARE_NEVER] = VK_COMPARE_OP_NEVER,
            [MEL_GPU_COMPARE_EQUAL] = VK_COMPARE_OP_EQUAL,
            [MEL_GPU_COMPARE_LESS_OR_EQUAL] = VK_COMPARE_OP_LESS_OR_EQUAL,
            [MEL_GPU_COMPARE_GREATER] = VK_COMPARE_OP_GREATER,
            [MEL_GPU_COMPARE_NOT_EQUAL] = VK_COMPARE_OP_NOT_EQUAL,
            [MEL_GPU_COMPARE_GREATER_OR_EQUAL] = VK_COMPARE_OP_GREATER_OR_EQUAL,
            [MEL_GPU_COMPARE_ALWAYS] = VK_COMPARE_OP_ALWAYS,
        }[opt.compare_op],
        .minLod = opt.min_lod,
        .maxLod = max_lod,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    VkSampler sampler = VK_NULL_HANDLE;
    VkResult r = vkCreateSampler(mel__gpu_device_vk(dev)->device, &sampler_info, nullptr, &sampler);
    assert(r == VK_SUCCESS);
    return sampler;
}

void mel_gpu_sampler_destroy(Mel_Gpu_Device* dev, void* sampler)
{
    assert(dev != nullptr);
    if (sampler != nullptr)
        vkDestroySampler(mel__gpu_device_vk(dev)->device, (VkSampler)sampler, nullptr);
}

static void create_sampler(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, Mel_Gpu_Texture_Opt opt)
{
    f32 max_lod = tex->image.mip_levels > 0 ? (f32)(tex->image.mip_levels - 1) : 0.0f;
    tex->_sampler = mel_gpu_sampler_create(dev,
        .nearest_filter = opt.nearest_filter,
        .address_mode_u = opt.address_mode_u,
        .address_mode_v = opt.address_mode_v,
        .address_mode_w = opt.address_mode_w,
        .min_lod = 0.0f,
        .max_lod = max_lod,
        .compare_enable = false,
        .compare_op = MEL_GPU_COMPARE_ALWAYS);
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
        .usage = MEL_GPU_BUFFER_USAGE_TRANSFER_SRC,
        .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU);

    mel_gpu_buffer_upload(&staging, dev, pixel_data, image_size, 0);
    if (free_pixels)
        stbi_image_free((void*)pixel_data);

    Mel_Gpu_Format format = opt.format ? opt.format : MEL_GPU_FORMAT_R8G8B8A8_SRGB;
    bool generate_mips = opt.generate_mips;
    if (generate_mips && !mel__gpu_texture_can_generate_mips(dev, format))
    {
        mel_log_warn("gpu.texture", "format %u does not support linear-blit mip generation; falling back to single mip", format);
        generate_mips = false;
    }
    u32 mip_levels = generate_mips ? mel__gpu_texture_mip_count(w, h) : 1;

    mel_gpu_image_init(&tex->image, dev,
        .width = w,
        .height = h,
        .format = format,
        .usage = MEL_GPU_IMAGE_USAGE_SAMPLED |
                 MEL_GPU_IMAGE_USAGE_TRANSFER_DST |
                 (generate_mips ? MEL_GPU_IMAGE_USAGE_TRANSFER_SRC : 0),
        .aspect = MEL_GPU_ASPECT_COLOR,
        .mip_levels = mip_levels,
        .alloc = opt.alloc);

    Gpu_Texture_Upload upload = {
        .image = &tex->image,
        .staging = &staging,
        .width = w,
        .height = h,
        .generate_mips = generate_mips,
    };

    mel_gpu_submit_immediate(dev, upload_cmd, &upload);
    mel_gpu_buffer_shutdown(&staging, dev);

    create_sampler(tex, dev, opt);

    mel_log_debug("gpu.texture", "Texture loaded: %ux%u", w, h);
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
        .usage = MEL_GPU_BUFFER_USAGE_TRANSFER_SRC,
        .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU);

    mel_gpu_buffer_upload(&staging, dev, white_pixel, 4, 0);

    mel_gpu_image_init(&tex->image, dev,
        .width = 1,
        .height = 1,
        .format = MEL_GPU_FORMAT_R8G8B8A8_SRGB,
        .usage = MEL_GPU_IMAGE_USAGE_SAMPLED | MEL_GPU_IMAGE_USAGE_TRANSFER_DST,
        .aspect = MEL_GPU_ASPECT_COLOR);

    Gpu_Texture_Upload upload = {
        .image = &tex->image,
        .staging = &staging,
        .width = 1,
        .height = 1,
    };

    mel_gpu_submit_immediate(dev, upload_cmd, &upload);
    mel_gpu_buffer_shutdown(&staging, dev);

    create_sampler(tex, dev, (Mel_Gpu_Texture_Opt){
        .nearest_filter = true,
        .generate_mips = false,
        .address_mode_u = MEL_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE,
        .address_mode_v = MEL_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE,
        .address_mode_w = MEL_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE,
    });
}

void mel_gpu_texture_shutdown(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev)
{
    assert(tex != nullptr);
    assert(dev != nullptr);

    if (tex->_sampler)
    {
        mel_gpu_sampler_destroy(dev, tex->_sampler);
        tex->_sampler = nullptr;
    }

    mel_gpu_image_shutdown(&tex->image, dev);
}
