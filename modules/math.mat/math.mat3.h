#pragma once

#include "core.types.h"
#include "math.scalar.h"
#include "math.vec2.h"
#include "math.vec3.h"
#include "math.mat4.h"

typedef union
{
    f32 e[9];
    f32 m[3][3];
} Mel_Mat3;

#define MEL_MAT3_IDENTITY ((Mel_Mat3){ .m = { \
    {1, 0, 0}, \
    {0, 1, 0}, \
    {0, 0, 1}  \
}})

#define MEL_MAT3_ZERO ((Mel_Mat3){ .e = {0} })

[[nodiscard]] static inline Mel_Mat3 mel_mat3_mul(Mel_Mat3 a, Mel_Mat3 b);
[[nodiscard]] static inline Mel_Vec3 mel_mat3_mul_vec3(Mel_Mat3 m, Mel_Vec3 v);
[[nodiscard]] static inline Mel_Vec2 mel_mat3_mul_vec2(Mel_Mat3 m, Mel_Vec2 v);
[[nodiscard]] static inline Mel_Mat3 mel_mat3_mul_inverse(Mel_Mat3 a, Mel_Mat3 b);
[[nodiscard]] static inline Mel_Vec3 mel_mat3_mul_vec3_inverse(Mel_Mat3 m, Mel_Vec3 v);
[[nodiscard]] static inline Mel_Mat3 mel_mat3_transpose(Mel_Mat3 m);
[[nodiscard]] static inline f32 mel_mat3_determinant(Mel_Mat3 m);
[[nodiscard]] static inline Mel_Mat3 mel_mat3_inverse(Mel_Mat3 m);
[[nodiscard]] static inline Mel_Mat3 mel_mat3_rotate(f32 radians);
[[nodiscard]] static inline Mel_Mat3 mel_mat3_scale_2d(f32 sx, f32 sy);
[[nodiscard]] static inline Mel_Mat3 mel_mat3_translate_2d(f32 x, f32 y);
[[nodiscard]] static inline Mel_Mat3 mel_mat3_translate_2dv(Mel_Vec2 v);
[[nodiscard]] static inline Mel_Mat3 mel_mat3_SRT(f32 sx, f32 sy, f32 angle, f32 tx, f32 ty);
[[nodiscard]] static inline Mel_Mat3 mel_mat3_from_mat4(Mel_Mat4 m);

#include "math.mat3.inl"
