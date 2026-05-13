#pragma once

#include <core/compiler.h>

#include "math.vec3.h"

typedef struct {
    Mel_Vec3 origin;
    Mel_Vec3 dir;
} Mel_Ray;

MEL_NODISCARD static inline Mel_Ray mel_ray(Mel_Vec3 origin, Mel_Vec3 dir)
{
    return (Mel_Ray){ .origin = origin, .dir = dir };
}

MEL_NODISCARD static inline Mel_Vec3 mel_ray_at(Mel_Ray r, f32 t)
{
    return mel_vec3_add(r.origin, mel_vec3_scale(r.dir, t));
}
