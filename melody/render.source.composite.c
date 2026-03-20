#include "render.source.composite.h"
#include "render.source.type.h"
#include "collection.array.h"
#include "allocator.h"
#include "allocator.heap.h"

#include <assert.h>

typedef Mel_Array(Mel_Render_Source*) Mel_Composite_Children;

typedef struct {
    Mel_Composite_Children children;
} Mel_Composite_Source_Data;

static void composite_sync(Mel_Render_Source* self, Mel_Render_Manager* mgr)
{
    Mel_Composite_Source_Data* data = mel_render_source_instance(self);

    for (usize i = 0; i < data->children.count; i++)
    {
        Mel_Render_Source* child = data->children.items[i];
        mel_render_source_sync(child, mgr);
    }
}

static void composite_shutdown(Mel_Render_Source* self)
{
    Mel_Composite_Source_Data* data = mel_render_source_instance(self);
    mel_array_free(&data->children);
}

const Mel_Render_Source_Type mel_source_composite_type = {
    .name = { .data = (u8*)"composite", .len = 9 },
    .sync            = composite_sync,
    .shutdown        = composite_shutdown,
    .instance_size   = sizeof(Mel_Composite_Source_Data),
};

Mel_Render_Source* mel_source_composite_create(const Mel_Alloc* alloc)
{
    if (!alloc) alloc = mel_alloc_heap();

    Mel_Render_Source* source = mel_render_source_create(
        .type = &mel_source_composite_type,
        .alloc = alloc);

    Mel_Composite_Source_Data* data = mel_render_source_instance(source);
    mel_array_init(&data->children, alloc);

    return source;
}

void mel_source_composite_add(Mel_Render_Source* composite, Mel_Render_Source* child)
{
    assert(composite != nullptr);
    assert(child != nullptr);
    assert(composite->type == &mel_source_composite_type);

    Mel_Composite_Source_Data* data = mel_render_source_instance(composite);
    mel_array_push(&data->children, child);
}
