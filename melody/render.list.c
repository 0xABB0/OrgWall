#include "render.list.h"
#include "allocator.h"
#include "allocator.heap.h"
#include <string.h>

static void grow(Mel_Render_List* list)
{
    u32 new_cap = list->capacity == 0 ? 8 : list->capacity * 2;

    if (list->gpu_backed)
    {
        Mel_Gpu_Buffer new_buf;
        mel_gpu_buffer_init(&new_buf, list->dev,
            .size = (u64)new_cap * list->entry_stride,
            .usage = MEL_GPU_BUFFER_USAGE_STORAGE,
            .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU);

        if (list->entries)
            memcpy(new_buf.mapped, list->entries, (usize)list->capacity * list->entry_stride);

        mel_gpu_buffer_shutdown(&list->gpu_buffer, list->dev);
        list->gpu_buffer = new_buf;
        list->entries = (u8*)new_buf.mapped;
    }
    else
    {
        if (list->entries == nullptr)
            list->entries = mel_alloc(list->alloc, (usize)new_cap * list->entry_stride);
        else
            list->entries = mel_realloc(list->alloc, list->entries, (usize)new_cap * list->entry_stride);
    }

    if (list->packets == nullptr)
        list->packets = mel_alloc(list->alloc, sizeof(Mel_Render_Packet) * new_cap);
    else
        list->packets = mel_realloc(list->alloc, list->packets, sizeof(Mel_Render_Packet) * new_cap);

    if (list->packet_map == nullptr)
        list->packet_map = mel_alloc(list->alloc, sizeof(u32) * new_cap);
    else
        list->packet_map = mel_realloc(list->alloc, list->packet_map, sizeof(u32) * new_cap);

    list->capacity = new_cap;
}

static void grow_free(Mel_Render_List* list)
{
    u32 new_cap = list->free_capacity == 0 ? 8 : list->free_capacity * 2;

    if (list->free_indices == nullptr)
        list->free_indices = mel_alloc(list->alloc, sizeof(u32) * new_cap);
    else
        list->free_indices = mel_realloc(list->alloc, list->free_indices, sizeof(u32) * new_cap);

    list->free_capacity = new_cap;
}

static u32 alloc_slot(Mel_Render_List* list)
{
    if (list->free_count > 0)
        return list->free_indices[--list->free_count];

    if (list->slot_count >= list->capacity)
        grow(list);

    return list->slot_count++;
}

void mel_render_list_init_opt(Mel_Render_List* list, Mel_Render_List_Opt opt)
{
    assert(list != nullptr);
    assert(opt.entry_stride > 0);

    *list = (Mel_Render_List){0};
    list->name = opt.name;
    list->entry_stride = opt.entry_stride;
    list->alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    list->mode = opt.mode;
    list->last_frame_prepared = ~(u64)0;

    if (opt.initial_capacity > 0)
    {
        list->capacity = opt.initial_capacity;
        list->entries = mel_alloc(list->alloc, (usize)list->capacity * list->entry_stride);
        list->packets = mel_alloc(list->alloc, sizeof(Mel_Render_Packet) * list->capacity);
        list->packet_map = mel_alloc(list->alloc, sizeof(u32) * list->capacity);
    }
}

void mel_render_list_init_gpu_opt(Mel_Render_List* list, Mel_Gpu_Device* dev, Mel_Render_List_Opt opt)
{
    assert(list != nullptr);
    assert(dev != nullptr);
    assert(opt.entry_stride > 0);

    *list = (Mel_Render_List){0};
    list->name = opt.name;
    list->entry_stride = opt.entry_stride;
    list->alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    list->gpu_backed = true;
    list->dev = dev;
    list->mode = opt.mode;
    list->last_frame_prepared = ~(u64)0;

    u32 capacity = opt.initial_capacity > 0 ? opt.initial_capacity : 8;
    list->capacity = capacity;

    mel_gpu_buffer_init(&list->gpu_buffer, dev,
        .size = (u64)capacity * opt.entry_stride,
        .usage = MEL_GPU_BUFFER_USAGE_STORAGE,
        .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU);

    list->entries = (u8*)list->gpu_buffer.mapped;
    list->packets = mel_alloc(list->alloc, sizeof(Mel_Render_Packet) * capacity);
    list->packet_map = mel_alloc(list->alloc, sizeof(u32) * capacity);
}

void mel_render_list_shutdown(Mel_Render_List* list)
{
    assert(list != nullptr);

    if (list->gpu_backed)
        mel_gpu_buffer_shutdown(&list->gpu_buffer, list->dev);
    else if (list->entries)
        mel_dealloc(list->alloc, list->entries);

    if (list->packets)
        mel_dealloc(list->alloc, list->packets);
    if (list->packet_map)
        mel_dealloc(list->alloc, list->packet_map);
    if (list->free_indices)
        mel_dealloc(list->alloc, list->free_indices);
    if (list->producers)
        mel_dealloc(list->alloc, list->producers);

    *list = (Mel_Render_List){0};
}

u32 mel_render_list_insert(Mel_Render_List* list, u64 sort_key)
{
    assert(list != nullptr);

    u32 slot = alloc_slot(list);

    memset(list->entries + (usize)slot * list->entry_stride, 0, list->entry_stride);

    u32 packet_idx = list->count;
    list->packets[packet_idx] = (Mel_Render_Packet){
        .sort_key = sort_key,
        .entry_index = slot,
    };
    list->packet_map[slot] = packet_idx;
    list->count++;
    list->dirty = true;

    return slot;
}

void* mel_render_list_push(Mel_Render_List* list, u64 sort_key)
{
    u32 slot = mel_render_list_insert(list, sort_key);
    return list->entries + (usize)slot * list->entry_stride;
}

void* mel_render_list_get(Mel_Render_List* list, u32 entry_index)
{
    assert(list != nullptr);
    assert(entry_index < list->slot_count);
    return list->entries + (usize)entry_index * list->entry_stride;
}

void mel_render_list_remove(Mel_Render_List* list, u32 entry_index)
{
    assert(list != nullptr);
    assert(entry_index < list->slot_count);

    u32 pi = list->packet_map[entry_index];
    assert(pi < list->count);
    assert(list->packets[pi].entry_index == entry_index);

    u32 last = --list->count;
    if (pi != last)
    {
        list->packets[pi] = list->packets[last];
        list->packet_map[list->packets[pi].entry_index] = pi;
    }

    if (list->free_count >= list->free_capacity)
        grow_free(list);

    list->free_indices[list->free_count++] = entry_index;
    list->dirty = true;
}

void mel_render_list_update_key(Mel_Render_List* list, u32 entry_index, u64 sort_key)
{
    assert(list != nullptr);
    assert(entry_index < list->slot_count);

    u32 pi = list->packet_map[entry_index];
    assert(pi < list->count);
    assert(list->packets[pi].entry_index == entry_index);

    list->packets[pi].sort_key = sort_key;
    list->dirty = true;
}

void mel_render_list_clear(Mel_Render_List* list)
{
    assert(list != nullptr);

    list->count = 0;
    list->slot_count = 0;
    list->free_count = 0;
    list->dirty = false;
    list->produced = false;
}

void mel_render_list_sort(Mel_Render_List* list)
{
    assert(list != nullptr);

    if (list->count <= 1)
    {
        if (list->count == 1)
            list->packet_map[list->packets[0].entry_index] = 0;
        list->dirty = false;
        return;
    }

    Mel_Render_Packet* scratch = mel_alloc(list->alloc, sizeof(Mel_Render_Packet) * list->count);

    Mel_Render_Packet* src = list->packets;
    Mel_Render_Packet* dst = scratch;

    for (u32 byte_idx = 0; byte_idx < 8; byte_idx++)
    {
        u32 shift = byte_idx * 8;
        u32 counts[256] = {0};

        for (u32 i = 0; i < list->count; i++)
        {
            u8 key = (u8)(src[i].sort_key >> shift);
            counts[key]++;
        }

        u32 offsets[256];
        offsets[0] = 0;
        for (u32 i = 1; i < 256; i++)
            offsets[i] = offsets[i - 1] + counts[i - 1];

        for (u32 i = 0; i < list->count; i++)
        {
            u8 key = (u8)(src[i].sort_key >> shift);
            dst[offsets[key]++] = src[i];
        }

        Mel_Render_Packet* tmp = src;
        src = dst;
        dst = tmp;
    }

    if (src != list->packets)
        memcpy(list->packets, src, sizeof(Mel_Render_Packet) * list->count);

    mel_dealloc(list->alloc, scratch);

    for (u32 i = 0; i < list->count; i++)
        list->packet_map[list->packets[i].entry_index] = i;

    list->dirty = false;
}

void mel_render_list_flush(Mel_Render_List* list)
{
    assert(list != nullptr);
    assert(list->gpu_backed);
    mel_gpu_buffer_flush(&list->gpu_buffer, list->dev);
}

void mel_render_list_add_producer(Mel_Render_List* list, Mel_Render_Producer_Fn fn, void* user)
{
    assert(list != nullptr);
    assert(fn != nullptr);

    if (list->producer_count >= list->producer_capacity)
    {
        u32 new_cap = list->producer_capacity == 0 ? 4 : list->producer_capacity * 2;

        if (list->producers == nullptr)
            list->producers = mel_alloc(list->alloc, sizeof(Mel_Render_Producer) * new_cap);
        else
            list->producers = mel_realloc(list->alloc, list->producers, sizeof(Mel_Render_Producer) * new_cap);

        list->producer_capacity = new_cap;
    }

    list->producers[list->producer_count++] = (Mel_Render_Producer){ .fn = fn, .user = user };
}

void mel_render_list_remove_producer(Mel_Render_List* list, Mel_Render_Producer_Fn fn)
{
    assert(list != nullptr);
    assert(fn != nullptr);

    for (u32 i = 0; i < list->producer_count; i++)
    {
        if (list->producers[i].fn == fn)
        {
            list->producers[i] = list->producers[--list->producer_count];
            return;
        }
    }
}

void mel_render_list_begin_frame(Mel_Render_List* list, u64 frame_id)
{
    assert(list != nullptr);

    if (list->last_frame_prepared == frame_id)
        return;

    list->last_frame_prepared = frame_id;

    if (list->mode == MEL_RENDER_LIST_EPHEMERAL)
        mel_render_list_clear(list);
    else
        list->produced = false;
}

void mel_render_list_produce(Mel_Render_List* list)
{
    assert(list != nullptr);

    if (list->produced || list->producer_count == 0)
        return;

    list->produced = true;

    for (u32 i = 0; i < list->producer_count; i++)
        list->producers[i].fn(list, list->producers[i].user);
}
