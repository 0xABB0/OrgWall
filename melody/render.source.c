#include "render.source.h"
#include "render.list.h"
#include "render.target.h"
#include "render.material.h"
#include "collection.slotmap.h"
#include "collection.array.h"
#include "allocator.heap.h"
#include "string.str8.h"

struct Mel_Source {
    Mel_Source_Desc desc;
    Mel_Render_List* render_list;
    Mel_Gpu_Buffer* gpu_buffer;
    Mel_Render_Target* render_target;
    u32 refcount;
    u64 shape_version;
    bool cached_wrapper;
};

typedef struct {
    void* backing;
    u32 schema;
    Mel_Source_Kind kind;
    Mel_Source_Handle handle;
} Mel__Source_Wrapper_Cache_Entry;

static Mel_SlotMap s_sources;
static Mel_Array(Mel__Source_Wrapper_Cache_Entry) s_wrapper_cache;
static bool s_initialized;

__attribute__((constructor(210)))
static void mel__source_registry_init(void)
{
    mel_slotmap_init(&s_sources, mel_alloc_heap(),
        .item_size = sizeof(Mel_Source), .initial_capacity = 16);
    mel_array_init(&s_wrapper_cache, mel_alloc_heap());
    s_initialized = true;
}

__attribute__((destructor(210)))
static void mel__source_registry_shutdown(void)
{
    if (!s_initialized) return;
    mel_array_free(&s_wrapper_cache);
    mel_slotmap_free(&s_sources);
    s_initialized = false;
}

static Mel_Source* mel__source_get(Mel_Source_Handle handle)
{
    assert(s_initialized);
    Mel_Source* source = mel_slotmap_get(&s_sources, handle.handle);
    assert(source != nullptr);
    return source;
}

Mel_Source_Handle mel_source_create(const Mel_Source_Desc* desc)
{
    assert(s_initialized);
    assert(desc != nullptr);

    Mel_Source source = {
        .desc = *desc,
        .refcount = 1,
        .shape_version = 1,
    };

    Mel_SlotMap_Handle raw = mel_slotmap_insert(&s_sources, &source);
    return (Mel_Source_Handle){ .handle = raw };
}

void mel_source_destroy(Mel_Source_Handle handle)
{
    assert(s_initialized);
    Mel_Source* source = mel__source_get(handle);

    if (source->cached_wrapper && source->refcount > 1)
    {
        source->refcount--;
        return;
    }

    if (source->cached_wrapper)
    {
        for (usize i = 0; i < s_wrapper_cache.count; i++)
        {
            if (s_wrapper_cache.items[i].handle.handle.index == handle.handle.index &&
                s_wrapper_cache.items[i].handle.handle.generation == handle.handle.generation)
            {
                mel_array_remove_ordered(&s_wrapper_cache, i);
                break;
            }
        }
    }

    mel_slotmap_remove(&s_sources, handle.handle);
}

Mel_Source_Handle mel_source_from_render_list(Mel_Render_List* list, u32 schema)
{
    assert(list != nullptr);

    Mel_Source_Kind kind = (list->mode == MEL_RENDER_LIST_EPHEMERAL) ? MEL_SOURCE_LIST : MEL_SOURCE_RETAINED;
    for (usize i = 0; i < s_wrapper_cache.count; i++)
    {
        if (s_wrapper_cache.items[i].backing == list &&
            s_wrapper_cache.items[i].schema == schema &&
            s_wrapper_cache.items[i].kind == kind)
        {
            Mel_Source* existing = mel__source_get(s_wrapper_cache.items[i].handle);
            existing->refcount++;
            return s_wrapper_cache.items[i].handle;
        }
    }

    Mel_Source_Handle handle = mel_source_create(&(Mel_Source_Desc){
        .name = list->name,
        .kind = kind,
        .schema = schema,
        .access_flags = MEL_SOURCE_ACCESS_CPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ,
        .lifetime = (kind == MEL_SOURCE_LIST) ? MEL_SOURCE_LIFETIME_FRAME : MEL_SOURCE_LIFETIME_RETAINED,
    });

    Mel_Source* source = mel__source_get(handle);
    source->render_list = list;
    source->cached_wrapper = true;
    mel_array_push(&s_wrapper_cache, ((Mel__Source_Wrapper_Cache_Entry){
        .backing = list,
        .schema = schema,
        .kind = kind,
        .handle = handle,
    }));
    return handle;
}

Mel_Source_Handle mel_source_from_gpu_buffer(Mel_Gpu_Buffer* buffer, u32 schema)
{
    assert(buffer != nullptr);

    for (usize i = 0; i < s_wrapper_cache.count; i++)
    {
        if (s_wrapper_cache.items[i].backing == buffer &&
            s_wrapper_cache.items[i].schema == schema &&
            s_wrapper_cache.items[i].kind == MEL_SOURCE_GPU_BUFFER)
        {
            Mel_Source* existing = mel__source_get(s_wrapper_cache.items[i].handle);
            existing->refcount++;
            return s_wrapper_cache.items[i].handle;
        }
    }

    Mel_Source_Handle handle = mel_source_create(&(Mel_Source_Desc){
        .name = S8("gpu_buffer"),
        .kind = MEL_SOURCE_GPU_BUFFER,
        .schema = schema,
        .access_flags = MEL_SOURCE_ACCESS_GPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ,
        .lifetime = MEL_SOURCE_LIFETIME_EXTERNAL,
    });

    Mel_Source* source = mel__source_get(handle);
    source->gpu_buffer = buffer;
    source->cached_wrapper = true;
    mel_array_push(&s_wrapper_cache, ((Mel__Source_Wrapper_Cache_Entry){
        .backing = buffer,
        .schema = schema,
        .kind = MEL_SOURCE_GPU_BUFFER,
        .handle = handle,
    }));
    return handle;
}

Mel_Source_Handle mel_source_from_material_table(Mel_Material_Table* table)
{
    assert(table != nullptr);
    assert(table->dev != nullptr);

    for (usize i = 0; i < s_wrapper_cache.count; i++)
    {
        if (s_wrapper_cache.items[i].backing == table &&
            s_wrapper_cache.items[i].schema == MEL_SCHEMA_MATERIAL_TABLE &&
            s_wrapper_cache.items[i].kind == MEL_SOURCE_GPU_BUFFER)
        {
            Mel_Source* existing = mel__source_get(s_wrapper_cache.items[i].handle);
            existing->refcount++;
            return s_wrapper_cache.items[i].handle;
        }
    }

    Mel_Source_Handle handle = mel_source_create(&(Mel_Source_Desc){
        .name = S8("material_table"),
        .kind = MEL_SOURCE_GPU_BUFFER,
        .schema = MEL_SCHEMA_MATERIAL_TABLE,
        .access_flags = MEL_SOURCE_ACCESS_CPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ,
        .lifetime = MEL_SOURCE_LIFETIME_RETAINED,
        .user = table,
    });

    Mel_Source* source = mel__source_get(handle);
    source->gpu_buffer = &table->buffer;
    source->cached_wrapper = true;
    mel_array_push(&s_wrapper_cache, ((Mel__Source_Wrapper_Cache_Entry){
        .backing = table,
        .schema = MEL_SCHEMA_MATERIAL_TABLE,
        .kind = MEL_SOURCE_GPU_BUFFER,
        .handle = handle,
    }));
    return handle;
}

Mel_Source_Handle mel_source_from_target(Mel_Render_Target* target, u32 schema)
{
    assert(target != nullptr);

    for (usize i = 0; i < s_wrapper_cache.count; i++)
    {
        if (s_wrapper_cache.items[i].backing == target &&
            s_wrapper_cache.items[i].schema == schema &&
            s_wrapper_cache.items[i].kind == MEL_SOURCE_TARGET)
        {
            Mel_Source* existing = mel__source_get(s_wrapper_cache.items[i].handle);
            existing->refcount++;
            return s_wrapper_cache.items[i].handle;
        }
    }

    Mel_Source_Handle handle = mel_source_create(&(Mel_Source_Desc){
        .name = target->name,
        .kind = MEL_SOURCE_TARGET,
        .schema = schema,
        .access_flags = MEL_SOURCE_ACCESS_GPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ,
        .lifetime = MEL_SOURCE_LIFETIME_EXTERNAL,
    });

    Mel_Source* source = mel__source_get(handle);
    source->render_target = target;
    source->cached_wrapper = true;
    mel_array_push(&s_wrapper_cache, ((Mel__Source_Wrapper_Cache_Entry){
        .backing = target,
        .schema = schema,
        .kind = MEL_SOURCE_TARGET,
        .handle = handle,
    }));
    return handle;
}

u32 mel_source_refcount(Mel_Source_Handle handle)
{
    return mel__source_get(handle)->refcount;
}

str8 mel_source_name(Mel_Source_Handle handle)
{
    return mel__source_get(handle)->desc.name;
}

Mel_Source_Kind mel_source_kind(Mel_Source_Handle handle)
{
    return mel__source_get(handle)->desc.kind;
}

u32 mel_source_schema(Mel_Source_Handle handle)
{
    return mel__source_get(handle)->desc.schema;
}

u32 mel_source_access_flags(Mel_Source_Handle handle)
{
    return mel__source_get(handle)->desc.access_flags;
}

u32 mel_source_lifetime(Mel_Source_Handle handle)
{
    return mel__source_get(handle)->desc.lifetime;
}

void* mel_source_user(Mel_Source_Handle handle)
{
    return mel__source_get(handle)->desc.user;
}

u64 mel_source_shape_version(Mel_Source_Handle handle)
{
    return mel__source_get(handle)->shape_version;
}

Mel_Render_List* mel_source_render_list(Mel_Source_Handle handle)
{
    return mel__source_get(handle)->render_list;
}

void mel_source_set_render_list(Mel_Source_Handle handle, Mel_Render_List* list)
{
    Mel_Source* source = mel__source_get(handle);
    assert(!source->cached_wrapper);
    assert(source->desc.kind == MEL_SOURCE_LIST || source->desc.kind == MEL_SOURCE_RETAINED);
    if (source->render_list == list)
        return;
    source->render_list = list;
    source->shape_version++;
}

Mel_Gpu_Buffer* mel_source_gpu_buffer(Mel_Source_Handle handle)
{
    return mel__source_get(handle)->gpu_buffer;
}

void mel_source_set_gpu_buffer(Mel_Source_Handle handle, Mel_Gpu_Buffer* buffer)
{
    Mel_Source* source = mel__source_get(handle);
    assert(!source->cached_wrapper);
    assert(source->desc.kind == MEL_SOURCE_GPU_BUFFER);
    if (source->gpu_buffer == buffer)
        return;
    source->gpu_buffer = buffer;
    source->shape_version++;
}

Mel_Render_Target* mel_source_target(Mel_Source_Handle handle)
{
    return mel__source_get(handle)->render_target;
}

void mel_source_set_target(Mel_Source_Handle handle, Mel_Render_Target* target)
{
    Mel_Source* source = mel__source_get(handle);
    assert(!source->cached_wrapper);
    assert(source->desc.kind == MEL_SOURCE_TARGET);
    if (source->render_target == target)
        return;
    source->render_target = target;
    source->shape_version++;
}
