#include "render.viewport.h"
#include "render.view.registry.h"
#include "render.source.type.h"
#include "render.pipeline.h"
#include "allocator.h"
#include "allocator.heap.h"

#include <string.h>

Mel_Render_View* mel_render_view_create_opt(Mel_Render_View_Desc desc)
{
    assert(desc.source != nullptr);
    assert(desc.dev != nullptr);

    const Mel_Alloc* alloc = desc.alloc ? desc.alloc : mel_alloc_heap();

    Mel_Render_View* view = mel_alloc(alloc, sizeof(Mel_Render_View));
    memset(view, 0, sizeof(Mel_Render_View));

    view->source = desc.source;
    view->camera = desc.camera.view_count > 0 ? desc.camera : MEL_RENDER_CAMERA_DEFAULT;
    view->target = desc.target;
    view->dev = desc.dev;
    view->priority = desc.priority;
    view->active = true;

    mel__render_source_ensure_manager(desc.source, desc.dev, alloc);

    const Mel_Render_Pipeline_Type* pipeline_type = nullptr;
    if (desc.pipeline.len > 0)
        pipeline_type = mel_pipeline_find(desc.pipeline);

    if (pipeline_type != nullptr)
        view->pipeline = mel_pipeline_create(pipeline_type, view, alloc);

    mel__view_registry_add(view);

    return view;
}

void mel_render_view_destroy(Mel_Render_View* view)
{
    assert(view != nullptr);

    mel__view_registry_remove(view);

    if (view->pipeline)
        mel_pipeline_destroy(view->pipeline);

    mel_dealloc(mel_alloc_heap(), view);
}

void mel_render_view_set_camera(Mel_Render_View* view, Mel_Render_Camera camera)
{
    assert(view != nullptr);
    view->camera = camera;
}

void mel_render_view_set_active(Mel_Render_View* view, bool active)
{
    assert(view != nullptr);
    view->active = active;
}

void mel_render_view_sync(Mel_Render_View* view)
{
    assert(view != nullptr);

    if (!view->active || view->source == nullptr)
        return;

    mel_render_source_sync(view->source);
}

void mel_render_view_draw(Mel_Render_View* view, Mel_Render_Draw_Ctx* ctx)
{
    assert(view != nullptr);
    assert(ctx != nullptr);

    if (!view->active || view->pipeline == nullptr)
        return;

    void* mgr = mel_render_source_manager(view->source);
    if (mgr == nullptr)
        return;

    mel_pipeline_init_frame(view->pipeline, view);
    mel_pipeline_draw(view->pipeline, mgr, ctx);
}
