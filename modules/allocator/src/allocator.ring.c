#include <allocator.ring/allocator.ring.h>
#include <string.h>

void mel_ring_init(Mel_Ring_Alloc* ring, void* buffer, usize size)
{
    assert(ring != NULL);
    assert(buffer != NULL);
    assert(size > 0);
    ring->base = (u8*)buffer;
    ring->size = size;
    ring->write_offset = 0;
    ring->read_offset = 0;
    ring->used = 0;
#if MEL_ALLOCATOR_RING_DEBUG
    ring->peak_used = 0;
    ring->push_count = 0;
    ring->pop_count = 0;
    ring->name = NULL;
#endif
}

void* mel_ring_push(Mel_Ring_Alloc* ring, usize size)
{
    assert(ring != NULL);
    assert(size > 0);

    usize total = sizeof(Mel_Ring_Header) + size;

    if (ring->write_offset + total > ring->size)
    {
        usize wasted = ring->size - ring->write_offset;
        if (wasted >= sizeof(Mel_Ring_Header))
        {
            Mel_Ring_Header* sentinel = (Mel_Ring_Header*)(ring->base + ring->write_offset);
            sentinel->size = 0;
        }

        if (total + ring->used + wasted > ring->size) return NULL;

        ring->used += wasted;
        ring->write_offset = 0;
    }

    if (ring->used + total > ring->size) return NULL;

    Mel_Ring_Header* header = (Mel_Ring_Header*)(ring->base + ring->write_offset);
    header->size = size;

    void* ptr = ring->base + ring->write_offset + sizeof(Mel_Ring_Header);
    ring->write_offset += total;
    if (ring->write_offset >= ring->size) ring->write_offset = 0;
    ring->used += total;

#if MEL_ALLOCATOR_RING_DEBUG
    ring->push_count++;
    if (ring->used > ring->peak_used) ring->peak_used = ring->used;
#endif

    return ptr;
}

void mel_ring_pop(Mel_Ring_Alloc* ring)
{
    assert(ring != NULL);
    assert(ring->used > 0);

    Mel_Ring_Header* header = (Mel_Ring_Header*)(ring->base + ring->read_offset);

    if (header->size == 0)
    {
        usize wasted = ring->size - ring->read_offset;
        ring->used -= wasted;
        ring->read_offset = 0;
        header = (Mel_Ring_Header*)(ring->base + ring->read_offset);
    }

    usize total = sizeof(Mel_Ring_Header) + header->size;
    ring->read_offset += total;
    if (ring->read_offset >= ring->size) ring->read_offset = 0;
    ring->used -= total;

#if MEL_ALLOCATOR_RING_DEBUG
    ring->pop_count++;
#endif
}

void* mel_ring_peek(Mel_Ring_Alloc* ring)
{
    assert(ring != NULL);
    if (ring->used == 0) return NULL;

    usize offset = ring->read_offset;
    Mel_Ring_Header* header = (Mel_Ring_Header*)(ring->base + offset);

    if (header->size == 0)
    {
        offset = 0;
        header = (Mel_Ring_Header*)(ring->base + offset);
    }

    return ring->base + offset + sizeof(Mel_Ring_Header);
}

void mel_ring_reset(Mel_Ring_Alloc* ring)
{
    assert(ring != NULL);
    ring->write_offset = 0;
    ring->read_offset = 0;
    ring->used = 0;
}

usize mel_ring_available(Mel_Ring_Alloc* ring)
{
    assert(ring != NULL);
    return ring->size - ring->used;
}
