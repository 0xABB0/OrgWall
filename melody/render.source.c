#include "render.source.h"
#include "render.list.h"
#include "render.target.h"
#include "collection.slotmap.h"
#include "allocator.heap.h"

struct Mel_Source {
    Mel_Source_Desc desc;
    Mel_Render_List* render_list;
    Mel_Render_Target* render_target;
};

static Mel_SlotMap s_sources;
static bool s_initialized;

__attribute__((constructor(210)))
static void mel__source_registry_init(void)
{
    mel_slotmap_init(&s_sources, mel_alloc_heap(),
        .item_size = sizeof(Mel_Source), .initial_capacity = 16);
    s_initialized = true;
}

__attribute__((destructor(210)))
static void mel__source_registry_shutdown(void)
{
    if (!s_initialized) return;
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
    };

    Mel_SlotMap_Handle raw = mel_slotmap_insert(&s_sources, &source);
    return (Mel_Source_Handle){ .handle = raw };
}

void mel_source_destroy(Mel_Source_Handle handle)
{
    assert(s_initialized);
    mel_slotmap_remove(&s_sources, handle.handle);
}

Mel_Source_Handle mel_source_from_render_list(Mel_Render_List* list, u32 schema)
{
    assert(list != nullptr);

    Mel_Source_Handle handle = mel_source_create(&(Mel_Source_Desc){
        .name = list->name,
        .kind = (list->mode == MEL_RENDER_LIST_EPHEMERAL) ? MEL_SOURCE_LIST : MEL_SOURCE_RETAINED,
        .schema = schema,
        .access_flags = MEL_SOURCE_ACCESS_CPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ,
        .lifetime = (list->mode == MEL_RENDER_LIST_EPHEMERAL)
            ? MEL_SOURCE_LIFETIME_FRAME
            : MEL_SOURCE_LIFETIME_RETAINED,
    });

    Mel_Source* source = mel__source_get(handle);
    source->render_list = list;
    return handle;
}

Mel_Source_Handle mel_source_from_target(Mel_Render_Target* target, u32 schema)
{
    assert(target != nullptr);

    Mel_Source_Handle handle = mel_source_create(&(Mel_Source_Desc){
        .name = target->name,
        .kind = MEL_SOURCE_TARGET,
        .schema = schema,
        .access_flags = MEL_SOURCE_ACCESS_GPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ,
        .lifetime = MEL_SOURCE_LIFETIME_EXTERNAL,
    });

    Mel_Source* source = mel__source_get(handle);
    source->render_target = target;
    return handle;
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

Mel_Render_List* mel_source_render_list(Mel_Source_Handle handle)
{
    return mel__source_get(handle)->render_list;
}

Mel_Render_Target* mel_source_target(Mel_Source_Handle handle)
{
    return mel__source_get(handle)->render_target;
}
