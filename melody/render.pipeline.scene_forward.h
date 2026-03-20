#pragma once

#include "event.channel.fwd.h"
#include "gpu.geometry_pool.fwd.h"
#include "render.types.2d.h"
#include "render.types.3d.h"

extern Mel_Event_Channel mel_pipeline_scene_forward_ready;

typedef struct Mel_Scene_Forward_Emitter Mel_Scene_Forward_Emitter;

typedef struct {
    Mel_Render_Transform_2D transform;
    Mel_Rect uv;
    Mel_Vec4 color;
    u32 texture_idx;
} Mel_Scene_Forward_Sprite;

typedef struct {
    Mel_Mat4 model;
    Mel_Render_Bounds bounds;
    Mel_Geometry_Handle mesh;
} Mel_Scene_Forward_Mesh;

void mel_scene_forward_emit_sprite(Mel_Scene_Forward_Emitter* emitter,
                                   const Mel_Scene_Forward_Sprite* sprite);
void mel_scene_forward_emit_mesh(Mel_Scene_Forward_Emitter* emitter,
                                 const Mel_Scene_Forward_Mesh* mesh);

void mel_pipeline_scene_forward_set_geometry_pool(Mel_Geometry_Pool* pool);
