#include "render.camera.orbit.h"
#include "render.camera.h"
#include "math.scalar.h"
#include "math.mat4.h"
#include <math.h>

void mel_orbit_camera_init_opt(Mel_Orbit_Camera* orbit, Mel_Orbit_Camera_Init_Opt opt)
{
    *orbit = (Mel_Orbit_Camera){
        .target = opt.target,
        .distance = opt.distance > 0 ? opt.distance : 5.0f,
        .yaw = opt.yaw,
        .pitch = opt.pitch,
        .fov = opt.fov > 0 ? opt.fov : 50.0f * MEL_DEG2RAD,
        .near_plane = opt.near_plane > 0 ? opt.near_plane : 0.1f,
        .far_plane = opt.far_plane > 0 ? opt.far_plane : 100.0f,
        .sensitivity = opt.sensitivity > 0 ? opt.sensitivity : 0.005f,
        .zoom_speed = opt.zoom_speed > 0 ? opt.zoom_speed : 0.3f,
        .min_distance = opt.min_distance > 0 ? opt.min_distance : 0.5f,
        .max_distance = opt.max_distance > 0 ? opt.max_distance : 50.0f,
        .min_pitch = -MEL_HALF_PI + 0.01f,
        .max_pitch = MEL_HALF_PI - 0.01f,
    };
}

void mel_orbit_camera_event(Mel_Orbit_Camera* orbit, const SDL_Event* event)
{
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT)
        orbit->dragging = true;

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT)
        orbit->dragging = false;

    if (event->type == SDL_EVENT_MOUSE_MOTION && orbit->dragging) {
        orbit->yaw -= event->motion.xrel * orbit->sensitivity;
        orbit->pitch += event->motion.yrel * orbit->sensitivity;
        orbit->pitch = mel_clampf(orbit->pitch, orbit->min_pitch, orbit->max_pitch);
    }

    if (event->type == SDL_EVENT_MOUSE_WHEEL) {
        orbit->distance -= event->wheel.y * orbit->zoom_speed;
        orbit->distance = mel_clampf(orbit->distance, orbit->min_distance, orbit->max_distance);
    }
}

void mel_orbit_camera_update(Mel_Orbit_Camera* orbit, Mel_Camera* camera, f32 aspect)
{
    f32 cp = cosf(orbit->pitch);
    f32 sp = sinf(orbit->pitch);
    f32 cy = cosf(orbit->yaw);
    f32 sy = sinf(orbit->yaw);

    Mel_Vec3 eye = mel_vec3(
        orbit->target.x + orbit->distance * cp * sy,
        orbit->target.y + orbit->distance * sp,
        orbit->target.z + orbit->distance * cp * cy);

    camera->position = eye;
    camera->view = mel_mat4_look_at(eye, orbit->target, mel_vec3(0, 1, 0));
    camera->projection = mel_mat4_perspective(orbit->fov, aspect, orbit->near_plane, orbit->far_plane);
}
