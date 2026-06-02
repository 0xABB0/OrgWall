#include <math.h>
#include <stdlib.h>

#include <log/log.h>

#include "particles.h"
#include "hud.h"
#include "particle_sim_spv.h"
#include "particle_draw_spv.h"
#include "instances_spv.h" // INSTANCES_FRAG_SPV: plain v_color passthrough

#define PARTICLE_COUNT 40000
#define LOCAL          64

typedef struct
{
    f32 pos_life[4]; // xy pos, z life, w seed
    f32 vel[4];
} Particle;

typedef struct
{
    u32 particles, total;
    f32 dt, time, attract_x, attract_y;
} Sim_Root;

typedef struct
{
    u32 particles;
    f32 aspect, pad0, pad1;
} Draw_Root;

typedef struct
{
    Mel_Gpu_Device*  dev;
    bool             ready;
    Mel_Gpu_Shader   sim_sh;
    Mel_Gpu_Pipeline sim_pl;
    Mel_Gpu_Shader   draw_sh;
    Mel_Gpu_Pipeline draw_pl;
    Mel_Gpu_Buffer   particles;
    u32              particles_slot;
    bool             first_frame;
    f32              aspect;
    Hud              hud;
    f64              t;
} Particles;

static void* particles_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Particles* p = calloc(1, sizeof *p);
    p->dev = dev;
    p->aspect = 1.0f;
    hud_init(&p->hud, dev);

    if (!mel_gpu_bindless_available(dev))
    {
        mel_log_warn("hello-gpu", "particles: bindless heap unavailable; the storage-buffer class needs it");
        return p;
    }

    Mel_Gpu_Shader_Create_Result cs = mel_gpu_shader_create_compute_from_bytecode(dev, .spirv = PARTICLE_SIM_COMP_SPV, .spirv_size = sizeof PARTICLE_SIM_COMP_SPV, .entry = "main", .name = "particle-sim");
    if (mel_gpu_failed(cs.status))
        return p;
    p->sim_sh = cs.value;
    p->sim_pl = mel_gpu_pipeline_compute_create(dev, .shader = p->sim_sh, .push_constant_size = sizeof(Sim_Root), .name = "particle-sim").value;

    p->draw_sh = mel_gpu_shader_create_from_bytecode(dev,
                                                     .spirv_vertex = PARTICLE_DRAW_VERT_SPV,
                                                     .spirv_vertex_size = sizeof PARTICLE_DRAW_VERT_SPV,
                                                     .spirv_fragment = INSTANCES_FRAG_SPV,
                                                     .spirv_fragment_size = sizeof INSTANCES_FRAG_SPV,
                                                     .vertex_entry = "main",
                                                     .fragment_entry = "main",
                                                     .name = "particle-draw")
                     .value;
    // Additive blend so dense regions of the swarm bloom (the glow reads as energy).
    Mel_Gpu_Color_Target target = {
        .format = mel_gpu_swapchain_format(sc),
        .blend = { .enable = true,
                   .src_color = MEL_GPU_BLEND_SRC_ALPHA,
                   .dst_color = MEL_GPU_BLEND_ONE,
                   .color_op = MEL_GPU_BLEND_OP_ADD,
                   .src_alpha = MEL_GPU_BLEND_ONE,
                   .dst_alpha = MEL_GPU_BLEND_ONE,
                   .alpha_op = MEL_GPU_BLEND_OP_ADD,
                   .write_mask = MEL_GPU_COLOR_WRITE_ALL },
    };
    p->draw_pl = mel_gpu_pipeline_create(dev,
                                         .shader = p->draw_sh,
                                         .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                         .cull = MEL_GPU_CULL_NONE,
                                         .color_targets = &target,
                                         .color_target_count = 1,
                                         .push_constant_size = sizeof(Draw_Root),
                                         .name = "particle-draw")
                     .value;

    // Seed the pool in a thin ring; the integrator takes over from frame 1.
    Particle* seed = malloc(PARTICLE_COUNT * sizeof(Particle));
    for (i32 i = 0; i < PARTICLE_COUNT; ++i)
    {
        f32 a = (f32)i * 2.3999632f;
        f32 r = 0.9f + 0.1f * (f32)((i * 2654435761u) & 0xFF) / 255.0f;
        seed[i] = (Particle){ { r * cosf(a), r * sinf(a), (f32)(i % 100) / 100.0f, a }, { -0.15f * sinf(a), 0.15f * cosf(a), 0.0f, 0.0f } };
    }
    Mel_Gpu_Buffer_Create_Result pb = mel_gpu_buffer_create(dev, .size = PARTICLE_COUNT * sizeof(Particle), .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_DEVICE, .data = seed, .name = "particles");
    free(seed);
    if (mel_gpu_failed(pb.status))
        return p;
    p->particles = pb.value;
    p->particles_slot = mel_gpu_buffer_bindless_slot(dev, p->particles);

    p->first_frame = true;
    p->ready = true;
    return p;
}

static void particles_resize(void* state, i32 w, i32 h)
{
    Particles* p = state;
    p->aspect = (h > 0) ? (f32)w / (f32)h : 1.0f;
}

static void particles_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    Particles* p = state;
    p->t += dt;
    hud_frame(&p->hud, dt, "40k particles · compute-integrate → instanced draw");

    if (!p->ready)
    {
        mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.20f, 0.10f, 0.02f, 1.0f));
        mel_gpu_cmd_end_pass(cmd);
        return;
    }

    // First frame the buffer is COMMON; thereafter the prior frame left it in
    // SHADER_RESOURCE (the vertex read) — the read-after-write edge to clear.
    Mel_Gpu_Resource_State buf_src = p->first_frame ? MEL_GPU_STATE_COMMON : MEL_GPU_STATE_SHADER_RESOURCE;
    p->first_frame = false;
    mel_gpu_cmd_buffer_barrier(cmd, p->particles, buf_src, MEL_GPU_STATE_UNORDERED_ACCESS);

    // Integrate. The attractor traces a Lissajous path so the swarm chases it.
    f32      ax = 0.6f * (f32)sin(p->t * 0.7);
    f32      ay = 0.6f * (f32)sin(p->t * 0.9 + 1.3);
    Sim_Root sroot = { .particles = p->particles_slot, .total = PARTICLE_COUNT, .dt = (f32)(dt > 0.05 ? 0.05 : dt), .time = (f32)p->t, .attract_x = ax, .attract_y = ay };
    mel_gpu_cmd_bind_pipeline(cmd, p->sim_pl);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof sroot, &sroot);
    mel_gpu_cmd_dispatch(cmd, (PARTICLE_COUNT + LOCAL - 1) / LOCAL, 1, 1);

    // Compute write -> vertex read.
    mel_gpu_cmd_buffer_barrier(cmd, p->particles, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);

    Draw_Root droot = { .particles = p->particles_slot, .aspect = p->aspect };
    mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.02f, 0.02f, 0.04f, 1.0f));
    mel_gpu_cmd_bind_pipeline(cmd, p->draw_pl);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof droot, &droot);
    mel_gpu_cmd_draw(cmd, 6, PARTICLE_COUNT);
    mel_gpu_cmd_end_pass(cmd);
}

static void particles_teardown(void* state)
{
    Particles* p = state;
    if (!p)
        return;
    if (p->ready)
    {
        mel_gpu_buffer_destroy(p->dev, p->particles);
        mel_gpu_pipeline_destroy(p->dev, p->draw_pl);
        mel_gpu_shader_destroy(p->dev, p->draw_sh);
        mel_gpu_pipeline_destroy(p->dev, p->sim_pl);
        mel_gpu_shader_destroy(p->dev, p->sim_sh);
    }
    free(p);
}

const Graphical_App PARTICLES_APP = {
    .title = "gpu-particles",
    .init = particles_init,
    .resize = particles_resize,
    .render = particles_render,
    .teardown = particles_teardown,
};
