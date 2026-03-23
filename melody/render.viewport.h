#pragma once

#include "render.pipeline.h"
#include "render.scene.fwd.h"
#include "render.target.fwd.h"
#include "render.target.h"
#include "render.viewport.fwd.h"
#include "math.mat4.h"
#include "math.vec4.h"
#include "gpu.device.fwd.h"
#include "gpu.image.h"
#include "allocator.fwd.h"

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

#define MEL_SCALE_NONE    0
#define MEL_SCALE_FIT     1
#define MEL_SCALE_FILL    2
#define MEL_SCALE_STRETCH 3

struct Mel_Render_View {
    Mel_Render_View_Handle self;
    Mel_Render_Scene* scene;
    Mel_Render_Camera camera;
    Mel_Render_Target_Handle target;
    Mel_Render_Pipeline* pipeline;
    Mel_Gpu_Device* dev;
    i32 priority;
    bool active;

    u32 design_width;
    u32 design_height;
    u32 scale_mode;
    Mel_Render_Target_Handle design_target;
};

typedef struct {
    Mel_Render_Scene* scene;
    Mel_Render_Camera camera;
    Mel_Render_Target_Handle target;
    str8 pipeline;
    i32 priority;
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
    u32 design_width;
    u32 design_height;
    u32 scale_mode;
} Mel_Render_View_Desc;

Mel_Render_View_Handle mel_render_view_create_opt(Mel_Render_View_Desc desc);
#define mel_render_view_create(...) mel_render_view_create_opt((Mel_Render_View_Desc){__VA_ARGS__})

void mel_render_view_destroy(Mel_Render_View_Handle handle);
bool mel_render_view_alive(Mel_Render_View_Handle handle);
Mel_Render_View* mel_render_view_get(Mel_Render_View_Handle handle);

void mel_render_view_set_camera(Mel_Render_View_Handle handle, Mel_Render_Camera camera);
void mel_render_view_set_active(Mel_Render_View_Handle handle, bool active);

bool mel_render_view_has_design_resolution(Mel_Render_View* view);
Mel_Render_Target* mel_render_view_effective_target(Mel_Render_View* view);

void mel_render_view_sync(Mel_Render_View* view);
void mel_render_view_draw(Mel_Render_View* view, Mel_Render_Draw_Ctx* ctx);

void mel_render_view_destroy_by_target(Mel_Render_Target_Handle target);
