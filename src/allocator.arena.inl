#pragma once

#ifdef _CLANGD
#include "allocator.arena.h"
#endif

#ifdef MEL_ALLOCATOR_ARENA_DEBUG

static inline void* mel__arena_push_tracked(Mel_Arena *a, usize size, char *file, u32 line);
static inline void* mel__arena_push_align_tracked(Mel_Arena *a, usize size, usize align, char *file, u32 line);
static inline void* mel__arena_push_zero_tracked(Mel_Arena *a, usize size, char *file, u32 line);
static inline void* mel__arena_push_zero_align_tracked(Mel_Arena *a, usize size, usize align, char *file, u32 line);
static inline void* mel__arena_push_copy_tracked(Mel_Arena* a, void* src, usize size, char* file, u32 line);

#else

static inline void* mel__arena_push(Mel_Arena *a, usize size);
static inline void* mel__arena_push_align(Mel_Arena *a, usize size, usize align);
static inline void* mel__arena_push_zero(Mel_Arena *a, usize size);
static inline void* mel__arena_push_zero_align(Mel_Arena *a, usize size, usize align);
static inline void* mel__arena_push_copy(Mel_Arena* a, void* src, usize size);

#endif


inline static Mel_Arena_Scratch mel_arena_scratch_begin(Mel_Arena* a) { return (Mel_Arena_Scratch){ .arena = a, .checkpoint = a->offset }; }
inline static void              mel_arena_scratch_discard(Mel_Arena_Scratch scratch) { scratch.arena->offset = scratch.checkpoint; }
inline static void              mel_arena_scratch_keep(Mel_Arena_Scratch) { /* Purposefully empty. maybe we could keep track of something like counts*/ }
