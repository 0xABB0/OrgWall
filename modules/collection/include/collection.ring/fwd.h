#pragma once

#include <core/types.h>
#include <allocator/fwd.h>

#define Mel_Ring(T) struct { T* items; usize head; usize count; usize capacity; const Mel_Alloc* allocator; }
