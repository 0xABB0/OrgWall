#include "render.viewport.h"
#include "render.source.type.h"
#include "render.pipeline.h"
#include "render.manager.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "math.vec3.h"

#include <string.h>
#include <math.h>

Mel_Render_View* mel_render_view_create_opt(Mel_Render_View_Desc desc)
{
    assert(desc.source != nullptr);

    const Mel_Alloc* alloc = desc.alloc ? desc.alloc : mel_alloc_heap();

    Mel_Render_View* view = mel_alloc(alloc, sizeof(Mel_Render_View));
    memset(view, 0, sizeof(Mel_Render_View));

    view->source = desc.source;
    view->camera = desc.camera.view_count > 0 ? desc.camera : MEL_RENDER_CAMERA_DEFAULT;
    view->target = desc.target;
    view->priority = desc.priority;
    view->active = true;

    view->manager = desc.source->manager;

    const Mel_Render_Pipeline_Type* pipeline_type = nullptr;
    if (desc.pipeline.len > 0)
        pipeline_type = mel_pipeline_find(desc.pipeline);

    if (pipeline_type == nullptr)
        pipeline_type = mel_pipeline_find(S8("default_2d"));

    if (pipeline_type != nullptr)
        view->pipeline = mel_pipeline_create(pipeline_type, view, alloc);

    mel_bitset_init(&view->visibility, 256, alloc);

    return view;
}

void mel_render_view_destroy(Mel_Render_View* view)
{
    assert(view != nullptr);

    if (view->pipeline)
        mel_pipeline_destroy(view->pipeline);

    mel_bitset_free(&view->visibility);
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

Mel_Frustum mel_render_view_extract_frustum(const Mel_Render_Camera* camera)
{
    assert(camera != nullptr);

    Mel_Mat4 vp = mel_mat4_mul(camera->projection, camera->view);

    Mel_Vec4 r0 = mel_vec4(vp.m[0][0], vp.m[0][1], vp.m[0][2], vp.m[0][3]);
    Mel_Vec4 r1 = mel_vec4(vp.m[1][0], vp.m[1][1], vp.m[1][2], vp.m[1][3]);
    Mel_Vec4 r2 = mel_vec4(vp.m[2][0], vp.m[2][1], vp.m[2][2], vp.m[2][3]);
    Mel_Vec4 r3 = mel_vec4(vp.m[3][0], vp.m[3][1], vp.m[3][2], vp.m[3][3]);

    Mel_Frustum f;
    f.planes[0] = mel_plane_normalize(mel_vec4_add(r3, r0));
    f.planes[1] = mel_plane_normalize(mel_vec4_sub(r3, r0));
    f.planes[2] = mel_plane_normalize(mel_vec4_add(r3, r1));
    f.planes[3] = mel_plane_normalize(mel_vec4_sub(r3, r1));
    f.planes[4] = mel_plane_normalize(mel_vec4_add(r3, r2));
    f.planes[5] = mel_plane_normalize(mel_vec4_sub(r3, r2));
    return f;
}

void mel_render_view_sync(Mel_Render_View* view)
{
    assert(view != nullptr);

    if (!view->active || view->source == nullptr)
        return;

    if (view->manager == nullptr)
        return;

    mel_render_source_sync(view->source, view->manager);
}

void mel_render_view_cull(Mel_Render_View* view)
{
    assert(view != nullptr);

    if (!view->active || view->manager == nullptr)
        return;

    view->frustum = mel_render_view_extract_frustum(&view->camera);
    mel_mgr_cull(view->manager, &view->frustum, &view->visibility);
}

void mel_render_view_draw(Mel_Render_View* view)
{
    assert(view != nullptr);

    if (!view->active || view->pipeline == nullptr || view->manager == nullptr)
        return;

    mel_pipeline_init_frame(view->pipeline, view);
    mel_pipeline_draw(view->pipeline, view->manager);
}
