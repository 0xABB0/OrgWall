#include "render.pipeline.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "string.str8.h"

#include <string.h>

#define MEL_MAX_PIPELINE_TYPES 64

static const Mel_Render_Pipeline_Type* s_types[MEL_MAX_PIPELINE_TYPES];
static u32 s_type_count;

void mel_pipeline_register(const Mel_Render_Pipeline_Type* type)
{
    assert(type != nullptr);
    assert(type->name.len > 0);
    assert(type->draw != nullptr);
    assert(s_type_count < MEL_MAX_PIPELINE_TYPES);

    for (u32 i = 0; i < s_type_count; i++)
        assert(!str8_equals(s_types[i]->name, type->name));

    s_types[s_type_count++] = type;
}

const Mel_Render_Pipeline_Type* mel_pipeline_find(str8 name)
{
    for (u32 i = 0; i < s_type_count; i++)
    {
        if (str8_equals(s_types[i]->name, name))
            return s_types[i];
    }
    return nullptr;
}

u32 mel_pipeline_registered_count(void)
{
    return s_type_count;
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

void mel_pipeline_draw(Mel_Render_Pipeline* pipeline, Mel_Render_Manager* mgr)
{
    assert(pipeline != nullptr);
    assert(pipeline->type->draw != nullptr);

    pipeline->type->draw(pipeline, mgr);
}

void* mel_pipeline_instance(Mel_Render_Pipeline* pipeline)
{
    assert(pipeline != nullptr);
    return pipeline->instance;
}
