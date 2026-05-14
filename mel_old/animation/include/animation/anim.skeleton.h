#pragma once

#include <core/types.h>
#include "anim.skeleton.fwd.h"

struct Mel_Skeleton {
    u32 bone_count;
    u64* bone_hashes;
    i32* parent_indices;
    u64 root_bone_hash;
};
