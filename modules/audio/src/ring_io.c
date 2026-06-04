#include "audio_internal.h"

#include <core/types.h>
#include <allocator/allocator.h>
#include <thread/sem.h>

#include <stdatomic.h>
#include <string.h>

struct Mel_Audio_Ring
{
    f32*               samples;
    u32                capacity;
    _Atomic(u32)       head;
    _Atomic(u32)       tail;
    _Atomic(Mel_Sem*)  wake;
    const Mel_Alloc*   alloc;
};

void mel_audio_ring_set_wake(Mel_Audio_Ring* r, Mel_Sem* wake)
{
    assert(r != NULL);
    atomic_store_explicit(&r->wake, wake, memory_order_release);
}

Mel_Audio_Ring* mel_audio_ring_create(const Mel_Alloc* a, u32 capacity_samples)
{
    assert(a != NULL);
    assert(capacity_samples > 0);

    Mel_Audio_Ring* r = mel_alloc(a, sizeof(*r));
    if (r == NULL)
        return NULL;

    r->samples = mel_calloc(a, sizeof(f32) * (usize)capacity_samples);
    if (r->samples == NULL)
    {
        mel_dealloc(a, r);
        return NULL;
    }

    r->capacity = capacity_samples;
    atomic_store_explicit(&r->head, 0u, memory_order_relaxed);
    atomic_store_explicit(&r->tail, 0u, memory_order_relaxed);
    atomic_store_explicit(&r->wake, NULL, memory_order_relaxed);
    r->alloc = a;
    return r;
}

void mel_audio_ring_destroy(Mel_Audio_Ring* r)
{
    if (r == NULL)
        return;
    const Mel_Alloc* a = r->alloc;
    mel_dealloc(a, r->samples);
    mel_dealloc(a, r);
}

u32 mel_audio_ring_capacity(const Mel_Audio_Ring* r)
{
    assert(r != NULL);
    return r->capacity;
}

u32 mel_audio_ring_write_available(const Mel_Audio_Ring* r)
{
    assert(r != NULL);
    u32 head = atomic_load_explicit(&r->head, memory_order_relaxed);
    u32 tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    u32 used = head - tail;
    return r->capacity - used;
}

u32 mel_audio_ring_read_available(const Mel_Audio_Ring* r)
{
    assert(r != NULL);
    u32 head = atomic_load_explicit(&r->head, memory_order_acquire);
    u32 tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    return head - tail;
}

u32 mel_audio_ring_write(Mel_Audio_Ring* r, const f32* src, u32 count)
{
    assert(r != NULL);
    assert(src != NULL);

    u32 head = atomic_load_explicit(&r->head, memory_order_relaxed);
    u32 tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    u32 free_room = r->capacity - (head - tail);
    if (count > free_room)
        count = free_room;

    u32 offset = head % r->capacity;
    u32 first = r->capacity - offset;
    if (first > count)
        first = count;

    memcpy(r->samples + offset, src, sizeof(f32) * (usize)first);
    if (count > first)
        memcpy(r->samples, src + first, sizeof(f32) * (usize)(count - first));

    atomic_store_explicit(&r->head, head + count, memory_order_release);
    return count;
}

u32 mel_audio_ring_read(Mel_Audio_Ring* r, f32* dst, u32 count)
{
    assert(r != NULL);
    assert(dst != NULL);

    u32 head = atomic_load_explicit(&r->head, memory_order_acquire);
    u32 tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    u32 avail = head - tail;
    u32 take = count < avail ? count : avail;

    u32 offset = tail % r->capacity;
    u32 first = r->capacity - offset;
    if (first > take)
        first = take;

    memcpy(dst, r->samples + offset, sizeof(f32) * (usize)first);
    if (take > first)
        memcpy(dst + first, r->samples, sizeof(f32) * (usize)(take - first));

    if (take < count)
        memset(dst + take, 0, sizeof(f32) * (usize)(count - take));

    atomic_store_explicit(&r->tail, tail + take, memory_order_release);

    if (take > 0u)
    {
        Mel_Sem* wake = atomic_load_explicit(&r->wake, memory_order_acquire);
        if (wake != NULL)
            mel_sem_post(wake);
    }

    return take;
}
