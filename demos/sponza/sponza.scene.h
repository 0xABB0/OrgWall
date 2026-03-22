#pragma once

#include "render.scene.h"
#include "math.vec3.h"

void sponza_scene_apply_lighting(Mel_Render_Scene* scene,
                                 Mel_Vec3 world_center,
                                 Mel_Vec3 world_extents);
