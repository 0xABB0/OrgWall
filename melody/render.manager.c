#include "render.manager.h"
#include "gpu.geometry_pool.h"
#include "gpu.device.h"
#include "allocator.h"
#include "allocator.heap.h"

#include <string.h>

void mel_mgr_init_opt(Mel_Render_Manager* mgr, Mel_Render_Manager_Opt opt)
{
    assert(mgr != nullptr);
    assert(opt.dev != nullptr);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    u32 cap = opt.initial_capacity > 0 ? opt.initial_capacity : 64;

    *mgr = (Mel_Render_Manager){0};
    mgr->dev = opt.dev;
    mgr->alloc = alloc;
    mgr->geometry = opt.geometry;
    mgr->materials = opt.materials;

    bool mirror_transforms = (opt.cpu_access_mask & MEL_MGR_TRANSFORMS) != 0;
    bool mirror_bounds = (opt.cpu_access_mask & MEL_MGR_BOUNDS) != 0;
    bool mirror_infos = (opt.cpu_access_mask & MEL_MGR_INFOS) != 0;

    mel_storage_pool_init(&mgr->transforms,
        .dev = opt.dev,
        .alloc = alloc,
        .item_size = sizeof(Mel_Render_Transform),
        .initial_capacity = cap,
        .cpu_mirror = mirror_transforms,
    );

    mel_storage_pool_init(&mgr->bounds,
        .dev = opt.dev,
        .alloc = alloc,
        .item_size = sizeof(Mel_Render_Bounds),
        .initial_capacity = cap,
        .cpu_mirror = mirror_bounds);

    mel_storage_pool_init(&mgr->infos,
        .dev = opt.dev,
        .alloc = alloc,
        .item_size = sizeof(Mel_Render_Info),
        .initial_capacity = cap,
        .cpu_mirror = mirror_infos);
}

void mel_mgr_shutdown(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);

    mel_storage_pool_shutdown(&mgr->infos, mgr->dev);
    mel_storage_pool_shutdown(&mgr->bounds, mgr->dev);
    mel_storage_pool_shutdown(&mgr->transforms, mgr->dev);

    *mgr = (Mel_Render_Manager){0};
}

Mel_Render_Handle mel_mgr_alloc(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);

    Mel_Render_Transform identity_transform = {
        .model = MEL_MAT4_IDENTITY,
        .model_inverse = MEL_MAT4_IDENTITY,
    };

    Mel_Render_Bounds zero_bounds = {0};
    Mel_Render_Info zero_info = {0};

    Mel_Storage_Handle th = mel_storage_pool_alloc(&mgr->transforms, &identity_transform);
    mel_storage_pool_alloc(&mgr->bounds, &zero_bounds);
    mel_storage_pool_alloc(&mgr->infos, &zero_info);

    return (Mel_Render_Handle){ .handle = th };
}

void mel_mgr_free(Mel_Render_Manager* mgr, Mel_Render_Handle h)
{
    assert(mgr != nullptr);
    assert(mel_slotmap_alive(&mgr->transforms.slots, h.handle));

    mel_storage_pool_free(&mgr->transforms, h.handle);
    mel_storage_pool_free(&mgr->bounds, h.handle);
    mel_storage_pool_free(&mgr->infos, h.handle);
}

void mel_mgr_set_transform(Mel_Render_Manager* mgr, Mel_Render_Handle h, Mel_Mat4 model)
{
    assert(mgr != nullptr);
    assert(mel_slotmap_alive(&mgr->transforms.slots, h.handle));

    Mel_Render_Transform t = {
        .model = model,
        .model_inverse = mel_mat4_inverse(model),
    };

    mel_storage_pool_set(&mgr->transforms, h.handle, &t);
}

void mel_mgr_set_bounds(Mel_Render_Manager* mgr, Mel_Render_Handle h, Mel_Render_Bounds bounds)
{
    assert(mgr != nullptr);
    assert(mel_slotmap_alive(&mgr->bounds.slots, h.handle));

    mel_storage_pool_set(&mgr->bounds, h.handle, &bounds);
}

void mel_mgr_set_info(Mel_Render_Manager* mgr, Mel_Render_Handle h, Mel_Render_Info info)
{
    assert(mgr != nullptr);
    assert(mel_slotmap_alive(&mgr->infos.slots, h.handle));

    mel_storage_pool_set(&mgr->infos, h.handle, &info);
}

void mel_mgr_upload_dirty(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);

    mel_storage_pool_upload_dirty(&mgr->transforms, mgr->dev);
    mel_storage_pool_upload_dirty(&mgr->bounds, mgr->dev);
    mel_storage_pool_upload_dirty(&mgr->infos, mgr->dev);

    if (mgr->geometry != nullptr)
        mel_geometry_pool_upload_catalog(mgr->geometry);
}

void mel_mgr_cull(Mel_Render_Manager* mgr, const Mel_Frustum* frustum, Mel_BitSet* out_visibility)
{
    assert(mgr != nullptr);
    assert(frustum != nullptr);
    assert(out_visibility != nullptr);

    u32 count = mel_storage_pool_count(&mgr->bounds);
    if (count == 0)
        return;

    if (out_visibility->bit_count < count)
        mel_bitset_resize(out_visibility, count);

    Mel_Render_Bounds* bounds_data = (Mel_Render_Bounds*)mgr->bounds.slots.data;

    _Static_assert(sizeof(Mel_Render_Bounds) == sizeof(Mel_AABB),
        "Mel_Render_Bounds and Mel_AABB must have identical layout");
    _Static_assert(offsetof(Mel_Render_Bounds, center) == offsetof(Mel_AABB, center),
        "center offset mismatch");
    _Static_assert(offsetof(Mel_Render_Bounds, extents) == offsetof(Mel_AABB, extents),
        "extents offset mismatch");

    mel_frustum_cull((const Mel_AABB*)bounds_data, count, frustum, out_visibility);
}

u32 mel_mgr_count(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);
    return mel_storage_pool_count(&mgr->transforms);
}

Mel_Gpu_Buffer* mel_mgr_transform_buffer(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);
    return mel_storage_pool_buffer(&mgr->transforms);
}

Mel_Gpu_Buffer* mel_mgr_bounds_buffer(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);
    return mel_storage_pool_buffer(&mgr->bounds);
}

Mel_Gpu_Buffer* mel_mgr_info_buffer(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);
    return mel_storage_pool_buffer(&mgr->infos);
}

void mel_mgr_request_cpu_access(Mel_Render_Manager* mgr, u32 buffer_mask)
{
    assert(mgr != nullptr);
    (void)buffer_mask;
}
