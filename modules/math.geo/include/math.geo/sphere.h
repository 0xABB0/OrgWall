#pragma once

#include <core/compiler.h>

#include "math.vec3.h"

typedef struct {
    Mel_Vec3 center;
    f32 radius;
} Mel_Sphere;

MEL_NODISCARD static inline Mel_Sphere mel_sphere(Mel_Vec3 center, f32 radius)
{
    return (Mel_Sphere){ .center = center, .radius = radius };
}
