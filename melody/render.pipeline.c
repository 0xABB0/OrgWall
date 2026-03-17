#include "render.pipeline.h"
#include "collection.array.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "string.str8.h"

#include <string.h>

typedef Mel_Array(const Mel_Render_Pipeline_Type*) Mel_Pipeline_Type_Array;

static Mel_Pipeline_Type_Array s_types;
static bool s_initialized;

__attribute__((constructor))
static void mel__pipeline_registry_init(void)
{
    mel_array_init(&s_types, mel_alloc_heap());
    s_initialized = true;
}

__attribute__((destructor))
static void mel__pipeline_registry_shutdown(void)
{
    if (!s_initialized) return;
    mel_array_free(&s_types);
    s_initialized = false;
}

void mel_pipeline_register(const Mel_Render_Pipeline_Type* type)
{
    assert(type != nullptr);
    assert(type->name.len > 0);
    assert(type->draw != nullptr);
    assert(s_initialized);

    for (usize i = 0; i < s_types.count; i++)
        assert(!str8_equals(s_types.items[i]->name, type->name));

    mel_array_push(&s_types, type);
}

const Mel_Render_Pipeline_Type* mel_pipeline_find(str8 name)
{
    for (usize i = 0; i < s_types.count; i++)
    {
        if (str8_equals(s_types.items[i]->name, name))
            return s_types.items[i];
    }
    return nullptr;
}

u32 mel_pipeline_registered_count(void)
{
    return (u32)s_types.count;
}

Mel_Render_Pipeline* mel_pipeline_create(const Mel_Render_Pipeline_Type* type,
                                          Mel_Render_View* view,
                                          const Mel_Alloc* alloc)
{
    assert(type != nullptr);
    if (!alloc) alloc = mel_alloc_heap();

    usize total = sizeof(Mel_Render_Pipeline) + type->instance_size;
    Mel_Render_Pipeline* pipeline = mel_alloc(alloc, total);
    memset(pipeline, 0, total);

    pipeline->type = type;
    pipeline->view = view;

    if (type->instance_size > 0)
        pipeline->instance = (u8*)pipeline + sizeof(Mel_Render_Pipeline);
    else
        pipeline->instance = nullptr;

    if (type->init)
        type->init(pipeline, view);

    return pipeline;
}

void mel_pipeline_destroy(Mel_Render_Pipeline* pipeline)
{
    assert(pipeline != nullptr);

    if (pipeline->type->shutdown)
        pipeline->type->shutdown(pipeline);

    mel_dealloc(mel_alloc_heap(), pipeline);
}

void mel_pipeline_init_frame(Mel_Render_Pipeline* pipeline, Mel_Render_View* view)
{
    assert(pipeline != nullptr);

    pipeline->view = view;
    if (pipeline->type->init)
        pipeline->type->init(pipeline, view);
}

void mel_pipeline_draw(Mel_Render_Pipeline* pipeline, void* mgr, Mel_Render_Draw_Ctx* ctx)
{
    assert(pipeline != nullptr);
    assert(pipeline->type->draw != nullptr);
    assert(ctx != nullptr);

    pipeline->type->draw(pipeline, mgr, ctx);
}

void* mel_pipeline_instance(Mel_Render_Pipeline* pipeline)
{
    assert(pipeline != nullptr);
    return pipeline->instance;
}
