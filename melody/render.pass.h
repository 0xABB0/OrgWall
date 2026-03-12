#pragma once

#include "core.types.h"
#include "gpu.cmd.h"
#include "render.camera.fwd.h"
#include "render.list.fwd.h"
#include "render.target.fwd.h"

#define MEL_PASS_GRAPHICS 0
#define MEL_PASS_COMPUTE  1

#define MEL_PASS_VIEWPORT_TARGET 0
#define MEL_PASS_VIEWPORT_FIT    1

#define MEL_LISTS(...)   ((Mel_Render_List*[]){ __VA_ARGS__, NULL })
#define MEL_TARGETS(...) ((Mel_Render_Target*[]){ __VA_ARGS__, NULL })

typedef struct Mel_Pass_Write_Target Mel_Pass_Write_Target;

struct Mel_Pass_Write_Target {
    Mel_Render_Target* target;
    VkAttachmentLoadOp load_op;
    union {
        struct { f32 r, g, b, a; } color;
        struct { f32 depth; u32 stencil; } depth;
    } clear;
};

#define MEL_WRITE_TARGETS(...) ((Mel_Pass_Write_Target[]){ __VA_ARGS__, {0} })

typedef struct Mel_Render_Pass_Ctx Mel_Render_Pass_Ctx;
typedef void (*Mel_Render_Pass_Fn)(Mel_Render_Pass_Ctx* ctx);

typedef struct {
    Mel_Render_List** read_lists;
    Mel_Render_List** write_lists;
    Mel_Render_Target** read_targets;
    Mel_Pass_Write_Target* write_targets;
    Mel_Camera* camera;
    Mel_Render_Pass_Fn fn;
    void* user;
    u32 type;
    u32 viewport_mode;
    u32 viewport_design_width;
    u32 viewport_design_height;
} Mel_Pass_Desc;

struct Mel_Render_Pass_Ctx {
    Mel_Gpu_Cmd cmd;
    Mel_Render_List** read_lists;
    Mel_Render_List** write_lists;
    Mel_Render_Target** read_targets;
    Mel_Render_Target** write_targets;
    const Mel_Camera* camera;
    void* user;
    u32 render_width;
    u32 render_height;
};
