#pragma once

#include "render.environment.fwd.h"
#include "math.vec4.h"

#define MEL_RENDER_ENVIRONMENT_CONSTANT 1u

struct Mel_Render_Environment {
    u32 type;
    Mel_Vec4 constant_radiance;
};

Mel_Render_Environment_Handle mel_render_environment_create_constant(Mel_Vec4 radiance);
void mel_render_environment_destroy(Mel_Render_Environment_Handle handle);
bool mel_render_environment_alive(Mel_Render_Environment_Handle handle);
Mel_Render_Environment* mel_render_environment_get(Mel_Render_Environment_Handle handle);
void mel_render_environment_set_constant(Mel_Render_Environment_Handle handle, Mel_Vec4 radiance);
