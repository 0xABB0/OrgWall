#include <allocator.stack/allocator.stack.h>
#include <string.h>

static usize mel__stack_align_forward(usize ptr, usize align)
{
    assert((align & (align - 1)) == 0);
    usize mod = ptr & (align - 1);
    if (mod != 0) ptr += align - mod;
    return ptr;
}

void mel_stack_init(Mel_Stack_Alloc* stack, void* buffer, usize size)
{
    assert(stack != NULL);
    assert(buffer != NULL);
    assert(size > 0);
    stack->base = (u8*)buffer;
    stack->size = size;
    stack->offset = 0;
    stack->last_header = (usize)-1;
#if MEL_ALLOCATOR_STACK_DEBUG
    stack->peak_used = 0;
    stack->push_count = 0;
    stack->pop_count = 0;
    stack->name = NULL;
#endif
}

void* mel_stack_push(Mel_Stack_Alloc* stack, usize size, usize align)
{
    assert(stack != NULL);
    assert(size > 0);
    assert(align > 0 && (align & (align - 1)) == 0);

    usize prev_offset = stack->offset;

    usize header_start = mel__stack_align_forward(stack->offset, _Alignof(Mel_Stack_Header));
    usize data_start = mel__stack_align_forward(header_start + sizeof(Mel_Stack_Header), align);

    assert(data_start + size <= stack->size);

    Mel_Stack_Header* header = (Mel_Stack_Header*)(stack->base + header_start);
    header->prev_offset = prev_offset;
    header->prev_header = stack->last_header;

    stack->last_header = header_start;
    stack->offset = data_start + size;

#if MEL_ALLOCATOR_STACK_DEBUG
    stack->push_count++;
    if (stack->offset > stack->peak_used) stack->peak_used = stack->offset;
#endif

    return stack->base + data_start;
}

void mel_stack_pop(Mel_Stack_Alloc* stack)
{
    assert(stack != NULL);
    assert(stack->last_header != (usize)-1);

    Mel_Stack_Header* header = (Mel_Stack_Header*)(stack->base + stack->last_header);
    stack->offset = header->prev_offset;
    stack->last_header = header->prev_header;

#if MEL_ALLOCATOR_STACK_DEBUG
    stack->pop_count++;
#endif
}

Mel_Stack_Mark mel_stack_mark(Mel_Stack_Alloc* stack)
{
    assert(stack != NULL);
    return (Mel_Stack_Mark){ .offset = stack->offset, .last_header = stack->last_header };
}

void mel_stack_restore(Mel_Stack_Alloc* stack, Mel_Stack_Mark mark)
{
    assert(stack != NULL);
    assert(mark.offset <= stack->offset);
    stack->offset = mark.offset;
    stack->last_header = mark.last_header;
}

void mel_stack_reset(Mel_Stack_Alloc* stack)
{
    assert(stack != NULL);
    stack->offset = 0;
    stack->last_header = (usize)-1;
}
