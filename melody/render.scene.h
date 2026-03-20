#pragma once

#include "render.scene.fwd.h"
#include "render.manager.h"
#include "render.pipeline.h"
#include "render.source.type.h"
#include "gpu.device.fwd.h"
#include "allocator.fwd.h"

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
Mel_Render_Pipeline_Scene* mel_render_scene_pipeline_scene(Mel_Render_Scene* scene,
                                                           const Mel_Render_Pipeline_Type* type);
