#pragma once

#include "types.h"
#include "allocator.stack.config.h"

typedef struct Mel_Stack_Header {
    usize prev_offset;
    usize prev_header;
} Mel_Stack_Header;

typedef struct Mel_Stack_Alloc {
    u8*   base;
    usize size;
    usize offset;
    usize last_header;
#if MEL_ALLOCATOR_STACK_DEBUG
    usize peak_used;
    usize push_count;
    usize pop_count;
    const char* name;
#endif
} Mel_Stack_Alloc;

typedef struct Mel_Stack_Mark {
    usize offset;
    usize last_header;
} Mel_Stack_Mark;

void  mel_stack_init(Mel_Stack_Alloc* stack, void* buffer, usize size);
void* mel_stack_push(Mel_Stack_Alloc* stack, usize size, usize align);
void  mel_stack_pop(Mel_Stack_Alloc* stack);
Mel_Stack_Mark mel_stack_mark(Mel_Stack_Alloc* stack);
void  mel_stack_restore(Mel_Stack_Alloc* stack, Mel_Stack_Mark mark);
void  mel_stack_reset(Mel_Stack_Alloc* stack);

#define mel_stack_push_struct(s, T) (T*)mel_stack_push((s), sizeof(T), _Alignof(T))
#define mel_stack_push_array(s, T, count) (T*)mel_stack_push((s), sizeof(T) * (count), _Alignof(T))

#include "allocator.stack.inl"
