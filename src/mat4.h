#ifndef MEL_MAT4_H
#define MEL_MAT4_H

#include "types.h"
#include "vec3.h"
#include "vec4.h"
#include <math.h>

typedef union
{
    f32x4 rows[4];
    f32 e[16];
    f32 m[4][4];
} Mel_Mat4;

#define MEL_MAT4_IDENTITY ((Mel_Mat4){ .rows = { \
    (f32x4){1, 0, 0, 0}, \
    (f32x4){0, 1, 0, 0}, \
    (f32x4){0, 0, 1, 0}, \
    (f32x4){0, 0, 0, 1}  \
}})

#define MEL_MAT4_ZERO ((Mel_Mat4){ .rows = { \
    (f32x4){0, 0, 0, 0}, \
    (f32x4){0, 0, 0, 0}, \
    (f32x4){0, 0, 0, 0}, \
    (f32x4){0, 0, 0, 0}  \
}})

[[nodiscard]] static inline Mel_Mat4 mel_mat4_mul(Mel_Mat4 a, Mel_Mat4 b);
[[nodiscard]] static inline Mel_Vec4 mel_mat4_mul_vec4(Mel_Mat4 m, Mel_Vec4 v);
[[nodiscard]] static inline Mel_Vec3 mel_mat4_mul_point(Mel_Mat4 m, Mel_Vec3 p);
[[nodiscard]] static inline Mel_Vec3 mel_mat4_mul_dir(Mel_Mat4 m, Mel_Vec3 d);
[[nodiscard]] static inline Mel_Mat4 mel_mat4_transpose(Mel_Mat4 m);
[[nodiscard]] static inline Mel_Mat4 mel_mat4_translate(Mel_Vec3 t);
[[nodiscard]] static inline Mel_Mat4 mel_mat4_scale(Mel_Vec3 s);
[[nodiscard]] static inline Mel_Mat4 mel_mat4_rotate_x(f32 radians);
[[nodiscard]] static inline Mel_Mat4 mel_mat4_rotate_y(f32 radians);
[[nodiscard]] static inline Mel_Mat4 mel_mat4_rotate_z(f32 radians);
[[nodiscard]] static inline Mel_Mat4 mel_mat4_ortho(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far);
[[nodiscard]] static inline Mel_Mat4 mel_mat4_perspective(f32 fov_radians, f32 aspect, f32 near, f32 far);
[[nodiscard]] static inline Mel_Mat4 mel_mat4_look_at(Mel_Vec3 eye, Mel_Vec3 target, Mel_Vec3 up);

#include "mat4.inl"

#endif
