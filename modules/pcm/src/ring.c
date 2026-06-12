#include <pcm/ring.h>

#include <core/types.h>
#include <core/platform.h>
#include <core/compiler.h>
#include <allocator/allocator.h>

#include <stdatomic.h>
#include <string.h>

struct Mel_Pcm_Ring
{
    f32*             samples;
    u32              channels;
    u32              capacity;
    const Mel_Alloc* alloc;
    MEL_ALIGNAS(MEL_CACHE_LINE_SIZE) _Atomic(u64) head;
    MEL_ALIGNAS(MEL_CACHE_LINE_SIZE) _Atomic(u64) tail;
};

Mel_Pcm_Ring* mel_pcm_ring_create(const Mel_Alloc* a, u32 channels, u32 capacity_frames)
{
    assert(a != NULL);
    assert(channels > 0);
    assert(capacity_frames > 0);

    Mel_Pcm_Ring* r = mel_aligned_alloc(a, sizeof(*r), MEL_ALIGNOF(Mel_Pcm_Ring));
    if (r == NULL)
        return NULL;

    r->samples = mel_calloc(a, sizeof(f32) * (usize)capacity_frames * (usize)channels);
    if (r->samples == NULL)
    {
        mel_aligned_dealloc(a, r, MEL_ALIGNOF(Mel_Pcm_Ring));
        return NULL;
    }

    r->channels = channels;
    r->capacity = capacity_frames;
    atomic_store_explicit(&r->head, 0u, memory_order_relaxed);
    atomic_store_explicit(&r->tail, 0u, memory_order_relaxed);
    r->alloc = a;
    return r;
}

void mel_pcm_ring_destroy(Mel_Pcm_Ring* r)
{
    if (r == NULL)
        return;
    const Mel_Alloc* a = r->alloc;
    mel_dealloc(a, r->samples);
    mel_aligned_dealloc(a, r, MEL_ALIGNOF(Mel_Pcm_Ring));
}

u32 mel_pcm_ring_write(Mel_Pcm_Ring* r, const f32* interleaved_src, u32 frames)
{
    assert(r != NULL);
    assert(interleaved_src != NULL);

    u64 head = atomic_load_explicit(&r->head, memory_order_relaxed);
    u64 tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    u32 free_room = r->capacity - (u32)(head - tail);
    if (frames > free_room)
        frames = free_room;
    if (frames == 0)
        return 0;

    u32 channels = r->channels;
    u32 offset = (u32)(head % r->capacity);
    u32 first = r->capacity - offset;
    if (first > frames)
        first = frames;

    memcpy(r->samples + (usize)offset * channels, interleaved_src, sizeof(f32) * (usize)first * channels);
    if (frames > first)
        memcpy(r->samples, interleaved_src + (usize)first * channels, sizeof(f32) * (usize)(frames - first) * channels);

    atomic_store_explicit(&r->head, head + frames, memory_order_release);
    return frames;
}

u32 mel_pcm_ring_read(Mel_Pcm_Ring* r, f32* interleaved_dst, u32 frames)
{
    assert(r != NULL);
    assert(interleaved_dst != NULL);

    u64 head = atomic_load_explicit(&r->head, memory_order_acquire);
    u64 tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    u32 avail = (u32)(head - tail);
    if (frames > avail)
        frames = avail;
    if (frames == 0)
        return 0;

    u32 channels = r->channels;
    u32 offset = (u32)(tail % r->capacity);
    u32 first = r->capacity - offset;
    if (first > frames)
        first = frames;

    memcpy(interleaved_dst, r->samples + (usize)offset * channels, sizeof(f32) * (usize)first * channels);
    if (frames > first)
        memcpy(interleaved_dst + (usize)first * channels, r->samples, sizeof(f32) * (usize)(frames - first) * channels);

    atomic_store_explicit(&r->tail, tail + frames, memory_order_release);
    return frames;
}

u32 mel_pcm_ring_channels(const Mel_Pcm_Ring* r)
{
    assert(r != NULL);
    return r->channels;
}

u32 mel_pcm_ring_capacity(const Mel_Pcm_Ring* r)
{
    assert(r != NULL);
    return r->capacity;
}

u32 mel_pcm_ring_read_available(const Mel_Pcm_Ring* r)
{
    assert(r != NULL);
    u64 head = atomic_load_explicit(&r->head, memory_order_acquire);
    u64 tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    return (u32)(head - tail);
}

u32 mel_pcm_ring_write_available(const Mel_Pcm_Ring* r)
{
    assert(r != NULL);
    u64 head = atomic_load_explicit(&r->head, memory_order_relaxed);
    u64 tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    return r->capacity - (u32)(head - tail);
}
