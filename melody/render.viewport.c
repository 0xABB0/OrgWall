#include "render.viewport.h"
#include "render.view.registry.h"
#include "render.scene.h"
#include "render.pipeline.h"
#include "render.response.h"
#include "render.target.h"
#include "collection.slotmap.fwd.h"
#include "allocator.h"
#include "allocator.heap.h"

#include <string.h>

Mel_Render_View_Handle mel_render_view_create_opt(Mel_Render_View_Desc desc)
{
    assert(desc.dev != nullptr);

    const Mel_Alloc* alloc = desc.alloc ? desc.alloc : mel_alloc_heap();

    Mel_Render_View view = {0};
    view.scene = desc.scene;
    view.camera = desc.camera.view_count > 0 ? desc.camera : MEL_RENDER_CAMERA_DEFAULT;
    view.target = desc.target;
    view.dev = desc.dev;
    view.priority = desc.priority;
    view.active = true;
    view.design_width = desc.design_width;
    view.design_height = desc.design_height;
    view.scale_mode = desc.scale_mode;

    if (desc.design_width > 0 && desc.design_height > 0)
    {
        Mel_Gpu_Format fmt = MEL_GPU_FORMAT_B8G8R8A8_SRGB;
        if (mel_render_target_handle_valid(desc.target))
            fmt = mel_render_target_get(desc.target)->format;

        view.design_target = mel_render_target_offscreen(
            .dev = desc.dev,
            .width = desc.design_width,
            .height = desc.design_height,
            .format = fmt,
            .alloc = alloc);
    }

    Mel_SlotMap_Handle raw = mel__view_registry_insert(&view);

    Mel_Render_View* stored = mel__view_registry_get(raw);
    stored->self = (Mel_Render_View_Handle){ .handle = raw };

    const Mel_Render_Pipeline_Type* pipeline_type = nullptr;
    if (desc.pipeline.len > 0)
        pipeline_type = mel_pipeline_find(desc.pipeline);

    if (pipeline_type != nullptr)
    {
        Mel_Render_Pipeline_Scene* pipeline_scene = nullptr;
        if (desc.scene != nullptr)
            pipeline_scene = mel_render_scene_pipeline_scene(desc.scene, pipeline_type);
        stored->pipeline = mel_pipeline_create(pipeline_type, stored, pipeline_scene, alloc);
    }

    return (Mel_Render_View_Handle){ .handle = raw };
}

void mel_render_view_destroy(Mel_Render_View_Handle handle)
{
    if (!mel__view_registry_alive(handle.handle)) return;

    Mel_Render_View* view = mel__view_registry_get(handle.handle);
    assert(view != nullptr);

    if (view->pipeline)
        mel_pipeline_destroy(view->pipeline);

    mel_render_view_response_view_destroy(handle);

    if (mel_render_target_alive(view->design_target))
        mel_render_target_destroy(view->design_target);

    mel__view_registry_remove(handle.handle);
}

bool mel_render_view_alive(Mel_Render_View_Handle handle)
{
    return mel__view_registry_alive(handle.handle);
}

Mel_Render_View* mel_render_view_get(Mel_Render_View_Handle handle)
{
    Mel_Render_View* view = mel__view_registry_get(handle.handle);
    assert(view != nullptr);
    return view;
}

void mel_render_view_set_camera(Mel_Render_View_Handle handle, Mel_Render_Camera camera)
{
    Mel_Render_View* view = mel_render_view_get(handle);
    view->camera = camera;
}

void mel_render_view_set_active(Mel_Render_View_Handle handle, bool active)
{
    Mel_Render_View* view = mel_render_view_get(handle);
    view->active = active;
}

bool mel_render_view_has_design_resolution(Mel_Render_View* view)
{
    assert(view != nullptr);
    return view->design_width > 0 && view->design_height > 0
        && mel_render_target_alive(view->design_target);
}

Mel_Render_Target* mel_render_view_effective_target(Mel_Render_View* view)
{
    assert(view != nullptr);
    if (mel_render_target_alive(view->design_target))
        return mel_render_target_get(view->design_target);
    if (mel_render_target_alive(view->target))
        return mel_render_target_get(view->target);
    return nullptr;
}

void mel_render_view_sync(Mel_Render_View* view)
{
    assert(view != nullptr);

    if (!view->active || view->scene == nullptr)
        return;

    mel_render_scene_sync(view->scene);
}

void mel_render_view_draw(Mel_Render_View* view, Mel_Render_Draw_Ctx* ctx)
{
    assert(view != nullptr);
    assert(ctx != nullptr);

    if (!view->active || view->pipeline == nullptr)
        return;

    Mel_Render_Manager* mgr = view->scene ? mel_render_scene_manager(view->scene) : nullptr;

    if (view->scene != nullptr)
        view->pipeline->scene = mel_render_scene_pipeline_scene(view->scene, view->pipeline->type);

    Mel_Render_Draw_Ctx effective_ctx = *ctx;

    if (mel_render_view_has_design_resolution(view))
    {
        Mel_Render_Target* design = mel_render_target_get(view->design_target);
        effective_ctx.target = design;
        effective_ctx.target_width = view->design_width;
        effective_ctx.target_height = view->design_height;
        effective_ctx.target_format = mel_render_target_format(design);
    }

    mel_pipeline_begin_frame(view->pipeline, view);
    mel_pipeline_draw(view->pipeline, mgr, &effective_ctx);
}

void mel_render_view_destroy_by_target(Mel_Render_Target_Handle target)
{
    for (u32 i = mel__view_registry_count(); i > 0; i--)
    {
        Mel_Render_View* views = mel__view_registry_data();
        Mel_Render_View* v = &views[i - 1];

        if (v->target.handle.index != target.handle.index ||
            v->target.handle.generation != target.handle.generation) continue;

        Mel_Render_View_Handle h = mel__view_registry_handle_at(i - 1);
        mel_render_view_destroy(h);
    }
}
