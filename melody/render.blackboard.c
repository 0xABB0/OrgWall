#include "render.blackboard.h"
#include "allocator.h"
#include "string.str8.h"

static void grow(Mel_Render_Blackboard* bb)
{
    u32 new_cap = bb->capacity * 2;
    if (new_cap < 16) new_cap = 16;

    if (bb->keys)
    {
        bb->keys = mel_realloc(bb->alloc, bb->keys, sizeof(u64) * new_cap);
        bb->values = mel_realloc(bb->alloc, bb->values, sizeof(void*) * new_cap);
        bb->sizes = mel_realloc(bb->alloc, bb->sizes, sizeof(u32) * new_cap);
    }
    else
    {
        bb->keys = mel_alloc(bb->alloc, sizeof(u64) * new_cap);
        bb->values = mel_alloc(bb->alloc, sizeof(void*) * new_cap);
        bb->sizes = mel_alloc(bb->alloc, sizeof(u32) * new_cap);
    }
    bb->capacity = new_cap;
}

static i32 find_key(Mel_Render_Blackboard* bb, u64 key)
{
    for (u32 i = 0; i < bb->count; i++)
    {
        if (bb->keys[i] == key)
            return (i32)i;
    }
    return -1;
}

void mel_render_blackboard_init_opt(Mel_Render_Blackboard* bb, Mel_Render_Blackboard_Opt opt)
{
    assert(bb != nullptr);
    assert(opt.alloc != nullptr);

    *bb = (Mel_Render_Blackboard){0};
    bb->alloc = opt.alloc;

    if (opt.initial_capacity > 0)
    {
        bb->capacity = opt.initial_capacity;
        bb->keys = mel_alloc(bb->alloc, sizeof(u64) * bb->capacity);
        bb->values = mel_alloc(bb->alloc, sizeof(void*) * bb->capacity);
        bb->sizes = mel_alloc(bb->alloc, sizeof(u32) * bb->capacity);
    }
}

void mel_render_blackboard_shutdown(Mel_Render_Blackboard* bb)
{
    assert(bb != nullptr);

    for (u32 i = 0; i < bb->count; i++)
    {
        if (bb->values[i])
            mel_dealloc(bb->alloc, bb->values[i]);
    }

    if (bb->keys) mel_dealloc(bb->alloc, bb->keys);
    if (bb->values) mel_dealloc(bb->alloc, bb->values);
    if (bb->sizes) mel_dealloc(bb->alloc, bb->sizes);

    *bb = (Mel_Render_Blackboard){0};
}

void mel_render_blackboard_set(Mel_Render_Blackboard* bb, str8 name, const void* data, u32 size)
{
    assert(bb != nullptr);
    assert(!str8_is_empty(name));
    assert(data != nullptr);
    assert(size > 0);

    u64 key = str8_hash(name);
    i32 idx = find_key(bb, key);

    if (idx >= 0)
    {
        if (bb->sizes[idx] != size)
        {
            mel_dealloc(bb->alloc, bb->values[idx]);
            bb->values[idx] = mel_alloc(bb->alloc, size);
            bb->sizes[idx] = size;
        }
        memcpy(bb->values[idx], data, size);
        return;
    }

    if (bb->count >= bb->capacity)
        grow(bb);

    bb->keys[bb->count] = key;
    bb->values[bb->count] = mel_alloc(bb->alloc, size);
    memcpy(bb->values[bb->count], data, size);
    bb->sizes[bb->count] = size;
    bb->count++;
}

void* mel_render_blackboard_get(Mel_Render_Blackboard* bb, str8 name)
{
    assert(bb != nullptr);
    assert(!str8_is_empty(name));

    u64 key = str8_hash(name);
    i32 idx = find_key(bb, key);
    assert(idx >= 0 && "Blackboard key not found");

    return bb->values[idx];
}

bool mel_render_blackboard_has(Mel_Render_Blackboard* bb, str8 name)
{
    assert(bb != nullptr);
    assert(!str8_is_empty(name));

    u64 key = str8_hash(name);
    return find_key(bb, key) >= 0;
}

void mel_render_blackboard_clear(Mel_Render_Blackboard* bb)
{
    assert(bb != nullptr);

    for (u32 i = 0; i < bb->count; i++)
    {
        if (bb->values[i])
            mel_dealloc(bb->alloc, bb->values[i]);
    }
    bb->count = 0;
}
