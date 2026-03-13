#pragma once

#include "math.vec3.h"

typedef struct {
    Mel_Vec3 center;
    f32 radius;
} Mel_Sphere;

[[nodiscard]] static inline Mel_Sphere mel_sphere(Mel_Vec3 center, f32 radius)
{
    return (Mel_Sphere){ .center = center, .radius = radius };
}
