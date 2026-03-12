#pragma once

#include "render.view.fwd.h"
#include "render.camera.fwd.h"
#include "render.source.fwd.h"
#include "string.str8.fwd.h"
#include "math.vec4.h"

#define MEL_VIEW_COMPOSE_REPLACE 0
#define MEL_VIEW_COMPOSE_ALPHA   1

#define MEL_VIEW_TARGET_FILL 0
#define MEL_VIEW_TARGET_FIT  1

typedef struct {
    str8 name;
    const Mel_Camera* camera;
    bool clear_color_enabled;
    Mel_Vec4 clear_color;
    u32 composition_mode;
    u32 target_mode;
    u32 design_width;
    u32 design_height;
    void* user;
} Mel_View_Desc;

Mel_View_Handle mel_view_create(const Mel_View_Desc* desc);
void mel_view_destroy(Mel_View_Handle view);

void mel_view_attach_source(Mel_View_Handle view, Mel_Source_Handle source);
void mel_view_detach_source(Mel_View_Handle view, Mel_Source_Handle source);

str8 mel_view_name(Mel_View_Handle view);
const Mel_Camera* mel_view_camera(Mel_View_Handle view);
bool mel_view_clear_color_enabled(Mel_View_Handle view);
Mel_Vec4 mel_view_clear_color(Mel_View_Handle view);
u32 mel_view_composition_mode(Mel_View_Handle view);
u32 mel_view_target_mode(Mel_View_Handle view);
u32 mel_view_design_width(Mel_View_Handle view);
u32 mel_view_design_height(Mel_View_Handle view);
void* mel_view_user(Mel_View_Handle view);

u32 mel_view_source_count(Mel_View_Handle view);
Mel_Source_Handle mel_view_source_at(Mel_View_Handle view, u32 index);
