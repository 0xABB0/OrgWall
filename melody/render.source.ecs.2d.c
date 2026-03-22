#include "render.source.ecs.2d.h"
#include "render.source.type.h"
#include "render.manager.h"
#include "render.scene.h"
#include "render.pipeline.scene_forward.h"
#include "render.ecs.delta.h"
#include "ecs.2d.transform.h"
#include "ecs.2d.sprite.h"
#include "ecs.2d.text.h"
#include "sprite.material.h"
#include "font.descriptor.h"
#include "collection.hashmap.h"
#include "collection.array.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "math.vec2.h"

typedef struct {
    Mel_Render_Handle* handles;
    u32 count;
} Mel_Text_Glyph_Block;

typedef struct {
    Mel_Render_Handle handle;
    Mel_Rect uv;
    Mel_Vec4 color;
    u32 texture_idx;
    u32 layer;
} Mel_ECS_2D_Slot;

typedef Mel_Array(Mel_ECS_2D_Slot) Mel_ECS_2D_Slot_List;

typedef struct {
    Mel_ECS_Delta sprite_delta;
    Mel_ECS_Delta text_delta;
    Mel_HashMap entity_to_handle;
    Mel_HashMap text_entity_to_block;
    Mel_ECS_2D_Slot_List slots;
    ecs_world_t* world;
    const Mel_Alloc* alloc;
    bool has_text_delta;
} Mel_ECS_2D_Source_Data;

typedef struct {
    Mel_Render_Transform_2D transform;
} Mel_ECS_2D_Space;

static const Mel_Render_Space_Type s_ecs_2d_space_type = {
    .payload_size = sizeof(Mel_ECS_2D_Space),
};

static Mel_ECS_2D_Slot* ecs_2d_find_slot(Mel_ECS_2D_Source_Data* data, Mel_Render_Handle h)
{
    for (usize i = 0; i < data->slots.count; i++)
    {
        if (mel_render_handle_eq(data->slots.items[i].handle, h))
            return &data->slots.items[i];
    }
    return nullptr;
}

static void ecs_2d_store_slot(Mel_ECS_2D_Source_Data* data,
                              Mel_Render_Handle h,
                              Mel_Rect uv,
                              Mel_Vec4 color,
                              u32 texture_idx,
                              u32 layer)
{
    Mel_ECS_2D_Slot* slot = ecs_2d_find_slot(data, h);
    if (slot != nullptr)
    {
        slot->uv = uv;
        slot->color = color;
        slot->texture_idx = texture_idx;
        slot->layer = layer;
        return;
    }

    mel_array_push(&data->slots, ((Mel_ECS_2D_Slot){
        .handle = h,
        .uv = uv,
        .color = color,
        .texture_idx = texture_idx,
        .layer = layer,
    }));
}

static void ecs_2d_remove_slot(Mel_ECS_2D_Source_Data* data, Mel_Render_Handle h)
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

static void sync_sprite(Mel_Render_Source* self,
                        Mel_Render_Manager* mgr,
                        ecs_world_t* world,
                        ecs_entity_t entity,
                        Mel_Render_Handle h)
{
    Mel_ECS_2D_Source_Data* data = mel_render_source_instance(self);
    const Mel_CTransform* t = ecs_get(world, entity, Mel_CTransform);
    const Mel_Sprite* s = ecs_get(world, entity, Mel_Sprite);

    Mel_Render_Transform_2D transform = {
        .pos = t ? t->pos : MEL_VEC2_ZERO,
        .scale = s ? s->size : MEL_VEC2_ONE,
        .rotation = 0,
        .depth = t ? t->depth : 0.0f,
        .flags = 0,
    };

    Mel_ECS_2D_Slot* slot = ecs_2d_find_slot(data, h);
    Mel_Render_Space_Handle space = slot
        ? mel_mgr_get_instance(mgr, h)->space
        : mel_mgr_space_alloc(mgr, &s_ecs_2d_space_type);
    ((Mel_ECS_2D_Space*)mel_mgr_space_payload(mgr, space, &s_ecs_2d_space_type))->transform = transform;

    ecs_2d_store_slot(data, h,
        s ? s->uv : mel_rect(0, 0, 1, 1),
        s ? s->color : MEL_VEC4_ONE,
        0,
        s ? s->layer : 0);

    mel_mgr_set_instance(mgr, h, &(Mel_Render_Instance){
        .source = self,
        .space = space,
        .flags = 0,
        .visibility_mask = 0xFFFFFFFFu,
    });
    mel_mgr_set_material_bindings(mgr, h, &(Mel_Render_Material_Binding){
        .slot = 0,
        .material_base_id = mel_sprite_material_id(),
        .material_idx = 0,
        .flags = 0,
    }, 1);
}

static void expand_text(Mel_Render_Source* self,
                        Mel_Render_Manager* mgr,
                        Mel_Vec2 base_pos,
                        const Mel_CText* ct,
                        Mel_Text_Glyph_Block* block,
                        const Mel_Alloc* alloc)
{
    Mel_ECS_2D_Source_Data* data = mel_render_source_instance(self);
    u32 glyph_count = 0;
    for (size i = 0; i < ct->text.len; i++)
    {
        int c = ct->text.data[i];
        if (c == '\n') continue;
        if (c < (int)ct->desc->first_codepoint ||
            c >= (int)(ct->desc->first_codepoint + ct->desc->glyph_count))
            continue;
        glyph_count++;
    }

    if (glyph_count == 0)
    {
        block->handles = nullptr;
        block->count = 0;
        return;
    }

    block->handles = mel_alloc(alloc, glyph_count * sizeof(Mel_Render_Handle));
    block->count = glyph_count;

    f32 scale = ct->scale;
    f32 cursor_x = base_pos.x;
    f32 cursor_y = base_pos.y;
    u32 gi = 0;

    for (size i = 0; i < ct->text.len; i++)
    {
        int c = ct->text.data[i];
        if (c == '\n')
        {
            cursor_x = base_pos.x;
            cursor_y += ct->desc->line_height * scale;
            continue;
        }

        if (c < (int)ct->desc->first_codepoint ||
            c >= (int)(ct->desc->first_codepoint + ct->desc->glyph_count))
            continue;

        Mel_Font_Glyph* g = &ct->desc->glyphs[c - (int)ct->desc->first_codepoint];

        f32 gx = cursor_x + g->x0 * scale;
        f32 gy = cursor_y + g->y0 * scale;
        f32 gw = (g->x1 - g->x0) * scale;
        f32 gh = (g->y1 - g->y0) * scale;

        Mel_Render_Handle h = mel_mgr_alloc(mgr);
        Mel_Render_Space_Handle space = mel_mgr_space_alloc(mgr, &s_ecs_2d_space_type);
        Mel_Render_Transform_2D transform = {
            .pos = mel_vec2(gx + gw * 0.5f, gy + gh * 0.5f),
            .scale = mel_vec2(gw, gh),
            .rotation = 0.0f,
            .depth = 0.0f,
            .flags = 0,
        };
        ((Mel_ECS_2D_Space*)mel_mgr_space_payload(mgr, space, &s_ecs_2d_space_type))->transform = transform;

        ecs_2d_store_slot(data, h,
            mel_rect(g->u0, g->v0, g->u1 - g->u0, g->v1 - g->v0),
            ct->color,
            ct->texture_idx,
            0);

        mel_mgr_set_instance(mgr, h, &(Mel_Render_Instance){
            .source = self,
            .space = space,
            .flags = 0,
            .visibility_mask = 0xFFFFFFFFu,
        });
        mel_mgr_set_material_bindings(mgr, h, &(Mel_Render_Material_Binding){
            .slot = 0,
            .material_base_id = ct->material_id,
            .material_idx = ct->material_instance,
            .flags = 0,
        }, 1);

        block->handles[gi++] = h;
        cursor_x += g->xadvance * scale;
    }
}

static void free_text_block(Mel_Render_Manager* mgr,
                            Mel_ECS_2D_Source_Data* data,
                            Mel_Text_Glyph_Block* block,
                            const Mel_Alloc* alloc)
{
    for (u32 i = 0; i < block->count; i++)
    {
        Mel_Render_Instance* instance = mel_mgr_get_instance(mgr, block->handles[i]);
        if (mel_render_space_handle_valid(instance->space))
            mel_mgr_space_free(mgr, instance->space);
        ecs_2d_remove_slot(data, block->handles[i]);
        mel_mgr_free(mgr, block->handles[i]);
    }
    if (block->handles)
        mel_dealloc(alloc, block->handles);
    block->handles = nullptr;
    block->count = 0;
}

static void ecs_2d_sync(Mel_Render_Source* self, Mel_Render_Manager* mgr)
{
    Mel_ECS_2D_Source_Data* data = mel_render_source_instance(self);

    u32 sprite_removed_count = mel_ecs_delta_removed_count(&data->sprite_delta);
    const ecs_entity_t* sprite_removed = mel_ecs_delta_removed(&data->sprite_delta);
    for (u32 i = 0; i < sprite_removed_count; i++)
    {
        void* val = mel_hashmap_get(&data->entity_to_handle, (void*)(usize)sprite_removed[i]);
        if (val != nullptr)
        {
            Mel_Render_Handle h = mel_render_handle_unpack64((u64)(usize)val);
            Mel_Render_Instance* instance = mel_mgr_get_instance(mgr, h);
            if (mel_render_space_handle_valid(instance->space))
                mel_mgr_space_free(mgr, instance->space);
            ecs_2d_remove_slot(data, h);
            mel_mgr_free(mgr, h);
            mel_hashmap_remove(&data->entity_to_handle, (void*)(usize)sprite_removed[i]);
        }
    }

    u32 sprite_added_count = mel_ecs_delta_added_count(&data->sprite_delta);
    const ecs_entity_t* sprite_added = mel_ecs_delta_added(&data->sprite_delta);
    for (u32 i = 0; i < sprite_added_count; i++)
    {
        Mel_Render_Handle h = mel_mgr_alloc(mgr);
        u64 packed = mel_render_handle_pack64(h);
        mel_hashmap_put(&data->entity_to_handle,
            (void*)(usize)sprite_added[i], (void*)(usize)packed);
        sync_sprite(self, mgr, data->world, sprite_added[i], h);
    }

    u32 sprite_modified_count = mel_ecs_delta_modified_count(&data->sprite_delta);
    const ecs_entity_t* sprite_modified = mel_ecs_delta_modified(&data->sprite_delta);
    for (u32 i = 0; i < sprite_modified_count; i++)
    {
        void* val = mel_hashmap_get(&data->entity_to_handle, (void*)(usize)sprite_modified[i]);
        if (val == nullptr) continue;
        Mel_Render_Handle h = mel_render_handle_unpack64((u64)(usize)val);
        sync_sprite(self, mgr, data->world, sprite_modified[i], h);
    }

    mel_ecs_delta_begin_frame(&data->sprite_delta);

    if (!data->has_text_delta)
        return;

    u32 text_removed_count = mel_ecs_delta_removed_count(&data->text_delta);
    const ecs_entity_t* text_removed = mel_ecs_delta_removed(&data->text_delta);
    for (u32 i = 0; i < text_removed_count; i++)
    {
        void* val = mel_hashmap_get(&data->text_entity_to_block, (void*)(usize)text_removed[i]);
        if (val != nullptr)
        {
            Mel_Text_Glyph_Block* block = (Mel_Text_Glyph_Block*)val;
            free_text_block(mgr, data, block, data->alloc);
            mel_dealloc(data->alloc, block);
            mel_hashmap_remove(&data->text_entity_to_block, (void*)(usize)text_removed[i]);
        }
    }

    u32 text_added_count = mel_ecs_delta_added_count(&data->text_delta);
    const ecs_entity_t* text_added = mel_ecs_delta_added(&data->text_delta);
    for (u32 i = 0; i < text_added_count; i++)
    {
        const Mel_CTransform* t = ecs_get(data->world, text_added[i], Mel_CTransform);
        const Mel_CText* ct = ecs_get(data->world, text_added[i], Mel_CText);
        if (!t || !ct || !ct->desc) continue;

        Mel_Text_Glyph_Block* block = mel_alloc(data->alloc, sizeof(Mel_Text_Glyph_Block));
        expand_text(self, mgr, t->pos, ct, block, data->alloc);
        mel_hashmap_put(&data->text_entity_to_block,
            (void*)(usize)text_added[i], block);
    }

    u32 text_modified_count = mel_ecs_delta_modified_count(&data->text_delta);
    const ecs_entity_t* text_modified = mel_ecs_delta_modified(&data->text_delta);
    for (u32 i = 0; i < text_modified_count; i++)
    {
        void* val = mel_hashmap_get(&data->text_entity_to_block, (void*)(usize)text_modified[i]);
        if (val == nullptr) continue;

        Mel_Text_Glyph_Block* block = (Mel_Text_Glyph_Block*)val;
        free_text_block(mgr, data, block, data->alloc);

        const Mel_CTransform* t = ecs_get(data->world, text_modified[i], Mel_CTransform);
        const Mel_CText* ct = ecs_get(data->world, text_modified[i], Mel_CText);
        if (!t || !ct || !ct->desc) continue;

        expand_text(self, mgr, t->pos, ct, block, data->alloc);
    }

    mel_ecs_delta_begin_frame(&data->text_delta);
}

static void ecs_2d_scene_forward_emit(Mel_Render_Source* self,
                                      Mel_Render_Handle h,
                                      const Mel_Render_Instance* instance,
                                      Mel_Scene_Forward_Emitter* emitter)
{
    Mel_ECS_2D_Source_Data* data = mel_render_source_instance(self);
    Mel_ECS_2D_Slot* slot = ecs_2d_find_slot(data, h);
    if (slot == nullptr)
        return;

    mel_scene_forward_emit_sprite(emitter, &(Mel_Scene_Forward_Sprite){
        .transform = ((Mel_ECS_2D_Space*)mel_mgr_space_payload(
            mel_render_scene_manager(self->scene), instance->space, &s_ecs_2d_space_type))->transform,
        .uv = slot->uv,
        .color = slot->color,
        .texture_idx = slot->texture_idx,
        .material_binding_index = 0,
    });
    (void)instance;
}

static void ecs_2d_shutdown(Mel_Render_Source* self)
{
    Mel_ECS_2D_Source_Data* data = mel_render_source_instance(self);
    mel_ecs_delta_shutdown(&data->sprite_delta);
    if (data->has_text_delta)
        mel_ecs_delta_shutdown(&data->text_delta);
    mel_hashmap_free(&data->entity_to_handle);
    mel_hashmap_free(&data->text_entity_to_block);
    mel_array_free(&data->slots);
}

const Mel_Render_Source_Type mel_source_ecs_2d_type = {
    .name = { .data = (u8*)"ecs_2d", .len = 6 },
    .sync = ecs_2d_sync,
    .scene_forward_emit = ecs_2d_scene_forward_emit,
    .shutdown = ecs_2d_shutdown,
    .instance_size = sizeof(Mel_ECS_2D_Source_Data),
};

Mel_Render_Source* mel_source_ecs_2d_create_opt(Mel_Source_ECS_2D_Opt opt)
{
    assert(opt.world != nullptr);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    Mel_Render_Source* source = mel_render_source_create(
        .type = &mel_source_ecs_2d_type,
        .alloc = alloc);

    Mel_ECS_2D_Source_Data* data = mel_render_source_instance(source);
    data->world = opt.world;
    data->alloc = alloc;

    mel_ecs_delta_init(&data->sprite_delta,
        .world = opt.world,
        .components = { ecs_id(Mel_CTransform), ecs_id(Mel_Sprite) },
        .alloc = alloc);

    if (ecs_id(Mel_CText) != 0)
    {
        mel_ecs_delta_init(&data->text_delta,
            .world = opt.world,
            .components = { ecs_id(Mel_CTransform), ecs_id(Mel_CText) },
            .alloc = alloc);
        data->has_text_delta = true;
    }

    mel_hashmap_init(&data->entity_to_handle,
        mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_hashmap_init(&data->text_entity_to_block,
        mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_array_init(&data->slots, alloc);

    return source;
}

Mel_Render_Handle mel_source_ecs_2d_handle_for_entity(Mel_Render_Source* source, ecs_entity_t entity)
{
    assert(source != nullptr);
    assert(source->type == &mel_source_ecs_2d_type);

    Mel_ECS_2D_Source_Data* data = mel_render_source_instance(source);
    void* val = mel_hashmap_get(&data->entity_to_handle, (void*)(usize)entity);
    if (val == nullptr)
        return MEL_RENDER_HANDLE_NONE;

    return mel_render_handle_unpack64((u64)(usize)val);
}

bool mel_source_ecs_2d_get_sprite_payload(Mel_Render_Source* source,
                                          Mel_Render_Handle h,
                                          Mel_Render_Transform_2D* transform,
                                          Mel_Render_Sprite_Info* info)
{
    assert(source != nullptr);
    assert(source->type == &mel_source_ecs_2d_type);

    Mel_ECS_2D_Source_Data* data = mel_render_source_instance(source);
    Mel_ECS_2D_Slot* slot = ecs_2d_find_slot(data, h);
    if (slot == nullptr)
        return false;

    Mel_Render_Instance* instance = source->scene
        ? mel_mgr_get_instance(mel_render_scene_manager(source->scene), h)
        : nullptr;

    if (transform)
    {
        if (instance == nullptr)
            return false;
        *transform = ((Mel_ECS_2D_Space*)mel_mgr_space_payload(
            mel_render_scene_manager(source->scene), instance->space, &s_ecs_2d_space_type))->transform;
    }

    if (info)
    {
        const Mel_Render_Material_Binding* bindings = nullptr;
        u32 binding_count = 0;
        if (source->scene)
            bindings = mel_mgr_get_material_bindings(mel_render_scene_manager(source->scene), h, &binding_count);

        *info = (Mel_Render_Sprite_Info){
            .uv = slot->uv,
            .color = slot->color,
            .texture_idx = slot->texture_idx,
            .material_base_id = (instance && bindings && binding_count > 0) ? bindings[0].material_base_id : 0,
            .layer = (instance && bindings && binding_count > 0) ? bindings[0].material_idx : 0,
        };
    }

    return true;
}
