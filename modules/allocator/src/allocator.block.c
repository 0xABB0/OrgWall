#include <allocator.block/block.h>
#include <string.h>

static usize mel__block_align_forward(usize ptr, usize align)
{
    assert((align & (align - 1)) == 0);
    usize mod = ptr & (align - 1);
    if (mod != 0) ptr += align - mod;
    return ptr;
}

void mel_block_init(Mel_Block_Alloc* alloc, void* buffer, usize size)
{
    assert(alloc != NULL);
    assert(buffer != NULL);
    assert(size > 0);
    alloc->base = (u8*)buffer;
    alloc->size = size;
    alloc->offset = 0;
#if MEL_ALLOCATOR_BLOCK_DEBUG
    alloc->peak_used = 0;
    alloc->push_count = 0;
    alloc->reset_count = 0;
    alloc->name = NULL;
#endif
}

void* mel_block_push(Mel_Block_Alloc* alloc, usize size, usize align)
{
    assert(alloc != NULL);
    assert(size > 0);
    assert(align > 0 && (align & (align - 1)) == 0);

    usize header_start = mel__block_align_forward(alloc->offset, _Alignof(Mel_Block_Header));
    usize data_start = mel__block_align_forward(header_start + sizeof(Mel_Block_Header), align);
    assert(data_start + size <= alloc->size);

    Mel_Block_Header* header = (Mel_Block_Header*)(alloc->base + header_start);
    header->size = size;
    header->data_offset = data_start - header_start;

    alloc->offset = data_start + size;

#if MEL_ALLOCATOR_BLOCK_DEBUG
    alloc->push_count++;
    if (alloc->offset > alloc->peak_used) alloc->peak_used = alloc->offset;
#endif

    return alloc->base + data_start;
}

void mel_block_reset(Mel_Block_Alloc* alloc)
{
    assert(alloc != NULL);
    alloc->offset = 0;
#if MEL_ALLOCATOR_BLOCK_DEBUG
    alloc->reset_count++;
#endif
}

Mel_Block_Iter mel_block_iter_begin(Mel_Block_Alloc* alloc)
{
    return (Mel_Block_Iter){ .alloc = alloc, .offset = 0 };
}

void* mel_block_iter_next(Mel_Block_Iter* iter, usize* out_size)
{
    assert(iter != NULL);
    if (iter->offset >= iter->alloc->offset) return NULL;

    usize header_start = mel__block_align_forward(iter->offset, _Alignof(Mel_Block_Header));
    Mel_Block_Header* header = (Mel_Block_Header*)(iter->alloc->base + header_start);

    usize data_start = header_start + header->data_offset;
    if (out_size) *out_size = header->size;

    iter->offset = data_start + header->size;

    return iter->alloc->base + data_start;
}

bool mel_block_iter_end(Mel_Block_Iter* iter)
{
    return iter->offset >= iter->alloc->offset;
}
