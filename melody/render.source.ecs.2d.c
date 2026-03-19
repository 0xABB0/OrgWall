#include "render.source.ecs.2d.h"
#include "render.source.type.h"
#include "render.manager.h"
#include "render.types.2d.h"
#include "render.ecs.delta.h"
#include "ecs.2d.transform.h"
#include "ecs.2d.sprite.h"
#include "ecs.2d.text.h"
#include "sprite.material.h"
#include "font.descriptor.h"
#include "collection.hashmap.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "math.vec2.h"

typedef struct {
    Mel_Render_Handle* handles;
    u32 count;
} Mel_Text_Glyph_Block;

typedef struct {
    Mel_ECS_Delta sprite_delta;
    Mel_ECS_Delta text_delta;
    Mel_HashMap entity_to_handle;
    Mel_HashMap text_entity_to_block;
    ecs_world_t* world;
    const Mel_Alloc* alloc;
    bool has_text_delta;
} Mel_ECS_2D_Source_Data;


static void sync_sprite(Mel_Render_Manager* mgr, ecs_world_t* world,
                         ecs_entity_t entity, Mel_Render_Handle h)
{
    const Mel_CTransform* t = ecs_get(world, entity, Mel_CTransform);
    const Mel_Sprite* s = ecs_get(world, entity, Mel_Sprite);

    Mel_Render_Transform_2D transform = {
        .pos = t ? t->pos : MEL_VEC2_ZERO,
        .scale = s ? s->size : MEL_VEC2_ONE,
        .rotation = 0,
        .depth = 0.5f,
        .flags = 0,
    };

    Mel_Render_Sprite_Info info = {
        .uv = s ? s->uv : mel_rect(0, 0, 1, 1),
        .color = s ? s->color : MEL_VEC4_ONE,
        .texture_idx = 0,
        .material_base_id = mel_sprite_material_id(),
        .layer = 0,
    };

    mel_mgr_set(mgr, MEL_2D_POOL_TRANSFORMS, h, &transform);
    mel_mgr_set(mgr, MEL_2D_POOL_INFOS, h, &info);
}

static void expand_text(Mel_Render_Manager* mgr, Mel_Vec2 base_pos,
                         const Mel_CText* ct, Mel_Text_Glyph_Block* block,
                         const Mel_Alloc* alloc)
{
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

        Mel_Render_Handle h = mel_mgr_alloc(mgr, ct->material_id);

        Mel_Render_Transform_2D transform = {
            .pos = mel_vec2(gx + gw * 0.5f, gy + gh * 0.5f),
            .scale = mel_vec2(gw, gh),
            .depth = 0.0f,
        };

        Mel_Render_Sprite_Info info = {
            .uv = mel_rect(g->u0, g->v0, g->u1 - g->u0, g->v1 - g->v0),
            .color = ct->color,
            .texture_idx = ct->texture_idx,
            .material_base_id = ct->material_id,
            .layer = ct->material_instance,
        };

        mel_mgr_set(mgr, MEL_2D_POOL_TRANSFORMS, h, &transform);
        mel_mgr_set(mgr, MEL_2D_POOL_INFOS, h, &info);

        block->handles[gi++] = h;
        cursor_x += g->xadvance * scale;
    }
}

static void free_text_block(Mel_Render_Manager* mgr, Mel_Text_Glyph_Block* block,
                              const Mel_Alloc* alloc)
{
    for (u32 i = 0; i < block->count; i++)
        mel_mgr_free(mgr, block->handles[i]);
    if (block->handles)
        mel_dealloc(alloc, block->handles);
    block->handles = nullptr;
    block->count = 0;
}

static void* ecs_2d_create_manager(Mel_Render_Source* self, Mel_Gpu_Device* dev, const Mel_Alloc* alloc)
{
    (void)self;
    Mel_Render_Manager* mgr = mel_alloc(alloc, sizeof(Mel_Render_Manager));
    Mel_Mgr_Pool_Desc pools[] = {
        { .item_size = sizeof(Mel_Render_Transform_2D) },
        { .item_size = sizeof(Mel_Render_Sprite_Info) },
    };
    mel_mgr_init(mgr, .dev = dev, .alloc = alloc, .pools = pools, .pool_count = MEL_2D_POOL_COUNT);
    return mgr;
}

static void ecs_2d_destroy_manager(Mel_Render_Source* self, void* mgr)
{
    (void)self;
    Mel_Render_Manager* m = mgr;
    mel_mgr_shutdown(m);
    mel_dealloc(mel_alloc_heap(), m);
}

static void ecs_2d_sync(Mel_Render_Source* self, void* mgr)
{
    Mel_ECS_2D_Source_Data* data = mel_render_source_instance(self);
    Mel_Render_Manager* m = mgr;

    u32 sprite_removed_count = mel_ecs_delta_removed_count(&data->sprite_delta);
    const ecs_entity_t* sprite_removed = mel_ecs_delta_removed(&data->sprite_delta);
    for (u32 i = 0; i < sprite_removed_count; i++)
    {
        void* val = mel_hashmap_get(&data->entity_to_handle, (void*)(usize)sprite_removed[i]);
        if (val != nullptr)
        {
            Mel_Render_Handle h = mel_render_handle_unpack64((u64)(usize)val);
            mel_mgr_free(m, h);
            mel_hashmap_remove(&data->entity_to_handle, (void*)(usize)sprite_removed[i]);
        }
    }

    u32 sprite_added_count = mel_ecs_delta_added_count(&data->sprite_delta);
    const ecs_entity_t* sprite_added = mel_ecs_delta_added(&data->sprite_delta);
    for (u32 i = 0; i < sprite_added_count; i++)
    {
        Mel_Render_Handle h = mel_mgr_alloc(m, mel_sprite_material_id());
        u64 packed = mel_render_handle_pack64(h);
        mel_hashmap_put(&data->entity_to_handle,
            (void*)(usize)sprite_added[i], (void*)(usize)packed);
        sync_sprite(m, data->world, sprite_added[i], h);
    }

    u32 sprite_modified_count = mel_ecs_delta_modified_count(&data->sprite_delta);
    const ecs_entity_t* sprite_modified = mel_ecs_delta_modified(&data->sprite_delta);
    for (u32 i = 0; i < sprite_modified_count; i++)
    {
        void* val = mel_hashmap_get(&data->entity_to_handle, (void*)(usize)sprite_modified[i]);
        if (val == nullptr) continue;
        Mel_Render_Handle h = mel_render_handle_unpack64((u64)(usize)val);
        sync_sprite(m, data->world, sprite_modified[i], h);
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
            free_text_block(m, block, data->alloc);
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
        expand_text(m, t->pos, ct, block, data->alloc);
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
        free_text_block(m, block, data->alloc);

        const Mel_CTransform* t = ecs_get(data->world, text_modified[i], Mel_CTransform);
        const Mel_CText* ct = ecs_get(data->world, text_modified[i], Mel_CText);
        if (!t || !ct || !ct->desc) continue;

        expand_text(m, t->pos, ct, block, data->alloc);
    }

    mel_ecs_delta_begin_frame(&data->text_delta);
}

static void ecs_2d_shutdown(Mel_Render_Source* self)
{
    Mel_ECS_2D_Source_Data* data = mel_render_source_instance(self);
    mel_ecs_delta_shutdown(&data->sprite_delta);
    if (data->has_text_delta)
        mel_ecs_delta_shutdown(&data->text_delta);
    mel_hashmap_free(&data->entity_to_handle);
    mel_hashmap_free(&data->text_entity_to_block);
}

const Mel_Render_Source_Type mel_source_ecs_2d_type = {
    .name = { .data = (u8*)"ecs_2d", .len = 6 },
    .create_manager = ecs_2d_create_manager,
    .destroy_manager = ecs_2d_destroy_manager,
    .sync = ecs_2d_sync,
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
