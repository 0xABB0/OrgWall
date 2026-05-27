#include <math.h>
#include <stdlib.h>

#include "cube.h"
#include "passthrough.h"

#define CUBE_TRIS   12
#define CUBE_VERTS  (CUBE_TRIS * 3)
#define CUBE_FRAMES 3

typedef struct { f32 x, y, z; } V3;

typedef struct {
    Mel_Gpu_Shader*   shader;
    Mel_Gpu_Pipeline* pipeline;
    Mel_Gpu_Buffer*   vbo[CUBE_FRAMES];
    i32               frame;
    f64               angle;
    f32               aspect;
} Cube;

static const V3 CORNERS[8] = {
    { -0.9f, -0.9f, -0.9f }, {  0.9f, -0.9f, -0.9f },
    {  0.9f,  0.9f, -0.9f }, { -0.9f,  0.9f, -0.9f },
    { -0.9f, -0.9f,  0.9f }, {  0.9f, -0.9f,  0.9f },
    {  0.9f,  0.9f,  0.9f }, { -0.9f,  0.9f,  0.9f },
};

static const i32 FACES[6][4] = {
    { 1, 2, 6, 5 }, { 0, 4, 7, 3 },
    { 3, 7, 6, 2 }, { 0, 1, 5, 4 },
    { 4, 5, 6, 7 }, { 0, 3, 2, 1 },
};

static const V3 FACE_COLOR[6] = {
    { 0.91f, 0.30f, 0.24f }, { 0.18f, 0.80f, 0.44f },
    { 0.20f, 0.60f, 0.86f }, { 0.95f, 0.77f, 0.06f },
    { 0.61f, 0.35f, 0.71f }, { 0.10f, 0.74f, 0.71f },
};

static V3 rotate(V3 p, f32 ax, f32 ay)
{
    f32 cx = cosf(ax), sx = sinf(ax);
    f32 y1 = p.y * cx - p.z * sx;
    f32 z1 = p.y * sx + p.z * cx;
    f32 cy = cosf(ay), sy = sinf(ay);
    f32 x2 = p.x * cy + z1 * sy;
    f32 z2 = -p.x * sy + z1 * cy;
    return (V3){ x2, y1, z2 };
}

static V3 normalize(V3 v)
{
    f32 l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (l < 1e-6f) return (V3){ 0, 0, 0 };
    return (V3){ v.x / l, v.y / l, v.z / l };
}

static void* cube_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Cube* c = calloc(1, sizeof *c);
    c->aspect = 1.0f;
    c->shader = passthrough_shader(dev);
    c->pipeline = passthrough_pipeline(dev, c->shader,
        MEL_GPU_TOPOLOGY_TRIANGLE_LIST, mel_gpu_swapchain_format(sc));
    for (i32 i = 0; i < CUBE_FRAMES; ++i)
        c->vbo[i] = mel_gpu_buffer_create(dev,
            .size = CUBE_VERTS * sizeof(Pt_Vertex),
            .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD);
    return c;
}

static void cube_resize(void* state, i32 w, i32 h)
{
    Cube* c = state;
    c->aspect = (h > 0) ? (f32)w / (f32)h : 1.0f;
}

static void cube_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    Cube* c = state;
    c->angle += dt;

    const f32 dist  = 4.0f;
    const f32 f     = 1.0f / tanf(0.5f * 1.0472f);
    const V3  L     = normalize((V3){ -0.4f, 0.6f, 0.7f });

    f32 ax = (f32)(c->angle * 0.55);
    f32 ay = (f32)(c->angle * 0.9);

    V3 view[8];
    for (i32 i = 0; i < 8; ++i) {
        V3 r = rotate(CORNERS[i], ax, ay);
        view[i] = (V3){ r.x, r.y, r.z - dist };
    }

    Pt_Vertex out[CUBE_VERTS];
    u32 count = 0;
    for (i32 face = 0; face < 6; ++face) {
        const i32* q = FACES[face];
        const i32 idx[2][3] = { { q[0], q[1], q[2] }, { q[0], q[2], q[3] } };
        for (i32 t = 0; t < 2; ++t) {
            V3 a = view[idx[t][0]], b = view[idx[t][1]], d = view[idx[t][2]];

            V3 e1 = { b.x - a.x, b.y - a.y, b.z - a.z };
            V3 e2 = { d.x - a.x, d.y - a.y, d.z - a.z };
            V3 nrm = normalize((V3){
                e1.y * e2.z - e1.z * e2.y,
                e1.z * e2.x - e1.x * e2.z,
                e1.x * e2.y - e1.y * e2.x });

            V3 center = { (a.x + b.x + d.x) / 3.0f,
                          (a.y + b.y + d.y) / 3.0f,
                          (a.z + b.z + d.z) / 3.0f };
            V3 outward = { center.x, center.y, center.z + dist };
            if (nrm.x * outward.x + nrm.y * outward.y + nrm.z * outward.z < 0.0f)
                nrm = (V3){ -nrm.x, -nrm.y, -nrm.z };

            if (nrm.x * center.x + nrm.y * center.y + nrm.z * center.z >= 0.0f)
                continue;

            f32 diff = nrm.x * L.x + nrm.y * L.y + nrm.z * L.z;
            if (diff < 0.0f) diff = 0.0f;
            f32 shade = 0.25f + 0.75f * diff;
            V3 base = FACE_COLOR[face];

            V3 vp[3] = { a, b, d };
            for (i32 k = 0; k < 3; ++k) {
                f32 ndc_x = (vp[k].x * f / c->aspect) / (-vp[k].z);
                f32 ndc_y = (vp[k].y * f) / (-vp[k].z);
                out[count++] = (Pt_Vertex){
                    { ndc_x, ndc_y, 0.5f },
                    { base.x * shade, base.y * shade, base.z * shade, 1.0f } };
            }
        }
    }

    c->frame = (c->frame + 1) % CUBE_FRAMES;
    Mel_Gpu_Buffer* vbo = c->vbo[c->frame];
    mel_gpu_buffer_write(vbo, out, count * sizeof(Pt_Vertex));

    mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.05f, 0.06f, 0.09f, 1.0f));
    mel_gpu_cmd_bind_pipeline(cmd, c->pipeline);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 0, vbo);
    mel_gpu_cmd_draw(cmd, count, 1);
    mel_gpu_cmd_end_pass(cmd);
}

static void cube_teardown(void* state)
{
    Cube* c = state;
    if (!c) return;
    mel_gpu_pipeline_destroy(c->pipeline);
    mel_gpu_shader_destroy(c->shader);
    for (i32 i = 0; i < CUBE_FRAMES; ++i) mel_gpu_buffer_destroy(c->vbo[i]);
    free(c);
}

const Graphical_App CUBE_APP = {
    .title    = "spinning-cube",
    .init     = cube_init,
    .resize   = cube_resize,
    .render   = cube_render,
    .teardown = cube_teardown,
};
