#include "render.target.h"
#include "swapchain.h"
#include "allocator.h"
#include "allocator.heap.h"

#include <assert.h>
#include <string.h>

Mel_Render_Target* mel_render_target_from_swapchain(Mel_Swapchain_Handle sc, const Mel_Alloc* alloc)
{
    assert(mel_swapchain_handle_valid(sc));

    if (!alloc) alloc = mel_alloc_heap();

    Mel_Render_Target* target = mel_alloc_type(alloc, Mel_Render_Target);
    memset(target, 0, sizeof(*target));

    Mel_Swapchain_Entry* entry = mel_swapchain_registry_get(sc);
    assert(entry != nullptr);

    target->type      = MEL_TARGET_WINDOW;
    target->swapchain = sc;
    target->width     = entry->swapchain.extent_width;
    target->height    = entry->swapchain.extent_height;
    target->format    = entry->swapchain.format;
    target->_alloc    = alloc;

    return target;
}

Mel_Render_Target* mel_render_target_offscreen_opt(Mel_Render_Target_Offscreen_Opt opt)
{
    assert(opt.dev != nullptr);
    assert(opt.width > 0);
    assert(opt.height > 0);
    assert(opt.format != MEL_GPU_FORMAT_UNDEFINED);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    Mel_Render_Target* target = mel_alloc_type(alloc, Mel_Render_Target);
    memset(target, 0, sizeof(*target));

    target->type   = MEL_TARGET_OFFSCREEN;
    target->width  = opt.width;
    target->height = opt.height;
    target->format = opt.format;
    target->_dev   = opt.dev;
    target->_alloc = alloc;

    mel_gpu_image_init(&target->offscreen_image, opt.dev,
        .width   = opt.width,
        .height  = opt.height,
        .format  = opt.format,
        .usage   = MEL_GPU_IMAGE_USAGE_COLOR_ATTACHMENT | MEL_GPU_IMAGE_USAGE_SAMPLED,
        .aspect  = MEL_GPU_ASPECT_COLOR,
        .mip_levels  = 1,
        .layer_count = 1,
        .alloc   = alloc);

    target->_current_view = target->offscreen_image._view;

    return target;
}

void mel_render_target_destroy(Mel_Render_Target* target)
{
    assert(target != nullptr);

    if (target->type == MEL_TARGET_OFFSCREEN)
        mel_gpu_image_shutdown(&target->offscreen_image, target->_dev);

    const Mel_Alloc* alloc = target->_alloc;
    mel_dealloc(alloc, target);
}

u32 mel_render_target_width(Mel_Render_Target* target)
{
    assert(target != nullptr);
    return target->width;
}

u32 mel_render_target_height(Mel_Render_Target* target)
{
    assert(target != nullptr);
    return target->height;
}

Mel_Gpu_Format mel_render_target_format(Mel_Render_Target* target)
{
    assert(target != nullptr);
    return target->format;
}

void* mel_render_target_image_view(Mel_Render_Target* target)
{
    assert(target != nullptr);
    return target->_current_view;
}

void mel_render_target_begin_frame(Mel_Render_Target* target)
{
    assert(target != nullptr);

    if (target->type != MEL_TARGET_WINDOW)
        return;

    Mel_Swapchain_Entry* entry = mel_swapchain_registry_get(target->swapchain);
    assert(entry != nullptr);

    Mel_Swapchain* sc = &entry->swapchain;
    target->width         = sc->extent_width;
    target->height        = sc->extent_height;
    target->format        = sc->format;
    target->_current_view = sc->_image_views[sc->current_image];
}
