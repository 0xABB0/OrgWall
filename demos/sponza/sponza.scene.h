#pragma once

#include "render.scene.h"
#include "math.vec3.h"

Mel_Render_Environment_Handle sponza_scene_apply_lighting(Mel_Render_Scene* scene,
                                                          Mel_Vec3 world_center,
                                                          Mel_Vec3 world_extents);
