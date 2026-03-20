#include "render.source.manual.h"
#include "render.source.type.h"
#include "render.scene.h"
#include "render.manager.h"
#include "render.types.3d.h"
#include "collection.array.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "math.mat4.h"

typedef struct {
    Mel_Render_Handle handle;
    Mel_Mat4 transform;
    Mel_Render_Bounds bounds;
    Mel_Render_Info info;
} Mel_Manual_Pending_Add;

typedef Mel_Array(Mel_Manual_Pending_Add) Mel_Manual_Add_List;
typedef Mel_Array(Mel_Render_Handle) Mel_Manual_Remove_List;

typedef struct {
    Mel_Manual_Add_List pending_adds;
    Mel_Manual_Remove_List pending_removes;
    bool has_pending;
} Mel_Manual_Source_Data;

static Mel_Manual_Pending_Add* manual_find_pending_add(Mel_Manual_Source_Data* data, Mel_Render_Handle h)
{
    for (usize i = 0; i < data->pending_adds.count; i++)
    {
        if (mel_render_handle_eq(data->pending_adds.items[i].handle, h))
            return &data->pending_adds.items[i];
    }

    return nullptr;
}

static void manual_sync(Mel_Render_Source* self, Mel_Render_Manager* mgr)
{
    Mel_Manual_Source_Data* data = mel_render_source_instance(self);

    if (!data->has_pending)
        return;

    for (usize i = 0; i < data->pending_removes.count; i++)
        mel_mgr_free(mgr, data->pending_removes.items[i]);
    data->pending_removes.count = 0;

    for (usize i = 0; i < data->pending_adds.count; i++)
    {
        Mel_Manual_Pending_Add* add = &data->pending_adds.items[i];
        Mel_Render_Object object = {0};
        object.kind = MEL_RENDER_OBJECT_MESH_3D;
        object.material_base_id = add->info.material_base_id;
        object.material_idx = add->info.material_idx;
        object.flags = add->info.flags;
        object.layer_mask = add->info.layer_mask;
        object.mesh = add->info.mesh;
        object.bounds = add->bounds;
        object.mesh3d.model = add->transform;
        object.mesh3d.model_inverse = mel_mat4_inverse(add->transform);
        mel_mgr_set_object(mgr, add->handle, &object);
    }
    data->pending_adds.count = 0;

    data->has_pending = false;
}

static void manual_shutdown(Mel_Render_Source* self)
{
    Mel_Manual_Source_Data* data = mel_render_source_instance(self);
    mel_array_free(&data->pending_adds);
    mel_array_free(&data->pending_removes);
}

const Mel_Render_Source_Type mel_source_manual_type = {
    .name = { .data = (u8*)"manual", .len = 6 },
    .sync = manual_sync,
    .shutdown = manual_shutdown,
    .instance_size = sizeof(Mel_Manual_Source_Data),
};

Mel_Render_Source* mel_source_manual_create(const Mel_Alloc* alloc)
{
    if (!alloc) alloc = mel_alloc_heap();

    Mel_Render_Source* source = mel_render_source_create(
        .type = &mel_source_manual_type,
        .alloc = alloc);

    Mel_Manual_Source_Data* data = mel_render_source_instance(source);
    mel_array_init(&data->pending_adds, alloc);
    mel_array_init(&data->pending_removes, alloc);

    return source;
}

Mel_Render_Handle mel_source_manual_add(Mel_Render_Source* source,
                                         Mel_Mat4 transform,
                                         Mel_Render_Bounds bounds,
                                         Mel_Render_Info info)
{
    assert(source != nullptr);
    assert(source->scene != nullptr);

    Mel_Manual_Source_Data* data = mel_render_source_instance(source);
    Mel_Render_Manager* mgr = mel_render_scene_manager(source->scene);

    Mel_Render_Handle h = mel_mgr_alloc(mgr);

    mel_array_push(&data->pending_adds, ((Mel_Manual_Pending_Add){
        .handle = h,
        .transform = transform,
        .bounds = bounds,
        .info = info,
    }));

    data->has_pending = true;
    return h;
}

void mel_source_manual_remove(Mel_Render_Source* source, Mel_Render_Handle h)
{
    assert(source != nullptr);

    Mel_Manual_Source_Data* data = mel_render_source_instance(source);
    Mel_Manual_Pending_Add* pending = manual_find_pending_add(data, h);
    if (pending != nullptr)
    {
        usize idx = (usize)(pending - data->pending_adds.items);
        data->pending_adds.items[idx] = data->pending_adds.items[data->pending_adds.count - 1];
        data->pending_adds.count--;

        if (source->scene != nullptr)
            mel_mgr_free(mel_render_scene_manager(source->scene), h);
        return;
    }

    mel_array_push(&data->pending_removes, h);
    data->has_pending = true;
}

void mel_source_manual_set_transform(Mel_Render_Source* source,
                                      Mel_Render_Handle h, Mel_Mat4 transform)
{
    assert(source != nullptr);
    assert(source->scene != nullptr);

    Mel_Manual_Source_Data* data = mel_render_source_instance(source);
    Mel_Manual_Pending_Add* pending = manual_find_pending_add(data, h);
    if (pending != nullptr)
    {
        pending->transform = transform;
        return;
    }

    Mel_Render_Object* object = mel_mgr_get_object(mel_render_scene_manager(source->scene), h);
    assert(object != nullptr);
    assert(object->kind == MEL_RENDER_OBJECT_MESH_3D);
    object->mesh3d.model = transform;
    object->mesh3d.model_inverse = mel_mat4_inverse(transform);
    mel_mgr_mark_dirty(mel_render_scene_manager(source->scene), h);
}

void mel_source_manual_set_bounds(Mel_Render_Source* source,
                                   Mel_Render_Handle h, Mel_Render_Bounds bounds)
{
    assert(source != nullptr);
    assert(source->scene != nullptr);

    Mel_Manual_Source_Data* data = mel_render_source_instance(source);
    Mel_Manual_Pending_Add* pending = manual_find_pending_add(data, h);
    if (pending != nullptr)
    {
        pending->bounds = bounds;
        return;
    }

    Mel_Render_Object* object = mel_mgr_get_object(mel_render_scene_manager(source->scene), h);
    assert(object != nullptr);
    assert(object->kind == MEL_RENDER_OBJECT_MESH_3D);
    object->bounds = bounds;
    mel_mgr_mark_dirty(mel_render_scene_manager(source->scene), h);
}

void mel_source_manual_set_info(Mel_Render_Source* source,
                                 Mel_Render_Handle h, Mel_Render_Info info)
{
    assert(source != nullptr);
    assert(source->scene != nullptr);

    Mel_Manual_Source_Data* data = mel_render_source_instance(source);
    Mel_Manual_Pending_Add* pending = manual_find_pending_add(data, h);
    if (pending != nullptr)
    {
        pending->info = info;
        return;
    }

    Mel_Render_Object* object = mel_mgr_get_object(mel_render_scene_manager(source->scene), h);
    assert(object != nullptr);
    assert(object->kind == MEL_RENDER_OBJECT_MESH_3D);
    object->material_base_id = info.material_base_id;
    object->material_idx = info.material_idx;
    object->flags = info.flags;
    object->layer_mask = info.layer_mask;
    object->mesh = info.mesh;
    mel_mgr_mark_dirty(mel_render_scene_manager(source->scene), h);
}
