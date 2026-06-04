#include <math.h>
#include <stdlib.h>

#include <log/log.h>

#include "boids.h"
#include "hud.h"

static const char BOIDS_SLANG[] = {
#embed "shaders/slang/boids.slang"
    , 0
};

#define BOID_COUNT 4096
#define LOCAL      64

typedef struct
{
    f32 pos_vel[4];
} Boid;

typedef struct
{
    u32 src, dst, total, pad;
    f32 dt, time, goal_x, goal_y, aspect, pad1;
} Boids_Root;

typedef struct
{
    Mel_Gpu_Device*  dev;
    bool             ready;
    Mel_Gpu_Shader   sim_sh;
    Mel_Gpu_Pipeline sim_pl;
    Mel_Gpu_Shader   draw_sh;
    Mel_Gpu_Pipeline draw_pl;
    Mel_Gpu_Buffer   buf[2];
    u32              buf_slot[2];
    i32              cur;
    bool             first_frame;
    f32              aspect;
    Hud              hud;
    f64              t;
} Boids;

static void* boids_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Boids* b = calloc(1, sizeof *b);
    b->dev = dev;
    b->aspect = 1.0f;
    hud_init(&b->hud, dev);

    if (!mel_gpu_bindless_available(dev))
    {
        mel_log_warn("hello-gpu", "boids: bindless heap unavailable; the storage-buffer class needs it");
        return b;
    }

    Mel_Gpu_Pipeline_From_Slang_Result sim = mel_gpu_pipeline_compute_create_from_slang(dev, .source = BOIDS_SLANG, .compute_entry = "cs_sim", .push_constant_size = sizeof(Boids_Root), .bindless = true, .name = "boids-sim");
    if (mel_gpu_failed(sim.status))
        return b;
    b->sim_sh = sim.shader;
    b->sim_pl = sim.value;

    Mel_Gpu_Pipeline_From_Slang_Result draw = mel_gpu_pipeline_create_from_slang(dev,
                                                                                 .source = BOIDS_SLANG,
                                                                                 .vertex_entry = "vs_draw",
                                                                                 .fragment_entry = "fs_draw",
                                                                                 .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                                 .cull = MEL_GPU_CULL_NONE,
                                                                                 .color_format = mel_gpu_swapchain_format(sc),
                                                                                 .bindless = true,
                                                                                 .name = "boids-draw");
    if (mel_gpu_failed(draw.status))
        return b;
    b->draw_sh = draw.shader;
    b->draw_pl = draw.value;

    Boid* seed = malloc(BOID_COUNT * sizeof(Boid));
    for (i32 i = 0; i < BOID_COUNT; ++i)
    {
        f32 a = (f32)i * 2.3999632f;
        f32 r = 0.9f * sqrtf((f32)i / (f32)BOID_COUNT);
        f32 vx = 0.25f * cosf(a * 1.3f);
        f32 vy = 0.25f * sinf(a * 1.3f);
        seed[i] = (Boid){ { r * cosf(a), r * sinf(a), vx, vy } };
    }
    for (i32 k = 0; k < 2; ++k)
    {
        Mel_Gpu_Buffer_Create_Result pb = mel_gpu_buffer_create(dev, .size = BOID_COUNT * sizeof(Boid), .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_DEVICE, .data = seed, .name = "boids");
        if (mel_gpu_failed(pb.status))
        {
            free(seed);
            return b;
        }
        b->buf[k] = pb.value;
        b->buf_slot[k] = mel_gpu_buffer_bindless_slot(dev, b->buf[k]);
    }
    free(seed);

    b->first_frame = true;
    b->ready = true;
    return b;
}

static void boids_resize(void* state, i32 w, i32 h)
{
    Boids* b = state;
    b->aspect = (h > 0) ? (f32)w / (f32)h : 1.0f;
}

static void boids_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    Boids* b = state;
    b->t += dt;
    hud_frame(&b->hud, dt, "4k boids · compute flock (sep/align/cohere) → instanced draw");

    if (!b->ready)
    {
        mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.20f, 0.10f, 0.02f, 1.0f));
        mel_gpu_cmd_end_pass(cmd);
        return;
    }

    i32 src = b->cur;
    i32 dst = b->cur ^ 1;

    Mel_Gpu_Resource_State src_state = b->first_frame ? MEL_GPU_STATE_COMMON : MEL_GPU_STATE_SHADER_RESOURCE;
    Mel_Gpu_Resource_State dst_state = b->first_frame ? MEL_GPU_STATE_COMMON : MEL_GPU_STATE_SHADER_RESOURCE;
    b->first_frame = false;

    mel_gpu_cmd_buffer_barrier(cmd, b->buf[src], src_state, MEL_GPU_STATE_SHADER_RESOURCE);
    mel_gpu_cmd_buffer_barrier(cmd, b->buf[dst], dst_state, MEL_GPU_STATE_UNORDERED_ACCESS);

    f32        gx = 0.55f * (f32)sin(b->t * 0.37);
    f32        gy = 0.55f * (f32)sin(b->t * 0.51 + 1.7);
    Boids_Root sroot = { .src = b->buf_slot[src], .dst = b->buf_slot[dst], .total = BOID_COUNT, .dt = (f32)(dt > 0.05 ? 0.05 : dt), .time = (f32)b->t, .goal_x = gx, .goal_y = gy };
    mel_gpu_cmd_bind_pipeline(cmd, b->sim_pl);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof sroot, &sroot);
    mel_gpu_cmd_dispatch(cmd, (BOID_COUNT + LOCAL - 1) / LOCAL, 1, 1);

    mel_gpu_cmd_buffer_barrier(cmd, b->buf[dst], MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);

    Boids_Root droot = { .src = b->buf_slot[dst], .dst = b->buf_slot[src], .aspect = b->aspect, .time = (f32)b->t };
    mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.02f, 0.03f, 0.05f, 1.0f));
    mel_gpu_cmd_bind_pipeline(cmd, b->draw_pl);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof droot, &droot);
    mel_gpu_cmd_draw(cmd, 3, BOID_COUNT);
    mel_gpu_cmd_end_pass(cmd);

    b->cur = dst;
}

static void boids_teardown(void* state)
{
    Boids* b = state;
    if (!b)
        return;
    if (b->ready)
    {
        for (i32 k = 0; k < 2; ++k)
            mel_gpu_buffer_destroy(b->dev, b->buf[k]);
        mel_gpu_pipeline_destroy(b->dev, b->draw_pl);
        mel_gpu_shader_destroy(b->dev, b->draw_sh);
        mel_gpu_pipeline_destroy(b->dev, b->sim_pl);
        mel_gpu_shader_destroy(b->dev, b->sim_sh);
    }
    free(b);
}

const Graphical_App BOIDS_APP = {
    .title = "gpu-boids",
    .init = boids_init,
    .resize = boids_resize,
    .render = boids_render,
    .teardown = boids_teardown,
};
