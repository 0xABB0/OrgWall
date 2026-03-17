#pragma once

#include "render.source.type.h"
#include "render.pipeline.h"
#include "render.manager.h"
#include "render.cull.h"
#include "math.mat4.h"
#include "math.vec4.h"
#include "allocator.fwd.h"

typedef struct Mel_Render_Target Mel_Render_Target;

typedef struct {
    Mel_Mat4   view;
    Mel_Mat4   projection;
    Mel_Vec4   viewport;
    u32        visibility_mask;
    f32        lod_bias;
    u32        view_count;
    Mel_Mat4   stereo_views[2];
} Mel_Render_Camera;

#define MEL_RENDER_CAMERA_DEFAULT ((Mel_Render_Camera){ \
    .view = MEL_MAT4_IDENTITY, \
    .projection = MEL_MAT4_IDENTITY, \
    .viewport = {{ 0, 0, 1, 1 }}, \
    .visibility_mask = 0xFFFFFFFF, \
    .lod_bias = 1.0f, \
    .view_count = 1, \
})

struct Mel_Render_View {
    Mel_Render_Source* source;
    Mel_Render_Camera camera;
    Mel_Render_Target* target;
    Mel_Render_Pipeline* pipeline;
    Mel_Render_Manager* manager;
    i32 priority;
    Mel_BitSet visibility;
    Mel_Frustum frustum;
    bool active;
};

typedef struct {
    Mel_Render_Source* source;
    Mel_Render_Camera camera;
    Mel_Render_Target* target;
    str8 pipeline;
    i32 priority;
    const Mel_Alloc* alloc;
} Mel_Render_View_Desc;

Mel_Render_View* mel_render_view_create_opt(Mel_Render_View_Desc desc);
#define mel_render_view_create(...) mel_render_view_create_opt((Mel_Render_View_Desc){__VA_ARGS__})

void mel_render_view_destroy(Mel_Render_View* view);

void mel_render_view_set_camera(Mel_Render_View* view, Mel_Render_Camera camera);
void mel_render_view_set_active(Mel_Render_View* view, bool active);

Mel_Frustum mel_render_view_extract_frustum(const Mel_Render_Camera* camera);

void mel_render_view_sync(Mel_Render_View* view);
void mel_render_view_cull(Mel_Render_View* view);
void mel_render_view_draw(Mel_Render_View* view);
