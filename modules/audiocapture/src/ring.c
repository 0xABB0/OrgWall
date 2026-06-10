#include "audiocapture_internal.h"

#include <assert.h>
#include <string.h>

void mel_ac_ring_init(Mel_AC_Ring* r, const Mel_Alloc* alloc, u32 capacity)
{
    assert(capacity > 0);
    r->samples = mel_alloc_array(alloc, f32, capacity);
    r->capacity = capacity;
    atomic_store_explicit(&r->head, 0, memory_order_relaxed);
    atomic_store_explicit(&r->tail, 0, memory_order_relaxed);
}

void mel_ac_ring_free(Mel_AC_Ring* r, const Mel_Alloc* alloc)
{
    mel_dealloc(alloc, r->samples);
    r->samples = NULL;
}

u32 mel_ac_ring_write(Mel_AC_Ring* r, const f32* src, u32 count)
{
    u32 head = atomic_load_explicit(&r->head, memory_order_relaxed);
    u32 tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    u32 free_space = r->capacity - (head - tail);
    u32 n = count < free_space ? count : free_space;

    for (u32 i = 0; i < n; i++)
        r->samples[(head + i) % r->capacity] = src[i];

    atomic_store_explicit(&r->head, head + n, memory_order_release);
    return n;
}

u32 mel_ac_ring_read(Mel_AC_Ring* r, f32* dst, u32 count)
{
    u32 tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    u32 head = atomic_load_explicit(&r->head, memory_order_acquire);
    u32 avail = head - tail;
    u32 n = count < avail ? count : avail;

    for (u32 i = 0; i < n; i++)
        dst[i] = r->samples[(tail + i) % r->capacity];

    atomic_store_explicit(&r->tail, tail + n, memory_order_release);
    return n;
}

u32 mel_ac_ring_available(const Mel_AC_Ring* r)
{
    u32 head = atomic_load_explicit((_Atomic(u32)*)&r->head, memory_order_acquire);
    u32 tail = atomic_load_explicit((_Atomic(u32)*)&r->tail, memory_order_relaxed);
    return head - tail;
}
