#include "render.manager.2d.h"
#include "gpu.device.h"
#include "allocator.h"
#include "allocator.heap.h"

#include <string.h>

_Static_assert(sizeof(Mel_Render_Transform_2D) == 32, "Mel_Render_Transform_2D must be 32 bytes");
_Static_assert(sizeof(Mel_Render_Bounds_2D) == 16, "Mel_Render_Bounds_2D must be 16 bytes");
_Static_assert(sizeof(Mel_Render_Sprite_Info) == 48, "Mel_Render_Sprite_Info must be 48 bytes");

void mel_mgr_2d_init_opt(Mel_Render_Manager_2D* mgr, Mel_Render_Manager_2D_Opt opt)
{
    assert(mgr != nullptr);
    assert(opt.dev != nullptr);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    u32 cap = opt.initial_capacity > 0 ? opt.initial_capacity : 64;

    *mgr = (Mel_Render_Manager_2D){0};
    mgr->dev = opt.dev;
    mgr->alloc = alloc;

    mel_storage_pool_init(&mgr->transforms,
        .dev = opt.dev,
        .alloc = alloc,
        .item_size = sizeof(Mel_Render_Transform_2D),
        .initial_capacity = cap,
        .cpu_mirror = true,
    );

    mel_storage_pool_init(&mgr->bounds,
        .dev = opt.dev,
        .alloc = alloc,
        .item_size = sizeof(Mel_Render_Bounds_2D),
        .initial_capacity = cap,
        .cpu_mirror = true,
    );

    mel_storage_pool_init(&mgr->sprite_infos,
        .dev = opt.dev,
        .alloc = alloc,
        .item_size = sizeof(Mel_Render_Sprite_Info),
        .initial_capacity = cap,
        .cpu_mirror = true,
    );
}

void mel_mgr_2d_shutdown(Mel_Render_Manager_2D* mgr)
{
    assert(mgr != nullptr);

    mel_storage_pool_shutdown(&mgr->sprite_infos, mgr->dev);
    mel_storage_pool_shutdown(&mgr->bounds, mgr->dev);
    mel_storage_pool_shutdown(&mgr->transforms, mgr->dev);

    *mgr = (Mel_Render_Manager_2D){0};
}

Mel_Render_Handle_2D mel_mgr_2d_alloc(Mel_Render_Manager_2D* mgr)
{
    assert(mgr != nullptr);

    Mel_Render_Transform_2D identity_transform = {
        .pos = MEL_VEC2_ZERO,
        .scale = MEL_VEC2_ONE,
        .rotation = 0.0f,
        .depth = 0.0f,
        .flags = 0,
        ._pad = 0,
    };

    Mel_Render_Bounds_2D zero_bounds = {0};
    Mel_Render_Sprite_Info zero_info = {0};

    Mel_Storage_Handle th = mel_storage_pool_alloc(&mgr->transforms, &identity_transform);
    mel_storage_pool_alloc(&mgr->bounds, &zero_bounds);
    mel_storage_pool_alloc(&mgr->sprite_infos, &zero_info);

    return (Mel_Render_Handle_2D){ .handle = th };
}

void mel_mgr_2d_free(Mel_Render_Manager_2D* mgr, Mel_Render_Handle_2D h)
{
    assert(mgr != nullptr);
    assert(mel_slotmap_alive(&mgr->transforms.slots, h.handle));

    mel_storage_pool_free(&mgr->transforms, h.handle);
    mel_storage_pool_free(&mgr->bounds, h.handle);
    mel_storage_pool_free(&mgr->sprite_infos, h.handle);
}

void mel_mgr_2d_set_transform(Mel_Render_Manager_2D* mgr, Mel_Render_Handle_2D h, Mel_Render_Transform_2D t)
{
    assert(mgr != nullptr);
    assert(mel_slotmap_alive(&mgr->transforms.slots, h.handle));

    mel_storage_pool_set(&mgr->transforms, h.handle, &t);
}

void mel_mgr_2d_set_bounds(Mel_Render_Manager_2D* mgr, Mel_Render_Handle_2D h, Mel_Render_Bounds_2D b)
{
    assert(mgr != nullptr);
    assert(mel_slotmap_alive(&mgr->bounds.slots, h.handle));

    mel_storage_pool_set(&mgr->bounds, h.handle, &b);
}

void mel_mgr_2d_set_sprite_info(Mel_Render_Manager_2D* mgr, Mel_Render_Handle_2D h, Mel_Render_Sprite_Info info)
{
    assert(mgr != nullptr);
    assert(mel_slotmap_alive(&mgr->sprite_infos.slots, h.handle));

    mel_storage_pool_set(&mgr->sprite_infos, h.handle, &info);
}

void mel_mgr_2d_set_pos(Mel_Render_Manager_2D* mgr, Mel_Render_Handle_2D h, Mel_Vec2 pos)
{
    assert(mgr != nullptr);
    assert(mel_slotmap_alive(&mgr->transforms.slots, h.handle));

    Mel_Render_Transform_2D* t = mel_storage_pool_get(&mgr->transforms, h.handle);
    assert(t != nullptr);

    t->pos = pos;
    mel_storage_pool_mark_dirty(&mgr->transforms, h.handle);
}

void mel_mgr_2d_upload_dirty(Mel_Render_Manager_2D* mgr)
{
    assert(mgr != nullptr);

    mel_storage_pool_upload_dirty(&mgr->transforms, mgr->dev);
    mel_storage_pool_upload_dirty(&mgr->bounds, mgr->dev);
    mel_storage_pool_upload_dirty(&mgr->sprite_infos, mgr->dev);
}

void mel_mgr_2d_cull_rect(Mel_Render_Manager_2D* mgr, Mel_Rect viewport, Mel_BitSet* out_visibility)
{
    assert(mgr != nullptr);
    assert(out_visibility != nullptr);

    u32 count = mel_storage_pool_count(&mgr->bounds);
    if (count == 0)
        return;

    if (out_visibility->bit_count < count)
        mel_bitset_resize(out_visibility, count);

    mel_bitset_clear(out_visibility);

    f32 vp_min_x = viewport.x;
    f32 vp_min_y = viewport.y;
    f32 vp_max_x = viewport.x + viewport.w;
    f32 vp_max_y = viewport.y + viewport.h;

    Mel_Render_Bounds_2D* bounds_data = (Mel_Render_Bounds_2D*)mgr->bounds.slots.data;

    for (u32 i = 0; i < count; i++)
    {
        Mel_Render_Bounds_2D b = bounds_data[i];

        f32 obj_min_x = b.center.x - b.half_extents.x;
        f32 obj_max_x = b.center.x + b.half_extents.x;
        f32 obj_min_y = b.center.y - b.half_extents.y;
        f32 obj_max_y = b.center.y + b.half_extents.y;

        bool overlaps = obj_max_x >= vp_min_x && obj_min_x <= vp_max_x
                     && obj_max_y >= vp_min_y && obj_min_y <= vp_max_y;

        if (overlaps)
            mel_bitset_set(out_visibility, i);
    }
}

u32 mel_mgr_2d_count(Mel_Render_Manager_2D* mgr)
{
    assert(mgr != nullptr);
    return mel_storage_pool_count(&mgr->transforms);
}

Mel_Gpu_Buffer* mel_mgr_2d_transform_buffer(Mel_Render_Manager_2D* mgr)
{
    assert(mgr != nullptr);
    return mel_storage_pool_buffer(&mgr->transforms);
}

Mel_Gpu_Buffer* mel_mgr_2d_bounds_buffer(Mel_Render_Manager_2D* mgr)
{
    assert(mgr != nullptr);
    return mel_storage_pool_buffer(&mgr->bounds);
}

Mel_Gpu_Buffer* mel_mgr_2d_sprite_info_buffer(Mel_Render_Manager_2D* mgr)
{
    assert(mgr != nullptr);
    return mel_storage_pool_buffer(&mgr->sprite_infos);
}
