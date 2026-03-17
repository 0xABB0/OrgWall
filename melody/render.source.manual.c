#include "render.source.manual.h"
#include "render.source.type.h"
#include "render.manager.h"
#include "collection.array.h"
#include "allocator.h"
#include "allocator.heap.h"

typedef struct {
    Mel_Render_Handle handle;
    Mel_Mat4 transform;
    Mel_Render_Bounds bounds;
    Mel_Render_Info info;
} Mel_Manual_Pending_Add;

typedef Mel_Array(Mel_Manual_Pending_Add) Mel_Manual_Add_List;
typedef Mel_Array(Mel_Render_Handle) Mel_Manual_Remove_List;

typedef struct {
    Mel_Manual_Add_List pending_adds;
    Mel_Manual_Remove_List pending_removes;
    bool has_pending;
} Mel_Manual_Source_Data;

static void* manual_create_manager(Mel_Render_Source* self, Mel_Gpu_Device* dev, const Mel_Alloc* alloc)
{
    (void)self;
    Mel_Render_Manager* mgr = mel_alloc(alloc, sizeof(Mel_Render_Manager));
    mel_mgr_init(mgr, .dev = dev, .alloc = alloc);
    return mgr;
}

static void manual_destroy_manager(Mel_Render_Source* self, void* mgr)
{
    (void)self;
    Mel_Render_Manager* m = mgr;
    mel_mgr_shutdown(m);
    mel_dealloc(mel_alloc_heap(), m);
}

static void manual_sync(Mel_Render_Source* self, void* mgr)
{
    Mel_Manual_Source_Data* data = mel_render_source_instance(self);
    Mel_Render_Manager* m = mgr;

    if (!data->has_pending)
        return;

    for (usize i = 0; i < data->pending_removes.count; i++)
        mel_mgr_free(m, data->pending_removes.items[i]);
    data->pending_removes.count = 0;

    for (usize i = 0; i < data->pending_adds.count; i++)
    {
        Mel_Manual_Pending_Add* add = &data->pending_adds.items[i];
        mel_mgr_set_transform(m, add->handle, add->transform);
        mel_mgr_set_bounds(m, add->handle, add->bounds);
        mel_mgr_set_info(m, add->handle, add->info);
    }
    data->pending_adds.count = 0;

    data->has_pending = false;
}

static void manual_shutdown(Mel_Render_Source* self)
{
    Mel_Manual_Source_Data* data = mel_render_source_instance(self);
    mel_array_free(&data->pending_adds);
    mel_array_free(&data->pending_removes);
}

const Mel_Render_Source_Type mel_source_manual_type = {
    .name = { .data = (u8*)"manual", .len = 6 },
    .create_manager = manual_create_manager,
    .destroy_manager = manual_destroy_manager,
    .sync = manual_sync,
    .shutdown = manual_shutdown,
    .instance_size = sizeof(Mel_Manual_Source_Data),
};

Mel_Render_Source* mel_source_manual_create(const Mel_Alloc* alloc)
{
    if (!alloc) alloc = mel_alloc_heap();

    Mel_Render_Source* source = mel_render_source_create(
        .type = &mel_source_manual_type,
        .alloc = alloc);

    Mel_Manual_Source_Data* data = mel_render_source_instance(source);
    mel_array_init(&data->pending_adds, alloc);
    mel_array_init(&data->pending_removes, alloc);

    return source;
}

Mel_Render_Handle mel_source_manual_add(Mel_Render_Source* source,
                                         Mel_Mat4 transform,
                                         Mel_Render_Bounds bounds,
                                         Mel_Render_Info info)
{
    assert(source != nullptr);
    assert(source->manager != nullptr);

    Mel_Manual_Source_Data* data = mel_render_source_instance(source);
    Mel_Render_Manager* mgr = source->manager;

    Mel_Render_Handle h = mel_mgr_alloc(mgr);

    mel_array_push(&data->pending_adds, ((Mel_Manual_Pending_Add){
        .handle = h,
        .transform = transform,
        .bounds = bounds,
        .info = info,
    }));

    data->has_pending = true;
    return h;
}

void mel_source_manual_remove(Mel_Render_Source* source, Mel_Render_Handle h)
{
    assert(source != nullptr);

    Mel_Manual_Source_Data* data = mel_render_source_instance(source);
    mel_array_push(&data->pending_removes, h);
    data->has_pending = true;
}

void mel_source_manual_set_transform(Mel_Render_Source* source,
                                      Mel_Render_Handle h, Mel_Mat4 transform)
{
    assert(source != nullptr);
    assert(source->manager != nullptr);
    mel_mgr_set_transform(source->manager, h, transform);
}

void mel_source_manual_set_bounds(Mel_Render_Source* source,
                                   Mel_Render_Handle h, Mel_Render_Bounds bounds)
{
    assert(source != nullptr);
    assert(source->manager != nullptr);
    mel_mgr_set_bounds(source->manager, h, bounds);
}

void mel_source_manual_set_info(Mel_Render_Source* source,
                                 Mel_Render_Handle h, Mel_Render_Info info)
{
    assert(source != nullptr);
    assert(source->manager != nullptr);
    mel_mgr_set_info(source->manager, h, info);
}
