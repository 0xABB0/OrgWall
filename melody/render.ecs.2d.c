#include "render.ecs.2d.h"
#include "ecs.2d.transform.h"
#include "ecs.2d.sprite.h"
#include "ecs.2d.text.h"
#include "sprite.pass.h"
#include "text.draw.h"
#include "texture.pool.h"
#include "allocator.heap.h"
#include "string.str8.h"

static void mel__render_ecs_2d_write_sprite(void* entry_ptr, ecs_iter_t* it, i32 row, void* user)
{
    MEL_UNUSED(user);

    const Mel_CTransform* transform = ecs_field(it, Mel_CTransform, 0);
    const Mel_Sprite* sprite = ecs_field(it, Mel_Sprite, 1);
    Mel_Sprite_Entry* entry = entry_ptr;

    *entry = (Mel_Sprite_Entry){
        .pos = transform[row].pos,
        .size = sprite[row].size,
        .uv = sprite[row].uv.w > 0.0f ? sprite[row].uv : MEL_UV_FULL,
        .color = sprite[row].color,
        .tex = sprite[row].tex,
    };
}

static u64 mel__render_ecs_2d_sprite_key(ecs_iter_t* it, i32 row, void* user)
{
    MEL_UNUSED(user);
    const Mel_Sprite* sprite = ecs_field(it, Mel_Sprite, 1);
    return mel_sort_key_sprite(0, 0.0f, 0, mel_texture_bucket(sprite[row].tex));
}

bool mel_render_ecs_2d_init_opt(Mel_Render_ECS_2D* renderer, Mel_Render_ECS_2D_Opt opt)
{
    assert(renderer != nullptr);
    assert(opt.world != nullptr);
    assert(opt.world_camera != nullptr);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    Mel_Render_Stage_2D_Layer sprite_layer = opt.sprite_layer < MEL_RENDER_STAGE_2D_LAYER_COUNT
        ? opt.sprite_layer
        : MEL_RENDER_STAGE_2D_LAYER_WORLD;
    Mel_Render_Stage_2D_Layer text_layer = opt.text_layer < MEL_RENDER_STAGE_2D_LAYER_COUNT
        ? opt.text_layer
        : MEL_RENDER_STAGE_2D_LAYER_WORLD;

    *renderer = (Mel_Render_ECS_2D){
        .world = opt.world,
        .alloc = alloc,
    };

    if (!mel_render_stage_2d_init(&renderer->stage,
        .name = opt.name.len ? opt.name : S8("ecs_2d"),
        .swapchain = opt.swapchain,
        .world_camera = opt.world_camera,
        .hud_camera = opt.hud_camera,
        .debug_camera = opt.debug_camera,
        .ui_camera = opt.ui_camera,
        .clear_color_enabled = opt.clear_color_enabled,
        .clear_color = opt.clear_color,
        .design_width = opt.design_width,
        .design_height = opt.design_height,
        .enable_imgui = opt.enable_imgui,
        .imgui_fn = opt.imgui_fn,
        .imgui_user = opt.imgui_user,
        .install_as_current_graph = opt.install_as_current_graph,
        .dev = opt.dev,
        .sprite_pass = opt.sprite_pass,
        .text_pass = opt.text_pass,
        .alloc = alloc))
        return false;

    mel_render_list_init(&renderer->sprite_list,
        .name = S8("ecs_2d_sprites"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = alloc);

    mel_render_list_init(&renderer->text_list,
        .name = S8("ecs_2d_text"),
        .entry_stride = sizeof(Mel_Text_Entry),
        .mode = MEL_RENDER_LIST_EPHEMERAL,
        .alloc = alloc);

    if (!mel_render_stage_2d_attach_sprite_list_to_layer(&renderer->stage, sprite_layer, &renderer->sprite_list))
        return false;
    if (!mel_render_stage_2d_attach_text_list_to_layer(&renderer->stage, text_layer, &renderer->text_list))
        return false;

    mel_render_sync_init(&renderer->sprite_sync,
        .list = &renderer->sprite_list,
        .world = renderer->world,
        .components = { ecs_id(Mel_CTransform), ecs_id(Mel_Sprite) },
        .write = mel__render_ecs_2d_write_sprite,
        .key = mel__render_ecs_2d_sprite_key,
        .alloc = alloc);

    return mel_render_stage_2d_rebuild(&renderer->stage);
}

void mel_render_ecs_2d_shutdown(Mel_Render_ECS_2D* renderer)
{
    assert(renderer != nullptr);

    mel_render_sync_shutdown(&renderer->sprite_sync);
    mel_render_stage_2d_shutdown(&renderer->stage);
    mel_render_list_shutdown(&renderer->text_list);
    mel_render_list_shutdown(&renderer->sprite_list);

    *renderer = (Mel_Render_ECS_2D){0};
}

void mel_render_ecs_2d_extract(Mel_Render_ECS_2D* renderer)
{
    assert(renderer != nullptr);

    mel_render_sync_update(&renderer->sprite_sync);
    mel_render_list_clear(&renderer->text_list);
    mel_text_system_run(renderer->world,
        .list = &renderer->text_list);
}

Mel_Render_Stage_2D* mel_render_ecs_2d_stage(Mel_Render_ECS_2D* renderer)
{
    assert(renderer != nullptr);
    return &renderer->stage;
}

Mel_Render_List* mel_render_ecs_2d_sprite_list(Mel_Render_ECS_2D* renderer)
{
    assert(renderer != nullptr);
    return &renderer->sprite_list;
}

Mel_Render_List* mel_render_ecs_2d_text_list(Mel_Render_ECS_2D* renderer)
{
    assert(renderer != nullptr);
    return &renderer->text_list;
}
