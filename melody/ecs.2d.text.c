#include "ecs.2d.text.h"
#include "font.atlas.h"
#include "font.sdf.h"
#include "font.msdf.h"
#include "text.material.h"
#include "render.material_base.h"
#include "texture.pool.h"
#include "string.str8.h"

ECS_COMPONENT_DECLARE(Mel_CText);

void mel_component_text_register(ecs_world_t* world)
{
    ECS_COMPONENT_DEFINE(world, Mel_CText);
}

Mel_CText mel_ctext_atlas(Mel_Font_Atlas_Handle font, str8 text, Mel_Vec4 color)
{
    Mel_Font_Atlas_Entry* entry = mel_font_atlas_get(font);
    assert(entry != nullptr);

    u32 tex_idx = mel_texture_pool_add_to_table(&entry->atlas_texture);

    Mel_Material_Base_Id mat_id = mel_material_base_find(S8("text_atlas"));
    assert(mat_id != MEL_MATERIAL_BASE_ID_INVALID);

    Mel_Text_Atlas_Params params = { .color = color };
    Mel_Material_Instance_Id inst = mel_material_base_alloc_instance(mat_id, &params);

    return (Mel_CText){
        .text = text,
        .desc = &entry->desc,
        .texture_idx = tex_idx,
        .material_id = mat_id,
        .material_instance = inst,
        .color = color,
        .scale = 1.0f,
    };
}

Mel_CText mel_ctext_sdf(Mel_Font_SDF_Handle font, str8 text, Mel_Vec4 color)
{
    Mel_Font_SDF_Entry* entry = mel_font_sdf_get(font);
    assert(entry != nullptr);

    u32 tex_idx = mel_texture_pool_add_to_table(&entry->texture);

    Mel_Material_Base_Id mat_id = mel_material_base_find(S8("text_sdf"));
    assert(mat_id != MEL_MATERIAL_BASE_ID_INVALID);

    Mel_Text_SDF_Params params = {
        .color = color,
        .outline_color = {{ 0, 0, 0, 1 }},
        .edge = 0.5f,
        .softness = 0.004f,
        .outline = 0.0f,
        .px_range = entry->px_range,
    };
    Mel_Material_Instance_Id inst = mel_material_base_alloc_instance(mat_id, &params);

    return (Mel_CText){
        .text = text,
        .desc = &entry->desc,
        .texture_idx = tex_idx,
        .material_id = mat_id,
        .material_instance = inst,
        .color = color,
        .scale = 1.0f,
    };
}

Mel_CText mel_ctext_msdf(Mel_Font_MSDF_Handle font, str8 text, Mel_Vec4 color)
{
    Mel_Font_MSDF_Entry* entry = mel_font_msdf_get(font);
    assert(entry != nullptr);

    u32 tex_idx = mel_texture_pool_add_to_table(&entry->texture);

    Mel_Material_Base_Id mat_id = mel_material_base_find(S8("text_msdf"));
    assert(mat_id != MEL_MATERIAL_BASE_ID_INVALID);

    Mel_Text_MSDF_Params params = {
        .color = color,
        .outline_color = {{ 0, 0, 0, 1 }},
        .edge = 0.5f,
        .softness = 0.004f,
        .outline = 0.0f,
        .px_range = entry->px_range,
    };
    Mel_Material_Instance_Id inst = mel_material_base_alloc_instance(mat_id, &params);

    return (Mel_CText){
        .text = text,
        .desc = &entry->desc,
        .texture_idx = tex_idx,
        .material_id = mat_id,
        .material_instance = inst,
        .color = color,
        .scale = 1.0f,
    };
}
