#pragma once

#include "render.list.fwd.h"
#include "core.types.h"
#include "string.str8.fwd.h"
#include "allocator.fwd.h"
#include "gpu.buffer.h"
#include "gpu.device.fwd.h"

typedef void (*Mel_Render_Producer_Fn)(Mel_Render_List* list, void* user);

typedef struct {
    Mel_Render_Producer_Fn fn;
    void* user;
} Mel_Render_Producer;

struct Mel_Render_Packet {
    u64 sort_key;
    u32 entry_index;
};

struct Mel_Render_List {
    str8 name;
    u8* entries;
    u32 entry_stride;
    u32 slot_count;
    Mel_Render_Packet* packets;
    u32* packet_map;
    u32 count;
    u32 capacity;
    u32* free_indices;
    u32 free_count;
    u32 free_capacity;
    Mel_Render_Producer* producers;
    u32 producer_count;
    u32 producer_capacity;
    bool dirty;
    bool produced;
    bool gpu_backed;
    Mel_Gpu_Buffer gpu_buffer;
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
};

typedef struct {
    str8 name;
    u32 entry_stride;
    u32 initial_capacity;
    const Mel_Alloc* alloc;
} Mel_Render_List_Opt;

void mel_render_list_init_opt(Mel_Render_List* list, Mel_Render_List_Opt opt);
#define mel_render_list_init(list, ...) mel_render_list_init_opt((list), (Mel_Render_List_Opt){__VA_ARGS__})

void mel_render_list_shutdown(Mel_Render_List* list);

u32  mel_render_list_insert(Mel_Render_List* list, u64 sort_key);
void* mel_render_list_push(Mel_Render_List* list, u64 sort_key);
void* mel_render_list_get(Mel_Render_List* list, u32 entry_index);
void mel_render_list_remove(Mel_Render_List* list, u32 entry_index);
void mel_render_list_update_key(Mel_Render_List* list, u32 entry_index, u64 sort_key);
void mel_render_list_clear(Mel_Render_List* list);
void mel_render_list_sort(Mel_Render_List* list);

void mel_render_list_init_gpu_opt(Mel_Render_List* list, Mel_Gpu_Device* dev, Mel_Render_List_Opt opt);
#define mel_render_list_init_gpu(list, dev, ...) mel_render_list_init_gpu_opt((list), (dev), (Mel_Render_List_Opt){__VA_ARGS__})

void mel_render_list_flush(Mel_Render_List* list);

void mel_render_list_add_producer(Mel_Render_List* list, Mel_Render_Producer_Fn fn, void* user);
void mel_render_list_remove_producer(Mel_Render_List* list, Mel_Render_Producer_Fn fn);
void mel_render_list_produce(Mel_Render_List* list);
