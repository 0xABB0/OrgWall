#pragma once

#include "core.types.h"

#include "cfg.h"

typedef struct Mel_Arena {
    u8*   base;
    usize size;
    usize offset;
#if MEL_ALLOCATOR_ARENA_DEBUG
    usize       peak_used;
    usize       push_count;
    usize       reset_count;
    const char* name;
#endif
} Mel_Arena;

typedef struct Mel_Arena_Scratch {
  Mel_Arena* arena;
  usize      checkpoint;
} Mel_Arena_Scratch;

void  mel_arena_init(Mel_Arena* arena, void* buffer, usize size);
void  mel_arena_reset(Mel_Arena* arena);

#if MEL_ALLOCATOR_ARENA_DEBUG

# define mel_arena_push(arena, size) mel__arena_push_tracked(arena, size, __FILE__, __LINE__)
# define mel_arena_push_align(arena, size, align) mel__arena_push_align_tracked(arena, size, align, __FILE__, __LINE__)
# define mel_arena_push_zero(arena, size) mel__arena_push_zero_tracked(arena, size, __FILE__, __LINE__)
# define mel_arena_push_zero_align(arena, size, align)  mel__arena_push_zero_align_tracked(arena, size, align, __FILE__, __LINE__)

#else

# define mel_arena_push(arena, size) mel__arena_push(arena, size)
# define mel_arena_push_align(arena, size, align) mel__arena_push_align(arena, size, align)
# define mel_arena_push_zero(arena, size) mel__arena_push_zero(arena, size)
# define mel_arena_push_zero_align(arena, size, align) mel__arena_push_zero_align(arena, size, align)

#endif

#define mel_arena_push_struct(a, T)       (T*) mel_arena_push_align(a, sizeof(T), _Alignof(T))
#define mel_arena_push_array(a, T, count) (T*)mel_arena_push_align(a, sizeof(T) * (count), _Alignof(T))

inline static Mel_Arena_Scratch mel_arena_scratch_begin(Mel_Arena* a);
inline static void              mel_arena_scratch_discard(Mel_Arena_Scratch);
inline static void              mel_arena_scratch_keep(Mel_Arena_Scratch);

#include "allocator.arena.inl"
