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

#include <string.h>

typedef struct {
    Mel_Render_Bounds bounds;
    Mel_Render_Mesh_Part* parts;
    u32 part_count;
    u32 part_capacity;
} Mel_Manual_Payload;

typedef struct {
    Mel_Render_Handle handle;
    Mel_Render_Space_Handle space;
    Mel_Render_Bounds bounds;
    Mel_Render_Info info;
    Mel_Render_Mesh_Part* parts;
    u32 part_count;
    u32 part_capacity;
    Mel_Render_Material_Binding* bindings;
    u32 binding_count;
    u32 binding_capacity;
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
    const Mel_Alloc* alloc;
    bool has_pending;
} Mel_Manual_Source_Data;

typedef struct {
    Mel_Mat4 model;
} Mel_Manual_Space;

static const Mel_Render_Space_Type s_manual_space_type = {
    .payload_size = sizeof(Mel_Manual_Space),
};

static void manual_set_parts(Mel_Manual_Payload* payload, const Mel_Alloc* alloc,
                             const Mel_Render_Mesh_Part* parts, u32 part_count)
{
    assert(payload != nullptr);
    assert(part_count == 0 || parts != nullptr);

    if (part_count > payload->part_capacity)
    {
        u32 new_capacity = payload->part_capacity ? payload->part_capacity : 1;
        while (new_capacity < part_count)
            new_capacity *= 2;

        payload->parts = payload->parts
            ? mel_realloc(alloc, payload->parts, (usize)new_capacity * sizeof(Mel_Render_Mesh_Part))
            : mel_alloc(alloc, (usize)new_capacity * sizeof(Mel_Render_Mesh_Part));
        payload->part_capacity = new_capacity;
    }

    if (part_count > 0)
        memcpy(payload->parts, parts, (usize)part_count * sizeof(Mel_Render_Mesh_Part));
    payload->part_count = part_count;
}

static void manual_set_pending_parts(Mel_Manual_Pending_Add* pending, const Mel_Alloc* alloc,
                                     const Mel_Render_Mesh_Part* parts, u32 part_count)
{
    assert(pending != nullptr);
    assert(part_count == 0 || parts != nullptr);

    if (part_count > pending->part_capacity)
    {
        u32 new_capacity = pending->part_capacity ? pending->part_capacity : 1;
        while (new_capacity < part_count)
            new_capacity *= 2;

        pending->parts = pending->parts
            ? mel_realloc(alloc, pending->parts, (usize)new_capacity * sizeof(Mel_Render_Mesh_Part))
            : mel_alloc(alloc, (usize)new_capacity * sizeof(Mel_Render_Mesh_Part));
        pending->part_capacity = new_capacity;
    }

    if (part_count > 0)
        memcpy(pending->parts, parts, (usize)part_count * sizeof(Mel_Render_Mesh_Part));
    pending->part_count = part_count;
}

static void manual_set_pending_bindings(Mel_Manual_Pending_Add* pending, const Mel_Alloc* alloc,
                                        const Mel_Render_Material_Binding* bindings, u32 binding_count)
{
    assert(pending != nullptr);
    assert(binding_count == 0 || bindings != nullptr);

    if (binding_count > pending->binding_capacity)
    {
        u32 new_capacity = pending->binding_capacity ? pending->binding_capacity : 1;
        while (new_capacity < binding_count)
            new_capacity *= 2;

        pending->bindings = pending->bindings
            ? mel_realloc(alloc, pending->bindings, (usize)new_capacity * sizeof(Mel_Render_Material_Binding))
            : mel_alloc(alloc, (usize)new_capacity * sizeof(Mel_Render_Material_Binding));
        pending->binding_capacity = new_capacity;
    }

    if (binding_count > 0)
        memcpy(pending->bindings, bindings, (usize)binding_count * sizeof(Mel_Render_Material_Binding));
    pending->binding_count = binding_count;
}

static void manual_free_payload(Mel_Manual_Payload* payload, const Mel_Alloc* alloc)
{
    if (payload->parts != nullptr)
        mel_dealloc(alloc, payload->parts);
    *payload = (Mel_Manual_Payload){0};
}

static void manual_free_pending_add(Mel_Manual_Pending_Add* pending, const Mel_Alloc* alloc)
{
    if (pending->parts != nullptr)
        mel_dealloc(alloc, pending->parts);
    if (pending->bindings != nullptr)
        mel_dealloc(alloc, pending->bindings);
    *pending = (Mel_Manual_Pending_Add){0};
}

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
        manual_free_payload(&slot->payload, data->alloc);
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

        manual_free_payload(&data->slots.items[i].payload, data->alloc);
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
        Mel_Render_Instance* instance = mel_mgr_get_instance(mgr, h);
        if (mel_render_space_handle_valid(instance->space))
            mel_mgr_space_free(mgr, instance->space);
        manual_remove_payload(data, h);
        mel_mgr_free(mgr, h);
    }
    data->pending_removes.count = 0;

    for (usize i = 0; i < data->pending_adds.count; i++)
    {
        Mel_Manual_Pending_Add* add = &data->pending_adds.items[i];
        Mel_Manual_Payload payload = {
            .bounds = add->bounds,
        };
        if (add->part_count > 0)
            manual_set_parts(&payload, self->alloc, add->parts, add->part_count);
        else
        {
            Mel_Render_Mesh_Part part = {
                .mesh = add->info.mesh,
                .material_binding_index = 0,
                .flags = 0,
            };
            manual_set_parts(&payload, self->alloc, &part, 1);
        }
        manual_store_payload(data, add->handle, payload);

        mel_mgr_set_instance(mgr, add->handle, &(Mel_Render_Instance){
            .source = self,
            .space = add->space,
            .flags = add->info.flags,
            .visibility_mask = add->info.layer_mask,
        });
        if (add->binding_count > 0)
            mel_mgr_set_material_bindings(mgr, add->handle, add->bindings, add->binding_count);
        else
            mel_mgr_set_material_bindings(mgr, add->handle, &(Mel_Render_Material_Binding){
                .slot = 0,
                .material_base_id = add->info.material_base_id,
                .material_idx = add->info.material_idx,
                .flags = 0,
            }, 1);
        manual_free_pending_add(add, self->alloc);
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
    Mel_Render_Manager* mgr = mel_render_scene_manager(self->scene);
    Mel_Manual_Space* space = mel_mgr_space_payload(mgr, instance->space, &s_manual_space_type);

    for (u32 i = 0; i < slot->payload.part_count; i++)
    {
        Mel_Render_Mesh_Part* part = &slot->payload.parts[i];
        mel_scene_forward_emit_mesh(emitter, &(Mel_Scene_Forward_Mesh){
            .model = space->model,
            .bounds = slot->payload.bounds,
            .mesh = part->mesh,
            .material_binding_index = part->material_binding_index,
        });
    }
    (void)instance;
}

static void manual_shutdown(Mel_Render_Source* self)
{
    Mel_Manual_Source_Data* data = mel_render_source_instance(self);
    for (usize i = 0; i < data->pending_adds.count; i++)
        manual_free_pending_add(&data->pending_adds.items[i], self->alloc);
    mel_array_free(&data->pending_adds);
    mel_array_free(&data->pending_removes);
    for (usize i = 0; i < data->slots.count; i++)
        manual_free_payload(&data->slots.items[i].payload, self->alloc);
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
    data->alloc = alloc;
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
    Mel_Render_Space_Handle space = mel_mgr_space_alloc(mgr, &s_manual_space_type);
    ((Mel_Manual_Space*)mel_mgr_space_payload(mgr, space, &s_manual_space_type))->model = transform;

    mel_array_push(&data->pending_adds, ((Mel_Manual_Pending_Add){
        .handle = h,
        .space = space,
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
        Mel_Render_Space_Handle pending_space = pending->space;
        manual_free_pending_add(pending, source->alloc);
        data->pending_adds.items[idx] = data->pending_adds.items[data->pending_adds.count - 1];
        data->pending_adds.count--;

        if (source->scene != nullptr)
        {
            Mel_Render_Manager* mgr = mel_render_scene_manager(source->scene);
            mel_mgr_space_free(mgr, pending_space);
            mel_mgr_free(mgr, h);
        }
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
        Mel_Render_Manager* mgr = mel_render_scene_manager(source->scene);
        ((Mel_Manual_Space*)mel_mgr_space_payload(mgr, pending->space, &s_manual_space_type))->model = transform;
        return;
    }

    Mel_Manual_Slot* slot = manual_find_slot(data, h);
    assert(slot != nullptr);
    Mel_Render_Manager* mgr = mel_render_scene_manager(source->scene);
    Mel_Render_Instance* instance = mel_mgr_get_instance(mgr, h);
    ((Mel_Manual_Space*)mel_mgr_space_payload(mgr, instance->space, &s_manual_space_type))->model = transform;
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
    if (slot->payload.part_count > 0)
        slot->payload.parts[0].mesh = info.mesh;

    Mel_Render_Manager* mgr = mel_render_scene_manager(source->scene);
    Mel_Render_Instance* instance = mel_mgr_get_instance(mgr, h);
    mel_mgr_set_instance(mgr, h, &(Mel_Render_Instance){
        .source = source,
        .space = instance->space,
        .flags = info.flags,
        .visibility_mask = info.layer_mask,
    });
    mel_mgr_set_material_bindings(mgr, h, &(Mel_Render_Material_Binding){
        .slot = 0,
        .material_base_id = info.material_base_id,
        .material_idx = info.material_idx,
        .flags = 0,
    }, 1);
}

void mel_source_manual_set_mesh_parts(Mel_Render_Source* source,
                                      Mel_Render_Handle h,
                                      const Mel_Render_Mesh_Part* parts,
                                      u32 part_count)
{
    assert(source != nullptr);
    assert(source->scene != nullptr);
    assert(part_count > 0);

    Mel_Manual_Source_Data* data = mel_render_source_instance(source);
    Mel_Manual_Pending_Add* pending = manual_find_pending_add(data, h);
    if (pending != nullptr)
    {
        pending->info.mesh = parts[0].mesh;
        manual_set_pending_parts(pending, source->alloc, parts, part_count);
        return;
    }

    Mel_Manual_Slot* slot = manual_find_slot(data, h);
    assert(slot != nullptr);
    manual_set_parts(&slot->payload, source->alloc, parts, part_count);
    mel_mgr_mark_dirty(mel_render_scene_manager(source->scene), h);
}

void mel_source_manual_set_material_bindings(Mel_Render_Source* source,
                                             Mel_Render_Handle h,
                                             const Mel_Render_Material_Binding* bindings,
                                             u32 binding_count)
{
    assert(source != nullptr);
    assert(source->scene != nullptr);
    Mel_Render_Manager* mgr = mel_render_scene_manager(source->scene);
    Mel_Manual_Source_Data* data = mel_render_source_instance(source);
    Mel_Manual_Pending_Add* pending = manual_find_pending_add(data, h);
    if (pending != nullptr)
    {
        manual_set_pending_bindings(pending, source->alloc, bindings, binding_count);
        return;
    }
    mel_mgr_set_material_bindings(mgr, h, bindings, binding_count);
    mel_mgr_mark_dirty(mgr, h);
}
