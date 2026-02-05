#pragma once

#include "types.h"
#include "math.vec4.h"

typedef union
{
    i32x4 v;
    struct { i32 x, y, z, w; };
    struct { i32 r, g, b, a; };
    i32 e[4];
} Mel_IVec4;

#define MEL_IVEC4_ZERO ((Mel_IVec4){ .v = (i32x4){0, 0, 0, 0} })
#define MEL_IVEC4_ONE  ((Mel_IVec4){ .v = (i32x4){1, 1, 1, 1} })

[[nodiscard]] static inline Mel_IVec4 mel_ivec4(i32 x, i32 y, i32 z, i32 w);
[[nodiscard]] static inline Mel_IVec4 mel_ivec4_add(Mel_IVec4 a, Mel_IVec4 b);
[[nodiscard]] static inline Mel_IVec4 mel_ivec4_sub(Mel_IVec4 a, Mel_IVec4 b);
[[nodiscard]] static inline Mel_IVec4 mel_ivec4_mul(Mel_IVec4 a, Mel_IVec4 b);
[[nodiscard]] static inline Mel_IVec4 mel_ivec4_div(Mel_IVec4 a, Mel_IVec4 b);
[[nodiscard]] static inline Mel_IVec4 mel_ivec4_scale(Mel_IVec4 v, i32 s);
[[nodiscard]] static inline Mel_IVec4 mel_ivec4_negate(Mel_IVec4 v);
[[nodiscard]] static inline Mel_IVec4 mel_ivec4_min(Mel_IVec4 a, Mel_IVec4 b);
[[nodiscard]] static inline Mel_IVec4 mel_ivec4_max(Mel_IVec4 a, Mel_IVec4 b);
[[nodiscard]] static inline Mel_IVec4 mel_ivec4_abs(Mel_IVec4 v);
[[nodiscard]] static inline i32 mel_ivec4_dot(Mel_IVec4 a, Mel_IVec4 b);
[[nodiscard]] static inline bool mel_ivec4_eq(Mel_IVec4 a, Mel_IVec4 b);
[[nodiscard]] static inline Mel_Vec4 mel_ivec4_to_vec4(Mel_IVec4 v);
[[nodiscard]] static inline Mel_IVec4 mel_vec4_to_ivec4(Mel_Vec4 v);

#include "math.ivec4.inl"
