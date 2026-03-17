#include "render.source.type.h"
#include "render.manager.h"
#include "allocator.h"
#include "allocator.heap.h"

#include <string.h>

Mel_Render_Source* mel_render_source_create_opt(Mel_Render_Source_Create_Opt opt)
{
    assert(opt.type != nullptr);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    usize total = sizeof(Mel_Render_Source) + opt.type->instance_size;
    Mel_Render_Source* source = mel_alloc(alloc, total);
    memset(source, 0, total);

    source->type = opt.type;
    source->manager = nullptr;

    if (opt.type->instance_size > 0)
        source->instance = (u8*)source + sizeof(Mel_Render_Source);
    else
        source->instance = nullptr;

    return source;
}

void mel_render_source_destroy(Mel_Render_Source* source)
{
    assert(source != nullptr);

    if (source->type->shutdown)
        source->type->shutdown(source);

    mel_dealloc(mel_alloc_heap(), source);
}

void mel_render_source_sync(Mel_Render_Source* source, Mel_Render_Manager* mgr)
{
    assert(source != nullptr);
    assert(mgr != nullptr);
    assert(source->type->sync != nullptr);

    source->manager = mgr;
    source->type->sync(source, mgr);
}

void* mel_render_source_instance(Mel_Render_Source* source)
{
    assert(source != nullptr);
    return source->instance;
}
