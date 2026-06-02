#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <log/log.h>

#include "dispatch_indirect.h"
#include "hud.h"
#include "bindless_present.h"
#include "cull_spv.h"
#include "buildargs_spv.h"
#include "clear_spv.h"
#include "shade_spv.h"

#define AGENT_COUNT 4096
#define LOCAL       64
#define RING        3

typedef struct
{
    f32 pos_phase[4];
} Agent;

typedef struct
{
    u32 agents, survivors, total, pad;
    f32 time, cull_r, cull_x, cull_y;
} Cull_Root;

typedef struct
{
    u32 survivors, args, local, pad;
} Args_Root;

typedef struct
{
    u32 image, w, h;
    f32 time;
} Clear_Root;

typedef struct
{
    u32 agents, survivors, image, total, w, h;
    f32 time, pad;
} Shade_Root;

typedef struct
{
    Mel_Gpu_Device* dev;
    bool            ready;

    Mel_Gpu_Shader   cull_sh, args_sh, clear_sh, shade_sh;
    Mel_Gpu_Pipeline cull_pl, args_pl, clear_pl, shade_pl;

    Mel_Gpu_Buffer agents;
    u32            agents_slot;

    Mel_Gpu_Buffer surv[RING];
    u32            surv_slot[RING];
    Mel_Gpu_Buffer args[RING];
    u32            args_slot[RING];
    i32            ring;

    i32                  w, h;
    Mel_Gpu_Texture      img;
    Mel_Gpu_Texture_View img_view;
    u32                  img_slot;
    bool                 img_fresh;

    Bindless_Present present;
    Hud              hud;
    f64              t;
} Dispatch_Indirect;

static inline bool handle_zero(Mel_SlotMap_Handle h) { return h.index == 0 && h.generation == 0; }

static Mel_Gpu_Pipeline make_compute(Dispatch_Indirect* d, const u32* spv, usize bytes, u32 pc, const char* name, Mel_Gpu_Shader* out_sh)
{
    Mel_Gpu_Shader_Create_Result cs = mel_gpu_shader_create_compute_from_bytecode(d->dev, .spirv = spv, .spirv_size = bytes, .entry = "main", .name = name);
    if (mel_gpu_failed(cs.status))
        return (Mel_Gpu_Pipeline){ 0 };
    *out_sh = cs.value;
    return mel_gpu_pipeline_compute_create(d->dev, .shader = cs.value, .push_constant_size = pc, .name = name).value;
}

static void* di_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Dispatch_Indirect* d = calloc(1, sizeof *d);
    d->dev = dev;
    hud_init(&d->hud, dev);

    if (!mel_gpu_bindless_available(dev))
    {
        mel_log_warn("hello-gpu", "dispatch-indirect: bindless heap unavailable; storage-image + storage-buffer classes need it");
        return d;
    }

    d->cull_pl = make_compute(d, CULL_COMP_SPV, sizeof CULL_COMP_SPV, sizeof(Cull_Root), "di-cull", &d->cull_sh);
    d->args_pl = make_compute(d, BUILDARGS_COMP_SPV, sizeof BUILDARGS_COMP_SPV, sizeof(Args_Root), "di-args", &d->args_sh);
    d->clear_pl = make_compute(d, CLEAR_COMP_SPV, sizeof CLEAR_COMP_SPV, sizeof(Clear_Root), "di-clear", &d->clear_sh);
    d->shade_pl = make_compute(d, SHADE_COMP_SPV, sizeof SHADE_COMP_SPV, sizeof(Shade_Root), "di-shade", &d->shade_sh);

    Agent* pool = malloc(AGENT_COUNT * sizeof(Agent));
    for (i32 i = 0; i < AGENT_COUNT; ++i)
    {
        f32 fi = (f32)i;
        f32 a = fi * 2.3999632f;
        f32 r = 0.95f * sqrtf(fi / (f32)AGENT_COUNT);
        pool[i] = (Agent){ { r * cosf(a), r * sinf(a), 0.06f + 0.05f * sinf(fi * 0.05f), a } };
    }
    Mel_Gpu_Buffer_Create_Result ab = mel_gpu_buffer_create(dev, .size = AGENT_COUNT * sizeof(Agent), .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_DEVICE, .data = pool, .name = "di-agents");
    free(pool);
    if (mel_gpu_failed(ab.status))
        return d;
    d->agents = ab.value;
    d->agents_slot = mel_gpu_buffer_bindless_slot(dev, d->agents);

    usize surv_bytes = 16 + (usize)AGENT_COUNT * sizeof(u32);
    for (i32 i = 0; i < RING; ++i)
    {
        Mel_Gpu_Buffer_Create_Result sb = mel_gpu_buffer_create(dev, .size = surv_bytes, .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_UPLOAD, .name = "di-survivors");
        if (mel_gpu_failed(sb.status))
            return d;
        d->surv[i] = sb.value;
        d->surv_slot[i] = mel_gpu_buffer_bindless_slot(dev, d->surv[i]);

        Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = 3 * sizeof(u32), .usage = MEL_GPU_BUFFER_STORAGE | MEL_GPU_BUFFER_INDIRECT, .memory = MEL_GPU_MEMORY_UPLOAD, .name = "di-args");
        if (mel_gpu_failed(rb.status))
            return d;
        d->args[i] = rb.value;
        d->args_slot[i] = mel_gpu_buffer_bindless_slot(dev, d->args[i]);
    }

    if (!bindless_present_init(&d->present, dev, mel_gpu_swapchain_format(sc)))
        return d;

    d->ready = true;
    return d;
}

static void destroy_img(Dispatch_Indirect* d)
{
    if (!handle_zero(d->img_view.slot))
        mel_gpu_texture_view_destroy(d->dev, d->img_view);
    if (!handle_zero(d->img.slot))
        mel_gpu_texture_destroy(d->dev, d->img);
    d->img_view = (Mel_Gpu_Texture_View){ 0 };
    d->img = (Mel_Gpu_Texture){ 0 };
}

static void di_resize(void* state, i32 w, i32 h)
{
    Dispatch_Indirect* d = state;
    if (!d->ready)
        return;
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    if (w == d->w && h == d->h && !handle_zero(d->img.slot))
        return;

    destroy_img(d);
    d->w = w;
    d->h = h;
    d->img = mel_gpu_texture_create(d->dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { (u32)w, (u32)h, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_STORAGE | MEL_GPU_TEXTURE_SAMPLED, .name = "di-canvas").value;
    d->img_view = mel_gpu_texture_default_view(d->dev, d->img).value;
    d->img_slot = mel_gpu_texture_view_bindless_slot(d->dev, d->img_view);
    d->img_fresh = true;
}

static void di_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    Dispatch_Indirect* d = state;
    d->t += dt;

    if (!d->ready || handle_zero(d->img.slot))
    {
        mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.20f, 0.10f, 0.02f, 1.0f));
        mel_gpu_cmd_end_pass(cmd);
        hud_frame(&d->hud, dt, "dispatch-indirect (init)");
        return;
    }

    d->ring = (d->ring + 1) % RING;
    Mel_Gpu_Buffer surv = d->surv[d->ring];
    Mel_Gpu_Buffer args = d->args[d->ring];

    u32* surv_map = mel_gpu_buffer_mapped(d->dev, surv);
    if (surv_map)
        surv_map[0] = 0;

    f32 cr = 0.55f + 0.25f * (f32)sin(d->t * 0.6);
    f32 cx = 0.35f * (f32)cos(d->t * 0.5);
    f32 cy = 0.35f * (f32)sin(d->t * 0.7);

    Mel_Gpu_Resource_State surv_src = MEL_GPU_STATE_COMMON;
    Mel_Gpu_Resource_State args_src = MEL_GPU_STATE_COMMON;

    mel_gpu_cmd_buffer_barrier(cmd, surv, surv_src, MEL_GPU_STATE_UNORDERED_ACCESS);
    Cull_Root croot = { .agents = d->agents_slot, .survivors = d->surv_slot[d->ring], .total = AGENT_COUNT, .time = (f32)d->t, .cull_r = cr, .cull_x = cx, .cull_y = cy };
    mel_gpu_cmd_bind_pipeline(cmd, d->cull_pl);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof croot, &croot);
    mel_gpu_cmd_dispatch(cmd, (AGENT_COUNT + LOCAL - 1) / LOCAL, 1, 1);

    mel_gpu_cmd_buffer_barrier(cmd, surv, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);
    mel_gpu_cmd_buffer_barrier(cmd, args, args_src, MEL_GPU_STATE_UNORDERED_ACCESS);

    Args_Root aroot = { .survivors = d->surv_slot[d->ring], .args = d->args_slot[d->ring], .local = LOCAL };
    mel_gpu_cmd_bind_pipeline(cmd, d->args_pl);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof aroot, &aroot);
    mel_gpu_cmd_dispatch(cmd, 1, 1, 1);

    mel_gpu_cmd_buffer_barrier(cmd, args, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_INDIRECT_ARGUMENT);

    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    Mel_Gpu_Resource_State    img_src = d->img_fresh ? MEL_GPU_STATE_COMMON : MEL_GPU_STATE_SHADER_RESOURCE;
    d->img_fresh = false;
    mel_gpu_cmd_texture_barrier(cmd, d->img, range, img_src, MEL_GPU_STATE_UNORDERED_ACCESS);

    Clear_Root clroot = { .image = d->img_slot, .w = (u32)d->w, .h = (u32)d->h, .time = (f32)d->t };
    mel_gpu_cmd_bind_pipeline(cmd, d->clear_pl);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof clroot, &clroot);
    mel_gpu_cmd_dispatch(cmd, ((u32)d->w + 7) / 8, ((u32)d->h + 7) / 8, 1);

    mel_gpu_cmd_texture_barrier(cmd, d->img, range, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_UNORDERED_ACCESS);

    Shade_Root sroot = { .agents = d->agents_slot, .survivors = d->surv_slot[d->ring], .image = d->img_slot, .total = AGENT_COUNT, .w = (u32)d->w, .h = (u32)d->h, .time = (f32)d->t };
    mel_gpu_cmd_bind_pipeline(cmd, d->shade_pl);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof sroot, &sroot);
    mel_gpu_cmd_dispatch_indirect(cmd, args, 0);

    mel_gpu_cmd_texture_barrier(cmd, d->img, range, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);
    bindless_present_blit(&d->present, cmd, d->img_slot, mel_gpu_rgba(0, 0, 0, 1));

    hud_frame(&d->hud, dt, "GPU-driven dispatch · 4096 agents culled on-GPU");
}

static void di_teardown(void* state)
{
    Dispatch_Indirect* d = state;
    if (!d)
        return;
    if (d->ready)
    {
        destroy_img(d);
        bindless_present_teardown(&d->present);
        for (i32 i = 0; i < RING; ++i)
        {
            mel_gpu_buffer_destroy(d->dev, d->args[i]);
            mel_gpu_buffer_destroy(d->dev, d->surv[i]);
        }
        mel_gpu_buffer_destroy(d->dev, d->agents);
        mel_gpu_pipeline_destroy(d->dev, d->shade_pl);
        mel_gpu_shader_destroy(d->dev, d->shade_sh);
        mel_gpu_pipeline_destroy(d->dev, d->clear_pl);
        mel_gpu_shader_destroy(d->dev, d->clear_sh);
        mel_gpu_pipeline_destroy(d->dev, d->args_pl);
        mel_gpu_shader_destroy(d->dev, d->args_sh);
        mel_gpu_pipeline_destroy(d->dev, d->cull_pl);
        mel_gpu_shader_destroy(d->dev, d->cull_sh);
    }
    free(d);
}

const Graphical_App DISPATCH_INDIRECT_APP = {
    .title = "dispatch-indirect",
    .init = di_init,
    .resize = di_resize,
    .render = di_render,
    .teardown = di_teardown,
};
