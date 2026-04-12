#pragma once

#include "core.types.h"
#include "math.vec3.h"
#include "math.mat4.h"
#include <math.h>

typedef union
{
    f32x4 v;
    struct { f32 x, y, z, w; };
    f32 e[4];
} Mel_Quat;

#define MEL_QUAT_IDENTITY ((Mel_Quat){ .v = (f32x4){0, 0, 0, 1} })

[[nodiscard]] static inline Mel_Quat mel_quat(f32 x, f32 y, f32 z, f32 w);
[[nodiscard]] static inline Mel_Quat mel_quat_from_axis_angle(Mel_Vec3 axis, f32 radians);
[[nodiscard]] static inline Mel_Quat mel_quat_from_euler(f32 pitch, f32 yaw, f32 roll);
[[nodiscard]] static inline Mel_Quat mel_quat_mul(Mel_Quat a, Mel_Quat b);
[[nodiscard]] static inline Mel_Quat mel_quat_conjugate(Mel_Quat q);
[[nodiscard]] static inline f32 mel_quat_len_sq(Mel_Quat q);
[[nodiscard]] static inline f32 mel_quat_len(Mel_Quat q);
[[nodiscard]] static inline Mel_Quat mel_quat_normalize(Mel_Quat q);
[[nodiscard]] static inline Mel_Quat mel_quat_inverse(Mel_Quat q);
[[nodiscard]] static inline Mel_Vec3 mel_quat_rotate_vec3(Mel_Quat q, Mel_Vec3 v);
[[nodiscard]] static inline Mel_Quat mel_quat_slerp(Mel_Quat a, Mel_Quat b, f32 t);
[[nodiscard]] static inline Mel_Mat4 mel_quat_to_mat4(Mel_Quat q);
[[nodiscard]] static inline f32 mel_quat_angle(Mel_Quat a, Mel_Quat b);
[[nodiscard]] static inline Mel_Quat mel_quat_rotateX(f32 radians);
[[nodiscard]] static inline Mel_Quat mel_quat_rotateY(f32 radians);
[[nodiscard]] static inline Mel_Quat mel_quat_rotateZ(f32 radians);
[[nodiscard]] static inline Mel_Quat mel_quat_lerp(Mel_Quat a, Mel_Quat b, f32 t);
[[nodiscard]] static inline Mel_Vec3 mel_quat_to_euler(Mel_Quat q);
[[nodiscard]] static inline Mel_Vec3 mel_quat_mulXYZ(Mel_Quat a, Mel_Quat b);

#include "math.quat.inl"
