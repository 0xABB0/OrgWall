#include "render.target.h"
#include "swapchain.h"
#include "gpu.format.h"
#include "allocator.heap.h"

void mel_render_target_init_opt(Mel_Render_Target* t, Mel_Gpu_Device* dev, Mel_Render_Target_Opt opt)
{
    assert(t != nullptr);
    assert(dev != nullptr);
    assert(opt.width > 0);
    assert(opt.height > 0);
    assert(opt.format != MEL_GPU_FORMAT_UNDEFINED);

    *t = (Mel_Render_Target){0};

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    bool depth = mel_gpu_format_has_depth(opt.format);

    t->kind = depth ? MEL_RENDER_TARGET_DEPTH : MEL_RENDER_TARGET_COLOR;
    t->name = opt.name;
    t->width = opt.width;
    t->height = opt.height;
    t->format = opt.format;
    t->dev = dev;
    t->alloc = alloc;

    Mel_Gpu_Image_Usage usage = depth
        ? (MEL_GPU_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT | MEL_GPU_IMAGE_USAGE_SAMPLED)
        : (MEL_GPU_IMAGE_USAGE_COLOR_ATTACHMENT | MEL_GPU_IMAGE_USAGE_SAMPLED);
    usage |= opt.extra_usage;

    Mel_Gpu_Aspect aspect = depth ? MEL_GPU_ASPECT_DEPTH : MEL_GPU_ASPECT_COLOR;

    mel_gpu_image_init(&t->image, dev,
        .width = opt.width,
        .height = opt.height,
        .format = opt.format,
        .usage = usage,
        .aspect = aspect,
        .alloc = alloc,
    );
}

void mel_render_target_init_swapchain(Mel_Render_Target* t, Mel_Swapchain* swapchain,
                                       Mel_Gpu_Device* dev, str8 name)
{
    assert(t != nullptr);
    assert(swapchain != nullptr);
    assert(dev != nullptr);

    *t = (Mel_Render_Target){0};
    t->kind = MEL_RENDER_TARGET_SWAPCHAIN;
    t->name = name;
    t->width = swapchain->extent_width;
    t->height = swapchain->extent_height;
    t->format = swapchain->format;
    t->swapchain = swapchain;
    t->dev = dev;
}

void mel_render_target_shutdown(Mel_Render_Target* t)
{
    assert(t != nullptr);

    if (t->kind != MEL_RENDER_TARGET_SWAPCHAIN)
        mel_gpu_image_shutdown(&t->image, t->dev);

    *t = (Mel_Render_Target){0};
}

void* mel_render_target_view(Mel_Render_Target* t)
{
    assert(t != nullptr);

    if (t->kind == MEL_RENDER_TARGET_SWAPCHAIN)
    {
        assert(t->swapchain != nullptr);
        return t->swapchain->_image_views[t->swapchain->current_image];
    }
    return t->image._view;
}

void* mel_render_target_image(Mel_Render_Target* t)
{
    assert(t != nullptr);

    if (t->kind == MEL_RENDER_TARGET_SWAPCHAIN)
    {
        assert(t->swapchain != nullptr);
        return t->swapchain->_images[t->swapchain->current_image];
    }
    return t->image._handle;
}

u32 mel_render_target_width(Mel_Render_Target* t)
{
    assert(t != nullptr);

    if (t->kind == MEL_RENDER_TARGET_SWAPCHAIN)
        return t->swapchain->extent_width;
    return t->width;
}

u32 mel_render_target_height(Mel_Render_Target* t)
{
    assert(t != nullptr);

    if (t->kind == MEL_RENDER_TARGET_SWAPCHAIN)
        return t->swapchain->extent_height;
    return t->height;
}
