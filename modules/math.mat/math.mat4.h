#pragma once

#include <core/compiler.h>

#include "core.types.h"
#include "math.vec3.h"
#include "math.vec4.h"
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

MEL_NODISCARD static inline Mel_Mat4 mel_mat4_mul(Mel_Mat4 a, Mel_Mat4 b);
MEL_NODISCARD static inline Mel_Vec4 mel_mat4_mul_vec4(Mel_Mat4 m, Mel_Vec4 v);
MEL_NODISCARD static inline Mel_Vec3 mel_mat4_mul_point(Mel_Mat4 m, Mel_Vec3 p);
MEL_NODISCARD static inline Mel_Vec3 mel_mat4_mul_dir(Mel_Mat4 m, Mel_Vec3 d);
MEL_NODISCARD static inline Mel_Mat4 mel_mat4_transpose(Mel_Mat4 m);
MEL_NODISCARD static inline Mel_Mat4 mel_mat4_translate(Mel_Vec3 t);
MEL_NODISCARD static inline Mel_Mat4 mel_mat4_scale(Mel_Vec3 s);
MEL_NODISCARD static inline Mel_Mat4 mel_mat4_rotate_x(f32 radians);
MEL_NODISCARD static inline Mel_Mat4 mel_mat4_rotate_y(f32 radians);
MEL_NODISCARD static inline Mel_Mat4 mel_mat4_rotate_z(f32 radians);
MEL_NODISCARD static inline Mel_Mat4 mel_mat4_ortho(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far);
MEL_NODISCARD static inline Mel_Mat4 mel_mat4_perspective(f32 fov_radians, f32 aspect, f32 near, f32 far);
MEL_NODISCARD static inline Mel_Mat4 mel_mat4_look_at(Mel_Vec3 eye, Mel_Vec3 target, Mel_Vec3 up);
MEL_NODISCARD static inline Mel_Mat4 mel_mat4_scalef(f32 s);
MEL_NODISCARD static inline Mel_Mat4 mel_mat4_rotateXY(f32 ax, f32 ay);
MEL_NODISCARD static inline Mel_Mat4 mel_mat4_rotateXYZ(f32 ax, f32 ay, f32 az);
MEL_NODISCARD static inline Mel_Mat4 mel_mat4_rotateZYX(f32 ax, f32 ay, f32 az);
MEL_NODISCARD static inline Mel_Vec3 mel_mat4_mul_vec3(Mel_Mat4 m, Mel_Vec3 v);
MEL_NODISCARD static inline Mel_Vec3 mel_mat4_mul_vec3_xyz0(Mel_Mat4 m, Mel_Vec3 v);
MEL_NODISCARD static inline Mel_Vec3 mel_mat4_mul_vec3_H(Mel_Mat4 m, Mel_Vec3 v);
MEL_NODISCARD static inline Mel_Mat4 mel_mat4_SRT(f32 sx, f32 sy, f32 sz, f32 ax, f32 ay, f32 az, f32 tx, f32 ty, f32 tz);
MEL_NODISCARD static inline Mel_Mat4 mel_mat4_inverse(Mel_Mat4 m);
MEL_NODISCARD static inline Mel_Mat4 mel_mat4_inv_transform(Mel_Mat4 m);
MEL_NODISCARD static inline Mel_Mat4 mel_mat4_proj_flip_handedness(Mel_Mat4 m);
MEL_NODISCARD static inline Mel_Mat4 mel_mat4_view_flip_handedness(Mel_Mat4 m);

#include "math.mat4.inl"
