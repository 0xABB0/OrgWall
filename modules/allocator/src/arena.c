#include <allocator/arena.h>

void mel_arena_init(Mel_Arena* arena, void* buffer, usize size)
{
    assert(arena != NULL);
    assert(buffer != NULL);
    assert(size > 0);
    arena->base = (u8*)buffer;
    arena->size = size;
    arena->offset = 0;
#if MEL_ALLOCATOR_ARENA_DEBUG
    arena->peak_used = 0;
    arena->push_count = 0;
    arena->reset_count = 0;
    arena->name = NULL;
#endif
}

void mel_arena_reset(Mel_Arena* arena)
{
    assert(arena != NULL);
    arena->offset = 0;
#if MEL_ALLOCATOR_ARENA_DEBUG
    arena->reset_count++;
#endif
}
