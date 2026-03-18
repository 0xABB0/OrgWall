#include "render.target.h"
#include "collection.slotmap.h"
#include "swapchain.h"
#include "allocator.heap.h"

#include <assert.h>
#include <string.h>

static Mel_SlotMap s_targets;
static bool s_initialized;

__attribute__((constructor))
static void mel__target_registry_init(void)
{
    mel_slotmap_init(&s_targets, mel_alloc_heap(),
        .item_size = sizeof(Mel_Render_Target), .initial_capacity = 8);
    s_initialized = true;
}

__attribute__((destructor))
static void mel__target_registry_shutdown(void)
{
    if (!s_initialized) return;

    Mel_Render_Target* targets = mel_slotmap_data(&s_targets);
    u32 count = mel_slotmap_count(&s_targets);

    for (u32 i = 0; i < count; i++)
    {
        if (targets[i].type == MEL_TARGET_OFFSCREEN)
            mel_gpu_image_shutdown(&targets[i].offscreen_image, targets[i]._dev);
    }

    mel_slotmap_free(&s_targets);
    s_initialized = false;
}

Mel_Render_Target_Handle mel_render_target_from_swapchain(Mel_Swapchain_Handle sc)
{
    assert(s_initialized);
    assert(mel_swapchain_handle_valid(sc));

    Mel_Swapchain_Entry* entry = mel_swapchain_registry_get(sc);
    assert(entry != nullptr);

    Mel_Render_Target target = {0};
    target.type      = MEL_TARGET_WINDOW;
    target.swapchain = sc;
    target.width     = entry->swapchain.extent_width;
    target.height    = entry->swapchain.extent_height;
    target.format    = entry->swapchain.format;

    Mel_SlotMap_Handle raw = mel_slotmap_insert(&s_targets, &target);
    return (Mel_Render_Target_Handle){ .handle = raw };
}

Mel_Render_Target_Handle mel_render_target_offscreen_opt(Mel_Render_Target_Offscreen_Opt opt)
{
    assert(s_initialized);
    assert(opt.dev != nullptr);
    assert(opt.width > 0);
    assert(opt.height > 0);
    assert(opt.format != MEL_GPU_FORMAT_UNDEFINED);

    Mel_Render_Target target = {0};
    target.type   = MEL_TARGET_OFFSCREEN;
    target.width  = opt.width;
    target.height = opt.height;
    target.format = opt.format;
    target._dev   = opt.dev;

    mel_gpu_image_init(&target.offscreen_image, opt.dev,
        .width   = opt.width,
        .height  = opt.height,
        .format  = opt.format,
        .usage   = MEL_GPU_IMAGE_USAGE_COLOR_ATTACHMENT | MEL_GPU_IMAGE_USAGE_SAMPLED,
        .aspect  = MEL_GPU_ASPECT_COLOR,
        .mip_levels  = 1,
        .layer_count = 1,
        .alloc   = opt.alloc);

    target._current_view = target.offscreen_image._view;

    Mel_SlotMap_Handle raw = mel_slotmap_insert(&s_targets, &target);
    return (Mel_Render_Target_Handle){ .handle = raw };
}

void mel_render_target_destroy(Mel_Render_Target_Handle handle)
{
    if (!s_initialized) return;
    if (!mel_slotmap_alive(&s_targets, handle.handle)) return;

    Mel_Render_Target* target = mel_slotmap_get(&s_targets, handle.handle);
    assert(target != nullptr);

    if (target->type == MEL_TARGET_OFFSCREEN)
        mel_gpu_image_shutdown(&target->offscreen_image, target->_dev);

    mel_slotmap_remove(&s_targets, handle.handle);
}

bool mel_render_target_alive(Mel_Render_Target_Handle handle)
{
    if (!s_initialized) return false;
    return mel_slotmap_alive(&s_targets, handle.handle);
}

Mel_Render_Target* mel_render_target_get(Mel_Render_Target_Handle handle)
{
    assert(s_initialized);
    Mel_Render_Target* target = mel_slotmap_get(&s_targets, handle.handle);
    assert(target != nullptr);
    return target;
}

void mel_render_target_destroy_by_swapchain(Mel_Swapchain_Handle sc)
{
    if (!s_initialized) return;

    Mel_Render_Target* targets = mel_slotmap_data(&s_targets);
    u32 count = mel_slotmap_count(&s_targets);

    for (u32 i = count; i > 0; i--)
    {
        Mel_Render_Target* t = &targets[i - 1];
        if (t->type != MEL_TARGET_WINDOW) continue;
        if (t->swapchain.handle.index != sc.handle.index ||
            t->swapchain.handle.generation != sc.handle.generation) continue;

        u32 slot_idx = s_targets.packed_to_slot[i - 1];
        Mel_SlotMap_Slot* slot = &s_targets.slots[slot_idx];
        Mel_Render_Target_Handle h = {
            .handle = mel_slotmap_handle_make(slot_idx, slot->generation),
        };
        mel_render_target_destroy(h);
    }
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
