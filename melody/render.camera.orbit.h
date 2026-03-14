#pragma once

#include "core.types.h"
#include "render.camera.fwd.h"
#include "math.vec3.h"
#include <SDL3/SDL_events.h>

typedef struct {
    Mel_Vec3 target;
    f32 distance;
    f32 yaw;
    f32 pitch;
    f32 fov;
    f32 near_plane;
    f32 far_plane;
    f32 sensitivity;
    f32 zoom_speed;
    f32 min_distance;
    f32 max_distance;
    f32 min_pitch;
    f32 max_pitch;
    bool dragging;
} Mel_Orbit_Camera;

typedef struct {
    Mel_Vec3 target;
    f32 distance;
    f32 yaw;
    f32 pitch;
    f32 fov;
    f32 near_plane;
    f32 far_plane;
    f32 sensitivity;
    f32 zoom_speed;
    f32 min_distance;
    f32 max_distance;
} Mel_Orbit_Camera_Init_Opt;

void mel_orbit_camera_init_opt(Mel_Orbit_Camera* orbit, Mel_Orbit_Camera_Init_Opt opt);
#define mel_orbit_camera_init(orbit, ...) mel_orbit_camera_init_opt((orbit), (Mel_Orbit_Camera_Init_Opt){ __VA_ARGS__ })

void mel_orbit_camera_event(Mel_Orbit_Camera* orbit, const SDL_Event* event);
void mel_orbit_camera_update(Mel_Orbit_Camera* orbit, Mel_Camera* camera, f32 aspect);
