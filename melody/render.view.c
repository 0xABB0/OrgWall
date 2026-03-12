#include "render.view.h"
#include "collection.slotmap.h"
#include "collection.array.h"
#include "allocator.heap.h"

struct Mel_View {
    Mel_View_Desc desc;
    Mel_Array(Mel_Source_Handle) sources;
};

static Mel_SlotMap s_views;
static bool s_initialized;

__attribute__((constructor(211)))
static void mel__view_registry_init(void)
{
    mel_slotmap_init(&s_views, mel_alloc_heap(),
        .item_size = sizeof(Mel_View), .initial_capacity = 16);
    s_initialized = true;
}

__attribute__((destructor(211)))
static void mel__view_registry_shutdown(void)
{
    if (!s_initialized) return;

    Mel_View* views = mel_slotmap_data(&s_views);
    u32 count = mel_slotmap_count(&s_views);
    for (u32 i = 0; i < count; i++)
        mel_array_free(&views[i].sources);

    mel_slotmap_free(&s_views);
    s_initialized = false;
}

static Mel_View* mel__view_get(Mel_View_Handle handle)
{
    assert(s_initialized);
    Mel_View* view = mel_slotmap_get(&s_views, handle.handle);
    assert(view != nullptr);
    return view;
}

Mel_View_Handle mel_view_create(const Mel_View_Desc* desc)
{
    assert(s_initialized);
    assert(desc != nullptr);

    Mel_View view = {
        .desc = *desc,
    };
    mel_array_init(&view.sources, mel_alloc_heap());

    Mel_SlotMap_Handle raw = mel_slotmap_insert(&s_views, &view);
    return (Mel_View_Handle){ .handle = raw };
}

void mel_view_destroy(Mel_View_Handle handle)
{
    assert(s_initialized);
    Mel_View* view = mel__view_get(handle);
    mel_array_free(&view->sources);
    mel_slotmap_remove(&s_views, handle.handle);
}

void mel_view_attach_source(Mel_View_Handle handle, Mel_Source_Handle source)
{
    Mel_View* view = mel__view_get(handle);

    for (usize i = 0; i < view->sources.count; i++)
        if (view->sources.items[i].handle.index == source.handle.index &&
            view->sources.items[i].handle.generation == source.handle.generation)
            return;

    mel_array_push(&view->sources, source);
}

void mel_view_detach_source(Mel_View_Handle handle, Mel_Source_Handle source)
{
    Mel_View* view = mel__view_get(handle);

    for (usize i = 0; i < view->sources.count; i++)
    {
        if (view->sources.items[i].handle.index == source.handle.index &&
            view->sources.items[i].handle.generation == source.handle.generation)
        {
            mel_array_remove_ordered(&view->sources, i);
            return;
        }
    }
}

str8 mel_view_name(Mel_View_Handle handle)
{
    return mel__view_get(handle)->desc.name;
}

const Mel_Camera* mel_view_camera(Mel_View_Handle handle)
{
    return mel__view_get(handle)->desc.camera;
}

bool mel_view_clear_color_enabled(Mel_View_Handle handle)
{
    return mel__view_get(handle)->desc.clear_color_enabled;
}

Mel_Vec4 mel_view_clear_color(Mel_View_Handle handle)
{
    return mel__view_get(handle)->desc.clear_color;
}

u32 mel_view_composition_mode(Mel_View_Handle handle)
{
    return mel__view_get(handle)->desc.composition_mode;
}

void* mel_view_user(Mel_View_Handle handle)
{
    return mel__view_get(handle)->desc.user;
}

u32 mel_view_source_count(Mel_View_Handle handle)
{
    return (u32)mel__view_get(handle)->sources.count;
}

Mel_Source_Handle mel_view_source_at(Mel_View_Handle handle, u32 index)
{
    Mel_View* view = mel__view_get(handle);
    assert(index < (u32)view->sources.count);
    return view->sources.items[index];
}
