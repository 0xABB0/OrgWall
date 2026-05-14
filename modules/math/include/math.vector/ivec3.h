#pragma once

#include <core/compiler.h>

#include <core/types.h>
#include "vec3.h"

typedef i32 i32x3 MEL_VECTOR_TYPE(3);

typedef union
{
    i32x3 v;
    struct { i32 x, y, z; };
    struct { i32 r, g, b; };
    i32 e[3];
} Mel_IVec3;

#define MEL_IVEC3_ZERO ((Mel_IVec3){ .v = (i32x3){0, 0, 0} })
#define MEL_IVEC3_ONE  ((Mel_IVec3){ .v = (i32x3){1, 1, 1} })

MEL_NODISCARD static inline Mel_IVec3 mel_ivec3(i32 x, i32 y, i32 z);
MEL_NODISCARD static inline Mel_IVec3 mel_ivec3_add(Mel_IVec3 a, Mel_IVec3 b);
MEL_NODISCARD static inline Mel_IVec3 mel_ivec3_sub(Mel_IVec3 a, Mel_IVec3 b);
MEL_NODISCARD static inline Mel_IVec3 mel_ivec3_mul(Mel_IVec3 a, Mel_IVec3 b);
MEL_NODISCARD static inline Mel_IVec3 mel_ivec3_div(Mel_IVec3 a, Mel_IVec3 b);
MEL_NODISCARD static inline Mel_IVec3 mel_ivec3_scale(Mel_IVec3 v, i32 s);
MEL_NODISCARD static inline Mel_IVec3 mel_ivec3_negate(Mel_IVec3 v);
MEL_NODISCARD static inline Mel_IVec3 mel_ivec3_min(Mel_IVec3 a, Mel_IVec3 b);
MEL_NODISCARD static inline Mel_IVec3 mel_ivec3_max(Mel_IVec3 a, Mel_IVec3 b);
MEL_NODISCARD static inline Mel_IVec3 mel_ivec3_abs(Mel_IVec3 v);
MEL_NODISCARD static inline i32 mel_ivec3_dot(Mel_IVec3 a, Mel_IVec3 b);
MEL_NODISCARD static inline bool mel_ivec3_eq(Mel_IVec3 a, Mel_IVec3 b);
MEL_NODISCARD static inline Mel_Vec3 mel_ivec3_to_vec3(Mel_IVec3 v);
MEL_NODISCARD static inline Mel_IVec3 mel_vec3_to_ivec3(Mel_Vec3 v);

#include "ivec3.inl"
