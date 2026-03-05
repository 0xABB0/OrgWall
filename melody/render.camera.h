#pragma once

#include "render.camera.fwd.h"
#include "math.mat4.h"
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
