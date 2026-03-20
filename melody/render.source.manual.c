#include "render.source.manual.h"
#include "render.source.type.h"
#include "render.scene.h"
#include "render.manager.h"
#include "render.pipeline.scene_forward.h"
#include "render.types.3d.h"
#include "collection.array.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "math.mat4.h"

typedef struct {
    Mel_Mat4 transform;
    Mel_Render_Bounds bounds;
    Mel_Geometry_Handle mesh;
} Mel_Manual_Payload;

typedef struct {
    Mel_Render_Handle handle;
    Mel_Mat4 transform;
    Mel_Render_Bounds bounds;
    Mel_Render_Info info;
} Mel_Manual_Pending_Add;

typedef struct {
    Mel_Render_Handle handle;
    Mel_Manual_Payload payload;
} Mel_Manual_Slot;

typedef Mel_Array(Mel_Manual_Pending_Add) Mel_Manual_Add_List;
typedef Mel_Array(Mel_Render_Handle) Mel_Manual_Remove_List;
typedef Mel_Array(Mel_Manual_Slot) Mel_Manual_Slot_List;

typedef struct {
    Mel_Manual_Add_List pending_adds;
    Mel_Manual_Remove_List pending_removes;
    Mel_Manual_Slot_List slots;
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

static Mel_Manual_Slot* manual_find_slot(Mel_Manual_Source_Data* data, Mel_Render_Handle h)
{
    for (usize i = 0; i < data->slots.count; i++)
    {
        if (mel_render_handle_eq(data->slots.items[i].handle, h))
            return &data->slots.items[i];
    }

    return nullptr;
}

static void manual_store_payload(Mel_Manual_Source_Data* data, Mel_Render_Handle h, Mel_Manual_Payload payload)
{
    Mel_Manual_Slot* slot = manual_find_slot(data, h);
    if (slot != nullptr)
    {
        slot->payload = payload;
        return;
    }

    mel_array_push(&data->slots, ((Mel_Manual_Slot){
        .handle = h,
        .payload = payload,
    }));
}

static void manual_remove_payload(Mel_Manual_Source_Data* data, Mel_Render_Handle h)
{
    for (usize i = 0; i < data->slots.count; i++)
    {
        if (!mel_render_handle_eq(data->slots.items[i].handle, h))
            continue;

        data->slots.items[i] = data->slots.items[data->slots.count - 1];
        data->slots.count--;
        return;
    }
}

static void manual_sync(Mel_Render_Source* self, Mel_Render_Manager* mgr)
{
    Mel_Manual_Source_Data* data = mel_render_source_instance(self);

    if (!data->has_pending)
        return;

    for (usize i = 0; i < data->pending_removes.count; i++)
    {
        Mel_Render_Handle h = data->pending_removes.items[i];
        manual_remove_payload(data, h);
        mel_mgr_free(mgr, h);
    }
    data->pending_removes.count = 0;

    for (usize i = 0; i < data->pending_adds.count; i++)
    {
        Mel_Manual_Pending_Add* add = &data->pending_adds.items[i];
        manual_store_payload(data, add->handle, (Mel_Manual_Payload){
            .transform = add->transform,
            .bounds = add->bounds,
            .mesh = add->info.mesh,
        });

        mel_mgr_set_instance(mgr, add->handle, &(Mel_Render_Instance){
            .source = self,
            .material_base_id = add->info.material_base_id,
            .material_idx = add->info.material_idx,
            .flags = add->info.flags,
            .visibility_mask = add->info.layer_mask,
        });
    }
    data->pending_adds.count = 0;

    data->has_pending = false;
}

static void manual_scene_forward_emit(Mel_Render_Source* self,
                                      Mel_Render_Handle h,
                                      const Mel_Render_Instance* instance,
                                      Mel_Scene_Forward_Emitter* emitter)
{
    Mel_Manual_Source_Data* data = mel_render_source_instance(self);
    Mel_Manual_Slot* slot = manual_find_slot(data, h);
    if (slot == nullptr)
        return;

    mel_scene_forward_emit_mesh(emitter, &(Mel_Scene_Forward_Mesh){
        .model = slot->payload.transform,
        .bounds = slot->payload.bounds,
        .mesh = slot->payload.mesh,
    });
    (void)instance;
}

static void manual_shutdown(Mel_Render_Source* self)
{
    Mel_Manual_Source_Data* data = mel_render_source_instance(self);
    mel_array_free(&data->pending_adds);
    mel_array_free(&data->pending_removes);
    mel_array_free(&data->slots);
}

const Mel_Render_Source_Type mel_source_manual_type = {
    .name = { .data = (u8*)"manual", .len = 6 },
    .sync = manual_sync,
    .scene_forward_emit = manual_scene_forward_emit,
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
    mel_array_init(&data->slots, alloc);

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

    Mel_Manual_Slot* slot = manual_find_slot(data, h);
    assert(slot != nullptr);
    slot->payload.transform = transform;
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

    Mel_Manual_Slot* slot = manual_find_slot(data, h);
    assert(slot != nullptr);
    slot->payload.bounds = bounds;
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

    Mel_Manual_Slot* slot = manual_find_slot(data, h);
    assert(slot != nullptr);
    slot->payload.mesh = info.mesh;

    mel_mgr_set_instance(mel_render_scene_manager(source->scene), h, &(Mel_Render_Instance){
        .source = source,
        .material_base_id = info.material_base_id,
        .material_idx = info.material_idx,
        .flags = info.flags,
        .visibility_mask = info.layer_mask,
    });
}
