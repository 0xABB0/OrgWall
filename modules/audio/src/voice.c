#include <audio/voice.h>

#include "audio_internal.h"

#include <core/types.h>
#include <allocator/allocator.h>
#include <collection/slotmap.fwd.h>
#include <log/log.h>

void mel_audio__voices_init(Mel_Audio__Voice_Table* t, const Mel_Alloc* a, u32 initial_capacity)
{
    assert(t != NULL);
    assert(a != NULL);
    assert(initial_capacity > 0u);

    t->alloc = a;
    t->free_head = MEL_AUDIO__SLOT_SENTINEL;
    t->occupancy = 0u;
    t->lock = (Mel_Spinlock){ 0 };
    mel_array_init(&t->packed, a);
    mel_array_init(&t->slots, a);

    mel_array_reserve(&t->packed, initial_capacity);
    mel_array_reserve(&t->slots, initial_capacity);
    for (u32 i = 0; i < initial_capacity; i++)
    {
        Mel_Audio__Slot s = {
            .generation = 1u,
            .packed_idx = 0u,
            .state = MEL_AUDIO__SLOT_FREE,
            .next_free = (i + 1u < initial_capacity) ? (i + 1u) : MEL_AUDIO__SLOT_SENTINEL,
        };
        mel_array_push(&t->slots, s);
    }
    t->free_head = 0u;
}

void mel_audio__voices_free(Mel_Audio__Voice_Table* t)
{
    assert(t != NULL);
    mel_array_free(&t->packed);
    mel_array_free(&t->slots);
}

static Mel_Spinlock* mel_audio__table_lock(const Mel_Audio__Voice_Table* t) { return (Mel_Spinlock*)&t->lock; }

u32 mel_audio__voice_count(const Mel_Audio__Voice_Table* t)
{
    assert(t != NULL);
    Mel_Spinlock* lock = mel_audio__table_lock(t);
    mel_spinlock_lock(lock);
    u32 n = t->occupancy;
    mel_spinlock_unlock(lock);
    return n;
}

static u32 mel_audio__slots_grow_locked(Mel_Audio__Voice_Table* t)
{
    u32 old = (u32)t->slots.count;
    u32 add = old > 0u ? old : 16u;
    for (u32 i = 0; i < add; i++)
    {
        Mel_Audio__Slot s = {
            .generation = 1u,
            .packed_idx = 0u,
            .state = MEL_AUDIO__SLOT_FREE,
            .next_free = (i + 1u < add) ? (old + i + 1u) : MEL_AUDIO__SLOT_SENTINEL,
        };
        mel_array_push(&t->slots, s);
    }
    return old;
}

Mel_SlotMap_Handle mel_audio__voice_reserve(Mel_Audio* eng)
{
    assert(eng != NULL);
    Mel_Audio__Voice_Table* t = &eng->voices;

    mel_spinlock_lock(&t->lock);

    if (t->free_head == MEL_AUDIO__SLOT_SENTINEL)
    {
        u32 first_new = mel_audio__slots_grow_locked(t);
        t->free_head = first_new;
    }

    u32              idx = t->free_head;
    Mel_Audio__Slot* slot = &t->slots.items[idx];
    t->free_head = slot->next_free;
    slot->state = MEL_AUDIO__SLOT_RESERVED;
    slot->packed_idx = MEL_AUDIO__SLOT_SENTINEL;
    t->occupancy++;
    u32 gen = slot->generation;

    mel_spinlock_unlock(&t->lock);

    return mel_slotmap_handle_make(idx, gen);
}

Mel_Audio__Voice* mel_audio__voice_get(Mel_Audio__Voice_Table* t, Mel_SlotMap_Handle handle)
{
    assert(t != NULL);
    if (!mel_slotmap_handle_valid(handle))
        return NULL;

    mel_spinlock_lock(&t->lock);
    Mel_Audio__Voice* result = NULL;
    if (handle.index < (u32)t->slots.count)
    {
        Mel_Audio__Slot* slot = &t->slots.items[handle.index];
        if (slot->state == MEL_AUDIO__SLOT_LIVE && slot->generation == handle.generation)
            result = &t->packed.items[slot->packed_idx];
    }
    mel_spinlock_unlock(&t->lock);
    return result;
}

bool mel_audio__voice_alive(const Mel_Audio__Voice_Table* t, Mel_SlotMap_Handle handle)
{
    assert(t != NULL);
    if (!mel_slotmap_handle_valid(handle))
        return false;

    Mel_Spinlock* lock = mel_audio__table_lock(t);
    mel_spinlock_lock(lock);
    bool live = handle.index < (u32)t->slots.count && t->slots.items[handle.index].generation == handle.generation && t->slots.items[handle.index].state != MEL_AUDIO__SLOT_FREE;
    mel_spinlock_unlock(lock);
    return live;
}

bool mel_audio__voice_activate(Mel_Audio* eng, Mel_SlotMap_Handle handle, const Mel_Audio__Voice* payload)
{
    assert(eng != NULL);
    assert(payload != NULL);
    Mel_Audio__Voice_Table* t = &eng->voices;

    bool ok = false;
    mel_spinlock_lock(&t->lock);
    if (handle.index < (u32)t->slots.count)
    {
        Mel_Audio__Slot* slot = &t->slots.items[handle.index];
        if (slot->state == MEL_AUDIO__SLOT_RESERVED && slot->generation == handle.generation)
        {
            u32 packed_idx = (u32)t->packed.count;
            mel_array_push(&t->packed, *payload);
            slot->packed_idx = packed_idx;
            slot->state = MEL_AUDIO__SLOT_LIVE;
            ok = true;
        }
    }
    mel_spinlock_unlock(&t->lock);
    return ok;
}

static void mel_audio__voice_dispose(Mel_Audio* eng, Mel_Audio__Voice* v)
{
    if (v->source != NULL && v->instance != NULL && v->source->instance_free != NULL)
        v->source->instance_free(v->source, v->instance, eng->alloc);
    if (v->instance != NULL)
        mel_dealloc(eng->alloc, v->instance);
    if (v->tail != NULL)
        mel_dealloc(eng->alloc, v->tail);
}

void mel_audio__voice_remove(Mel_Audio* eng, Mel_SlotMap_Handle handle)
{
    assert(eng != NULL);
    Mel_Audio__Voice_Table* t = &eng->voices;

    Mel_Audio__Voice removed = { 0 };
    bool             have_removed = false;

    mel_spinlock_lock(&t->lock);
    if (handle.index < (u32)t->slots.count)
    {
        Mel_Audio__Slot* slot = &t->slots.items[handle.index];
        if (slot->state == MEL_AUDIO__SLOT_LIVE && slot->generation == handle.generation)
        {
            u32 packed_idx = slot->packed_idx;
            removed = t->packed.items[packed_idx];
            have_removed = true;

            u32 last = (u32)t->packed.count - 1u;
            if (packed_idx != last)
            {
                t->packed.items[packed_idx] = t->packed.items[last];
                t->slots.items[t->packed.items[packed_idx].self.index].packed_idx = packed_idx;
            }
            t->packed.count--;

            slot->state = MEL_AUDIO__SLOT_FREE;
            slot->generation++;
            slot->next_free = t->free_head;
            t->free_head = handle.index;
            t->occupancy--;
        }
    }
    mel_spinlock_unlock(&t->lock);

    if (have_removed)
    {
        mel_audio__end_future_resolve(eng, handle, MEL_FUTURE_OK);
        mel_audio__voice_dispose(eng, &removed);
    }
}

void mel_audio__voice_remove_reserved(Mel_Audio* eng, Mel_SlotMap_Handle handle, Mel_Audio_Source* source, void* instance, f32* tail)
{
    assert(eng != NULL);
    Mel_Audio__Voice_Table* t = &eng->voices;

    if (instance != NULL)
    {
        if (source != NULL && source->instance_free != NULL)
            source->instance_free(source, instance, eng->alloc);
        mel_dealloc(eng->alloc, instance);
    }
    if (tail != NULL)
        mel_dealloc(eng->alloc, tail);

    mel_spinlock_lock(&t->lock);
    if (handle.index < (u32)t->slots.count)
    {
        Mel_Audio__Slot* slot = &t->slots.items[handle.index];
        if (slot->state == MEL_AUDIO__SLOT_RESERVED && slot->generation == handle.generation)
        {
            slot->state = MEL_AUDIO__SLOT_FREE;
            slot->generation++;
            slot->next_free = t->free_head;
            t->free_head = handle.index;
            t->occupancy--;
        }
    }
    mel_spinlock_unlock(&t->lock);

    mel_audio__end_future_resolve(eng, handle, MEL_FUTURE_BROKEN);
}

static Mel_Audio_Voice mel_audio__play_impl(Mel_Audio* eng, Mel_Audio_Source* src, f32 volume, f32 pan, bool start_paused)
{
    assert(eng != NULL);
    assert(src != NULL);
    assert(src->get_audio != NULL);

    if (!mel_audio__api_enter(eng))
        return (Mel_Audio_Voice){ .slot = MEL_SLOTMAP_HANDLE_NULL };

    void* instance = NULL;
    if (src->instance_size > 0)
    {
        instance = mel_calloc(eng->alloc, src->instance_size);
        if (instance == NULL)
        {
            mel_log_error("audio", "play: instance alloc failed (%zu bytes)", src->instance_size);
            mel_audio__api_leave(eng);
            return (Mel_Audio_Voice){ .slot = MEL_SLOTMAP_HANDLE_NULL };
        }
        if (src->instance_init != NULL)
            src->instance_init(src, instance, eng->alloc);
    }

    u32  src_channels = src->channels >= 1u ? src->channels : 1u;
    f32* tail = mel_calloc(eng->alloc, sizeof(f32) * (usize)src_channels);
    if (tail == NULL)
    {
        mel_log_error("audio", "play: tail alloc failed (%u ch)", src_channels);
        if (instance != NULL)
        {
            if (src->instance_free != NULL)
                src->instance_free(src, instance, eng->alloc);
            mel_dealloc(eng->alloc, instance);
        }
        mel_audio__api_leave(eng);
        return (Mel_Audio_Voice){ .slot = MEL_SLOTMAP_HANDLE_NULL };
    }

    Mel_SlotMap_Handle handle = mel_audio__voice_reserve(eng);

    u32 flags = start_paused ? MEL_AUDIO_VOICE_PAUSED : 0u;

    Mel_Audio__Command cmd = {
        .apply = mel_audio__cmd_create,
        .handle = handle,
        .source = src,
        .instance = instance,
        .tail = tail,
        .f0 = volume,
        .f1 = pan,
        .d0 = 1.0,
        .u0 = flags,
    };
    mel_audio__command_push(eng, &cmd);
    mel_audio__api_leave(eng);

    return (Mel_Audio_Voice){ .slot = handle };
}

Mel_Audio_Voice mel_audio_play(Mel_Audio* eng, Mel_Audio_Source* src) { return mel_audio__play_impl(eng, src, 1.0f, 0.0f, false); }

Mel_Audio_Voice mel_audio_play_ex(Mel_Audio* eng, Mel_Audio_Source* src, f32 volume, f32 pan, bool start_paused) { return mel_audio__play_impl(eng, src, volume, pan, start_paused); }

bool mel_audio_voice_valid(const Mel_Audio* eng, Mel_Audio_Voice v)
{
    assert(eng != NULL);
    return mel_audio__voice_alive(&eng->voices, v.slot);
}

void mel_audio_set_volume(Mel_Audio* eng, Mel_Audio_Voice v, f32 volume)
{
    assert(eng != NULL);
    if (!mel_audio__api_enter(eng))
        return;
    Mel_Audio__Command cmd = { .apply = mel_audio__cmd_set_volume, .handle = v.slot, .f0 = volume };
    mel_audio__command_push(eng, &cmd);
    mel_audio__api_leave(eng);
}

void mel_audio_set_pan(Mel_Audio* eng, Mel_Audio_Voice v, f32 pan)
{
    assert(eng != NULL);
    if (!mel_audio__api_enter(eng))
        return;
    Mel_Audio__Command cmd = { .apply = mel_audio__cmd_set_pan, .handle = v.slot, .f0 = pan };
    mel_audio__command_push(eng, &cmd);
    mel_audio__api_leave(eng);
}

void mel_audio_set_play_speed(Mel_Audio* eng, Mel_Audio_Voice v, f64 ratio)
{
    assert(eng != NULL);
    if (!mel_audio__api_enter(eng))
        return;
    Mel_Audio__Command cmd = { .apply = mel_audio__cmd_set_speed, .handle = v.slot, .d0 = ratio };
    mel_audio__command_push(eng, &cmd);
    mel_audio__api_leave(eng);
}

void mel_audio_set_paused(Mel_Audio* eng, Mel_Audio_Voice v, bool paused)
{
    assert(eng != NULL);
    if (!mel_audio__api_enter(eng))
        return;
    Mel_Audio__Command cmd = { .apply = mel_audio__cmd_set_paused, .handle = v.slot, .u0 = paused ? 1u : 0u };
    mel_audio__command_push(eng, &cmd);
    mel_audio__api_leave(eng);
}

void mel_audio_set_looping(Mel_Audio* eng, Mel_Audio_Voice v, bool loop)
{
    assert(eng != NULL);
    if (!mel_audio__api_enter(eng))
        return;
    Mel_Audio__Command cmd = { .apply = mel_audio__cmd_set_loop, .handle = v.slot, .u0 = loop ? 1u : 0u };
    mel_audio__command_push(eng, &cmd);
    mel_audio__api_leave(eng);
}

void mel_audio_seek(Mel_Audio* eng, Mel_Audio_Voice v, f64 seconds)
{
    assert(eng != NULL);
    if (!mel_audio__api_enter(eng))
        return;
    Mel_Audio__Command cmd = { .apply = mel_audio__cmd_seek, .handle = v.slot, .d0 = seconds };
    mel_audio__command_push(eng, &cmd);
    mel_audio__api_leave(eng);
}

void mel_audio_stop(Mel_Audio* eng, Mel_Audio_Voice v)
{
    assert(eng != NULL);
    if (!mel_audio__api_enter(eng))
        return;
    Mel_Audio__Command cmd = { .apply = mel_audio__cmd_stop, .handle = v.slot };
    mel_audio__command_push(eng, &cmd);
    mel_audio__api_leave(eng);
}

void mel_audio_stop_all(Mel_Audio* eng)
{
    assert(eng != NULL);
    if (!mel_audio__api_enter(eng))
        return;
    Mel_Audio__Command cmd = { .apply = mel_audio__cmd_stop_all };
    mel_audio__command_push(eng, &cmd);
    mel_audio__api_leave(eng);
}
