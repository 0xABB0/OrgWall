#include <math.h>
#include <stdlib.h>

#include "lorenz.h"
#include "passthrough.h"

#define LORENZ_POINTS   4000
#define LORENZ_SEGMENTS (LORENZ_POINTS - 1)
#define LORENZ_VERTS    (LORENZ_SEGMENTS * 2)
#define LORENZ_FRAMES   3

typedef struct { f32 x, y, z; } V3;

typedef struct {
    Mel_Gpu_Shader*   shader;
    Mel_Gpu_Pipeline* pipeline;
    Mel_Gpu_Buffer*   vbo[LORENZ_FRAMES];
    i32               frame;
    f64               angle;
    f32               aspect;
    V3                pts[LORENZ_POINTS];
    Pt_Vertex         scratch[LORENZ_VERTS];
} Lorenz;

static V3 hue(f32 h)
{
    h -= floorf(h);
    f32 r = fabsf(h * 6.0f - 3.0f) - 1.0f;
    f32 g = 2.0f - fabsf(h * 6.0f - 2.0f);
    f32 b = 2.0f - fabsf(h * 6.0f - 4.0f);
    r = r < 0 ? 0 : r > 1 ? 1 : r;
    g = g < 0 ? 0 : g > 1 ? 1 : g;
    b = b < 0 ? 0 : b > 1 ? 1 : b;
    return (V3){ r, g, b };
}

static void* lorenz_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Lorenz* lz = calloc(1, sizeof *lz);
    lz->aspect = 1.0f;
    lz->shader = passthrough_shader(dev);
    lz->pipeline = passthrough_pipeline(dev, lz->shader,
        MEL_GPU_TOPOLOGY_LINE_LIST, mel_gpu_swapchain_format(sc));
    for (i32 i = 0; i < LORENZ_FRAMES; ++i)
        lz->vbo[i] = mel_gpu_buffer_create(dev,
            .size = LORENZ_VERTS * sizeof(Pt_Vertex),
            .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD);

    const f32 sigma = 10.0f, rho = 28.0f, beta = 8.0f / 3.0f, h = 0.005f;
    f32 x = 0.1f, y = 0.0f, z = 0.0f;
    for (i32 i = 0; i < LORENZ_POINTS; ++i) {
        f32 dx = sigma * (y - x);
        f32 dy = x * (rho - z) - y;
        f32 dz = x * y - beta * z;
        x += dx * h; y += dy * h; z += dz * h;
        lz->pts[i] = (V3){ x / 24.0f, y / 28.0f, (z - 25.0f) / 24.0f };
    }
    return lz;
}

static void lorenz_resize(void* state, i32 w, i32 h)
{
    Lorenz* lz = state;
    lz->aspect = (h > 0) ? (f32)w / (f32)h : 1.0f;
}

static void lorenz_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    Lorenz* lz = state;
    lz->angle += dt;

    const f32 dist = 3.0f;
    const f32 f    = 1.0f / tanf(0.5f * 1.0472f);
    f32 ay = (f32)(lz->angle * 0.3);
    f32 cy = cosf(ay), sy = sinf(ay);

    Pt_Vertex prev = { 0 };
    for (i32 i = 0; i < LORENZ_POINTS; ++i) {
        V3 p = lz->pts[i];
        f32 xr =  p.x * cy + p.z * sy;
        f32 zr = -p.x * sy + p.z * cy - dist;
        f32 ndc_x = (xr * f / lz->aspect) / (-zr);
        f32 ndc_y = (p.y * f) / (-zr);

        V3 col = hue((f32)i / (f32)LORENZ_POINTS + (f32)(lz->angle * 0.05));
        Pt_Vertex v = { { ndc_x, ndc_y, 0.5f }, { col.x, col.y, col.z, 1.0f } };

        if (i > 0) {
            lz->scratch[(i - 1) * 2 + 0] = prev;
            lz->scratch[(i - 1) * 2 + 1] = v;
        }
        prev = v;
    }

    lz->frame = (lz->frame + 1) % LORENZ_FRAMES;
    Mel_Gpu_Buffer* vbo = lz->vbo[lz->frame];
    mel_gpu_buffer_write(vbo, lz->scratch, LORENZ_VERTS * sizeof(Pt_Vertex));

    mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.02f, 0.02f, 0.05f, 1.0f));
    mel_gpu_cmd_bind_pipeline(cmd, lz->pipeline);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 0, vbo);
    mel_gpu_cmd_draw(cmd, LORENZ_VERTS, 1);
    mel_gpu_cmd_end_pass(cmd);
}

static void lorenz_teardown(void* state)
{
    Lorenz* lz = state;
    if (!lz) return;
    mel_gpu_pipeline_destroy(lz->pipeline);
    mel_gpu_shader_destroy(lz->shader);
    for (i32 i = 0; i < LORENZ_FRAMES; ++i) mel_gpu_buffer_destroy(lz->vbo[i]);
    free(lz);
}

const Graphical_App LORENZ_APP = {
    .title    = "lorenz-attractor",
    .init     = lorenz_init,
    .resize   = lorenz_resize,
    .render   = lorenz_render,
    .teardown = lorenz_teardown,
};
