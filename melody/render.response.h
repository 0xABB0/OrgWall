#pragma once

#include "core.types.h"
#include "string.str8.h"
#include "render.viewport.fwd.h"
#include "render.scene.fwd.h"
#include "render.target.fwd.h"
#include "gpu.image.fwd.h"

typedef struct Mel_Render_Draw_Ctx Mel_Render_Draw_Ctx;

typedef struct Mel_Render_Response_Ctx Mel_Render_Response_Ctx;
typedef struct Mel_Render_Response_Op_Type Mel_Render_Response_Op_Type;

struct Mel_Render_Response_Op_Type {
    str8  name;
    usize params_size;
    usize params_align;
    void  (*run)(Mel_Render_Response_Ctx* ctx, const void* params);
};

typedef struct {
    u32 id;
} Mel_Render_View_Response_Op_Handle;

#define MEL_RENDER_VIEW_RESPONSE_OP_HANDLE_NULL ((Mel_Render_View_Response_Op_Handle){0})

struct Mel_Render_Response_Ctx {
    Mel_Render_View*      view;
    Mel_Render_Scene*     scene;
    Mel_Render_Draw_Ctx*  draw_ctx;
    Mel_Gpu_Image*        src;
    u32                   src_texture_idx;
    Mel_Render_Target*    dst_target;
    Mel_Gpu_Image*        dst;
    void*                 user_data;
};

typedef struct {
    Mel_Render_View_Handle view;
    const Mel_Render_Response_Op_Type* type;
    const void* params;
    usize params_size;
    i32 order;
    void* user_data;
} Mel_Render_View_Response_Op_Desc;

Mel_Render_View_Response_Op_Handle
mel_render_view_response_op_add_impl(Mel_Render_View_Response_Op_Desc desc);

#define mel_render_view_response_op_add(view_handle, op_type, params_ptr, ...) \
    mel_render_view_response_op_add_impl((Mel_Render_View_Response_Op_Desc){ \
        .view = (view_handle), \
        .type = (op_type), \
        .params = (params_ptr), \
        .params_size = sizeof(*(params_ptr)), \
        ##__VA_ARGS__ \
    })

void mel_render_view_response_op_clear(Mel_Render_View_Handle view,
                                       const Mel_Render_Response_Op_Type* type);
void mel_render_view_response_ops_clear_all(Mel_Render_View_Handle view);

void mel_render_view_response_op_remove(Mel_Render_View_Response_Op_Handle handle);
bool mel_render_view_response_op_alive(Mel_Render_View_Response_Op_Handle handle);

u32 mel_render_view_response_op_count(Mel_Render_View_Handle view);
void mel_render_view_response_view_destroy(Mel_Render_View_Handle view);

void mel_render_view_response_execute(Mel_Render_View_Handle view,
                                      Mel_Render_Response_Ctx* ctx,
                                      Mel_Render_Target* src_target,
                                      Mel_Render_Target* ping_a,
                                      Mel_Render_Target* ping_b,
                                      Mel_Render_Target* final_target);

typedef void (*Mel_Render_Response_Visit_Cb)(const Mel_Render_Response_Op_Type* type,
                                             const void* params,
                                             void* user_data,
                                             void* ctx);
void mel__render_view_response_visit(Mel_Render_View_Handle view,
                                     Mel_Render_Response_Visit_Cb visit,
                                     void* ctx);

typedef struct {
    f32 value;
} Mel_Render_Response_Exposure_Manual_Params;

extern const Mel_Render_Response_Op_Type mel_render_response_exposure_manual;
