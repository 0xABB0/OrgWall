#pragma once

#include "render.scene.fwd.h"
#include "render.environment.h"
#include "render.manager.h"
#include "render.pipeline.h"
#include "render.source.type.h"
#include "gpu.device.fwd.h"
#include "allocator.fwd.h"
#include "math.vec4.h"

typedef struct {
    Mel_Vec4 direction_intensity;
    Mel_Vec4 color;
    Mel_Vec4 shadow_params;
    u32 flags;
    u32 _pad0;
    u32 _pad1;
    u32 _pad2;
} Mel_Render_Scene_Directional_Light;

#define MEL_RENDER_SCENE_DIRECTIONAL_LIGHT_CAST_SHADOWS (1u << 0)

typedef struct {
    Mel_Vec4 position_range;
    Mel_Vec4 color_intensity;
} Mel_Render_Scene_Point_Light;

typedef struct {
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
    u32 initial_capacity;
} Mel_Render_Scene_Opt;

Mel_Render_Scene* mel_render_scene_create_opt(Mel_Render_Scene_Opt opt);
#define mel_render_scene_create(...) mel_render_scene_create_opt((Mel_Render_Scene_Opt){__VA_ARGS__})

void mel_render_scene_destroy(Mel_Render_Scene* scene);

void mel_render_scene_attach_source(Mel_Render_Scene* scene, Mel_Render_Source* source);
void mel_render_scene_detach_source(Mel_Render_Scene* scene, Mel_Render_Source* source);

void mel_render_scene_sync(Mel_Render_Scene* scene);

Mel_Render_Manager* mel_render_scene_manager(Mel_Render_Scene* scene);
void mel_render_scene_set_environment(Mel_Render_Scene* scene,
                                      Mel_Render_Environment_Handle environment);
Mel_Render_Environment_Handle mel_render_scene_environment(Mel_Render_Scene* scene);
void mel_render_scene_clear_directional_lights(Mel_Render_Scene* scene);
void mel_render_scene_push_directional_light(Mel_Render_Scene* scene,
                                             Mel_Render_Scene_Directional_Light light);
const Mel_Render_Scene_Directional_Light* mel_render_scene_directional_lights(
    Mel_Render_Scene* scene, u32* out_count);
void mel_render_scene_clear_point_lights(Mel_Render_Scene* scene);
void mel_render_scene_push_point_light(Mel_Render_Scene* scene,
                                       Mel_Render_Scene_Point_Light light);
const Mel_Render_Scene_Point_Light* mel_render_scene_point_lights(
    Mel_Render_Scene* scene, u32* out_count);
Mel_Render_Pipeline_Scene* mel_render_scene_pipeline_scene(Mel_Render_Scene* scene,
                                                           const Mel_Render_Pipeline_Type* type);
