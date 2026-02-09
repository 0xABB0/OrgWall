#pragma once

#define Mel_Deque(T) struct { T* items; usize head; usize count; usize capacity; const Mel_Alloc* allocator; }
