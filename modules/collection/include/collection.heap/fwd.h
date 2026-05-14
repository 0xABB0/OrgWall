#pragma once

#include <core/types.h>

#define Mel_Heap(T) struct { T* items; usize count; usize capacity; const Mel_Alloc* allocator; }
