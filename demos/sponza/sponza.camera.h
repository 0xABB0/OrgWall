#pragma once

#include <SDL3/SDL.h>

#include "render.viewport.h"
#include "swapchain.h"
#include "math.vec3.h"

typedef struct {
    Mel_Vec3 target;
    Mel_Vec3 extents;
    Mel_Vec3 position;
    f32 yaw;
    f32 pitch;
    f32 far_plane;
    bool mouse_captured;
} Sponza_Camera;

void sponza_camera_init(Sponza_Camera* camera,
                        Mel_Vec3 target,
                        Mel_Vec3 extents);
void sponza_camera_update(Sponza_Camera* camera,
                          Mel_Swapchain_Handle swapchain,
                          Mel_Render_View_Handle view,
                          f32 dt);
void sponza_camera_event(Sponza_Camera* camera,
                         SDL_Event* event,
                         SDL_Window* window);
