#pragma once

#include "render.camera.fwd.h"
#include "math.mat4.h"
#include "math.vec2.h"
#include "math.vec3.h"

struct Mel_Camera {
    Mel_Mat4 view;
    Mel_Mat4 projection;
    Mel_Vec3 position;
};

[[nodiscard]] static inline Mel_Mat4 mel_camera_vp(const Mel_Camera* cam)
{
    return mel_mat4_mul(cam->projection, cam->view);
}

[[nodiscard]] static inline bool mel_camera_project_to_viewport(
    const Mel_Camera* cam, Mel_Vec3 world, f32 viewport_w, f32 viewport_h, Mel_Vec2* out)
{
    Mel_Mat4 vp = mel_camera_vp(cam);
    Mel_Vec4 clip = mel_mat4_mul_vec4(vp, mel_vec4(world.x, world.y, world.z, 1.0f));
    if (clip.w <= 0.001f)
        return false;

    f32 inv_w = 1.0f / clip.w;
    f32 ndc_x = clip.x * inv_w;
    f32 ndc_y = clip.y * inv_w;
    f32 ndc_z = clip.z * inv_w;
    if (ndc_x < -1.05f || ndc_x > 1.05f || ndc_y < -1.05f || ndc_y > 1.05f || ndc_z < -1.05f || ndc_z > 1.05f)
        return false;

    out->x = (ndc_x * 0.5f + 0.5f) * viewport_w;
    out->y = (1.0f - (ndc_y * 0.5f + 0.5f)) * viewport_h;
    return true;
}
