#pragma once

#include <core/types.h>
#include "anim.registry.fwd.h"
#include <allocator/fwd.h>
#include "hash.xxh.fwd.h"

struct Mel_Track_Type_Def {
    u64 type_hash;
    u32 stride;
    Mel_Batch_Lerp_Fn lerp_fn;
    Mel_Batch_Additive_Fn additive_fn;
};

#define MEL_ANIM_TYPE_F32  mel_xxh3_64("f32",  3)
#define MEL_ANIM_TYPE_VEC2 mel_xxh3_64("vec2", 4)
#define MEL_ANIM_TYPE_VEC3 mel_xxh3_64("vec3", 4)
#define MEL_ANIM_TYPE_VEC4 mel_xxh3_64("vec4", 4)
#define MEL_ANIM_TYPE_QUAT mel_xxh3_64("quat", 4)

void mel_anim_registry_init(const Mel_Alloc* alloc);
void mel_anim_registry_register(u64 type_hash, u32 stride,
                                Mel_Batch_Lerp_Fn lerp_fn,
                                Mel_Batch_Additive_Fn additive_fn);
Mel_Track_Type_Def* mel_anim_registry_get(u64 type_hash);
