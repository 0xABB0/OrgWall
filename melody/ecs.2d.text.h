#pragma once

#include "core.types.h"
#include "math.vec4.h"
#include "font.descriptor.h"
#include "render.material_base.fwd.h"
#include "string.str8.fwd.h"
#include "font.atlas.fwd.h"
#include "font.sdf.fwd.h"
#include "font.msdf.fwd.h"

#include <flecs.h>

typedef struct Mel_CText {
    str8 text;
    Mel_Font_Descriptor* desc;
    u32 texture_idx;
    Mel_Material_Base_Id material_id;
    Mel_Material_Instance_Id material_instance;
    Mel_Vec4 color;
    f32 scale;
} Mel_CText;

extern ECS_COMPONENT_DECLARE(Mel_CText);
void mel_component_text_register(ecs_world_t* world);

Mel_CText mel_ctext_atlas(Mel_Font_Atlas_Handle font, str8 text, Mel_Vec4 color);
Mel_CText mel_ctext_sdf(Mel_Font_SDF_Handle font, str8 text, Mel_Vec4 color);
Mel_CText mel_ctext_msdf(Mel_Font_MSDF_Handle font, str8 text, Mel_Vec4 color);
