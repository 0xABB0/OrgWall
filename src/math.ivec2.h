#pragma once

#include "types.h"
#include "math.vec2.h"

typedef union
{
    i32x2 v;
    struct { i32 x, y; };
    struct { i32 w, h; };
    i32 e[2];
} Mel_IVec2;

#define MEL_IVEC2_ZERO ((Mel_IVec2){ .v = (i32x2){0, 0} })
#define MEL_IVEC2_ONE  ((Mel_IVec2){ .v = (i32x2){1, 1} })

[[nodiscard]] static inline Mel_IVec2 mel_ivec2(i32 x, i32 y);
[[nodiscard]] static inline Mel_IVec2 mel_ivec2_add(Mel_IVec2 a, Mel_IVec2 b);
[[nodiscard]] static inline Mel_IVec2 mel_ivec2_sub(Mel_IVec2 a, Mel_IVec2 b);
[[nodiscard]] static inline Mel_IVec2 mel_ivec2_mul(Mel_IVec2 a, Mel_IVec2 b);
[[nodiscard]] static inline Mel_IVec2 mel_ivec2_div(Mel_IVec2 a, Mel_IVec2 b);
[[nodiscard]] static inline Mel_IVec2 mel_ivec2_scale(Mel_IVec2 v, i32 s);
[[nodiscard]] static inline Mel_IVec2 mel_ivec2_negate(Mel_IVec2 v);
[[nodiscard]] static inline Mel_IVec2 mel_ivec2_min(Mel_IVec2 a, Mel_IVec2 b);
[[nodiscard]] static inline Mel_IVec2 mel_ivec2_max(Mel_IVec2 a, Mel_IVec2 b);
[[nodiscard]] static inline Mel_IVec2 mel_ivec2_abs(Mel_IVec2 v);
[[nodiscard]] static inline i32 mel_ivec2_dot(Mel_IVec2 a, Mel_IVec2 b);
[[nodiscard]] static inline bool mel_ivec2_eq(Mel_IVec2 a, Mel_IVec2 b);
[[nodiscard]] static inline Mel_Vec2 mel_ivec2_to_vec2(Mel_IVec2 v);
[[nodiscard]] static inline Mel_IVec2 mel_vec2_to_ivec2(Mel_Vec2 v);

#include "math.ivec2.inl"
