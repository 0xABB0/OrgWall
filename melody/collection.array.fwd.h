#pragma once

#include "allocator.fwd.h"
#include "types.h"

#define Mel_Array(T) struct { T* items; usize count; usize capacity; const Mel_Alloc* allocator; }
