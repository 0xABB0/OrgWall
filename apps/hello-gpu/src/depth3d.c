#include <math.h>
#include <stdlib.h>

#include <log/log.h>

#include "depth3d.h"
#include "bindless_present.h"
#include "passthrough.h"

static const char DEPTH3D_SLANG[] = {
#embed "shaders/slang/depth3d.slang"
    , 0
};

#define OFF_W       1024
#define OFF_H       768

#define CUBE_COUNT  9
#define CUBE_TRIS   12
#define CUBE_VERTS  (CUBE_TRIS * 3)
#define SCENE_VERTS (CUBE_COUNT * CUBE_VERTS)
#define VBO_FRAMES  3

typedef struct
{
    f32 x, y, z;
} V3;

typedef struct
{
    Mel_Gpu_Device*      dev;
    bool                 ready;
    Mel_Gpu_Shader       shader;
    Mel_Gpu_Pipeline     pipeline;
    Mel_Gpu_Texture      color;
    Mel_Gpu_Texture_View color_view;
    Mel_Gpu_Texture      depth;
    Mel_Gpu_Texture_View depth_view;
    u32                  color_slot;
    Mel_Gpu_Buffer       vbo[VBO_FRAMES];
    i32                  frame;
    bool                 first_frame;
    f64                  angle;
    Bindless_Present     present;
} Depth3d;

static const V3 CORNERS[8] = {
    { -0.5f, -0.5f, -0.5f }, { 0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, -0.5f }, { -0.5f, 0.5f, -0.5f }, { -0.5f, -0.5f, 0.5f }, { 0.5f, -0.5f, 0.5f }, { 0.5f, 0.5f, 0.5f }, { -0.5f, 0.5f, 0.5f },
};
static const i32 FACES[6][4] = {
    { 1, 2, 6, 5 }, { 0, 4, 7, 3 }, { 3, 7, 6, 2 }, { 0, 1, 5, 4 }, { 4, 5, 6, 7 }, { 0, 3, 2, 1 },
};
static const V3 FACE_COLOR[6] = {
    { 0.91f, 0.30f, 0.24f }, { 0.18f, 0.80f, 0.44f }, { 0.20f, 0.60f, 0.86f }, { 0.95f, 0.77f, 0.06f }, { 0.61f, 0.35f, 0.71f }, { 0.10f, 0.74f, 0.71f },
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
    return (l < 1e-6f) ? (V3){ 0, 0, 0 } : (V3){ v.x / l, v.y / l, v.z / l };
}

static void* depth3d_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Depth3d* d = calloc(1, sizeof *d);
    d->dev = dev;

    if (!mel_gpu_bindless_available(dev))
    {
        mel_log_warn("hello-gpu", "depth3d: bindless heap unavailable; cannot present the offscreen scene");
        return d;
    }

    Mel_Gpu_Depth_Stencil scene_ds = { .depth_test = true, .depth_write = true, .depth_compare = MEL_GPU_COMPARE_LESS };
    Mel_Gpu_Pipeline_From_Slang_Result scene = mel_gpu_pipeline_create_from_slang(dev,
                                                                                  .source = DEPTH3D_SLANG,
                                                                                  .vertex_entry = "vs_scene",
                                                                                  .fragment_entry = "fs_scene",
                                                                                  .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                                  .cull = MEL_GPU_CULL_BACK,
                                                                                  .front_face = MEL_GPU_FRONT_FACE_CCW,
                                                                                  .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                                  .depth_format = MEL_GPU_FORMAT_D32_FLOAT,
                                                                                  .depth_stencil = &scene_ds,
                                                                                  .name = "scene3d");
    if (mel_gpu_failed(scene.status))
        return d;
    d->shader = scene.shader;
    d->pipeline = scene.value;

    Mel_Gpu_Texture_Create_Result color = mel_gpu_texture_create(dev,
                                                                 .kind = MEL_GPU_TEXTURE_2D,
                                                                 .extent = { OFF_W, OFF_H, 1 },
                                                                 .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                 .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_SAMPLED,
                                                                 .name = "scene-color");
    if (mel_gpu_failed(color.status))
        return d;
    d->color = color.value;
    d->color_view = mel_gpu_texture_default_view(dev, d->color).value;
    d->color_slot = mel_gpu_texture_view_bindless_slot(dev, d->color_view);

    Mel_Gpu_Texture_Create_Result depth = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { OFF_W, OFF_H, 1 }, .format = MEL_GPU_FORMAT_D32_FLOAT, .usage = MEL_GPU_TEXTURE_ATTACHMENT, .name = "scene-depth");
    if (mel_gpu_failed(depth.status))
        return d;
    d->depth = depth.value;
    d->depth_view = mel_gpu_texture_default_view(dev, d->depth).value;

    for (i32 i = 0; i < VBO_FRAMES; ++i)
        d->vbo[i] = mel_gpu_buffer_create(dev, .size = SCENE_VERTS * sizeof(Pt_Vertex), .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD, .name = "scene-vbo").value;

    if (!bindless_present_init(&d->present, dev, mel_gpu_swapchain_format(sc)))
        return d;

    d->first_frame = true;
    d->ready = true;
    return d;
}

static u32 build_scene(Depth3d* d, Pt_Vertex* out)
{
    const f32 aspect = (f32)OFF_W / (f32)OFF_H;
    const f32 f = 1.0f / tanf(0.5f * 1.0472f);
    const f32 znear = 0.5f, zfar = 20.0f;
    const V3  L = normalize((V3){ -0.4f, 0.6f, 0.7f });

    u32 count = 0;
    for (i32 c = 0; c < CUBE_COUNT; ++c)
    {
        f32 phase = (f32)c / (f32)CUBE_COUNT * 6.2831853f;
        f32 orbit = (f32)d->angle * 0.5f + phase;
        V3  centre = { 1.8f * cosf(orbit), 1.2f * sinf(orbit * 1.3f), -7.0f + 1.6f * sinf(orbit) };

        f32 ax = (f32)(d->angle * 0.7) + phase;
        f32 ay = (f32)(d->angle * 1.1) + phase * 0.5f;

        V3 view[8];
        for (i32 i = 0; i < 8; ++i)
        {
            V3 r = rotate(CORNERS[i], ax, ay);
            view[i] = (V3){ r.x + centre.x, r.y + centre.y, r.z + centre.z };
        }

        for (i32 face = 0; face < 6; ++face)
        {
            const i32* q = FACES[face];
            const i32  idx[2][3] = { { q[0], q[1], q[2] }, { q[0], q[2], q[3] } };
            for (i32 tri = 0; tri < 2; ++tri)
            {
                V3  a = view[idx[tri][0]], b = view[idx[tri][1]], e = view[idx[tri][2]];
                V3  e1 = { b.x - a.x, b.y - a.y, b.z - a.z };
                V3  e2 = { e.x - a.x, e.y - a.y, e.z - a.z };
                V3  nrm = normalize((V3){ e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x });
                f32 diff = nrm.x * L.x + nrm.y * L.y + nrm.z * L.z;
                if (diff < 0.0f)
                    diff = 0.0f;
                f32 shade = 0.25f + 0.75f * diff;
                V3  base = FACE_COLOR[face];

                V3 vp[3] = { a, b, e };
                for (i32 k = 0; k < 3; ++k)
                {
                    f32 z = -vp[k].z;
                    if (z < znear)
                        z = znear;
                    f32 ndc_x = (vp[k].x * f / aspect) / z;
                    f32 ndc_y = (vp[k].y * f) / z;
                    f32 ndc_z = (zfar / (zfar - znear)) * (1.0f - znear / z);
                    out[count++] = (Pt_Vertex){ { ndc_x, ndc_y, ndc_z }, { base.x * shade, base.y * shade, base.z * shade, 1.0f } };
                }
            }
        }
    }
    return count;
}

static void depth3d_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    Depth3d* d = state;
    d->angle += dt;

    if (!d->ready)
    {
        mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.20f, 0.10f, 0.02f, 1.0f));
        mel_gpu_cmd_end_pass(cmd);
        return;
    }

    Pt_Vertex* verts = malloc(SCENE_VERTS * sizeof(Pt_Vertex));
    u32        count = build_scene(d, verts);
    d->frame = (d->frame + 1) % VBO_FRAMES;
    Mel_Gpu_Buffer vbo = d->vbo[d->frame];
    mel_gpu_buffer_write(d->dev, vbo, verts, count * sizeof(Pt_Vertex));
    free(verts);

    Mel_Gpu_Subresource_Range crange = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    Mel_Gpu_Subresource_Range drange = { MEL_GPU_ASPECT_DEPTH, 0, 1, 0, 1 };

    Mel_Gpu_Resource_State color_src = d->first_frame ? MEL_GPU_STATE_COMMON : MEL_GPU_STATE_SHADER_RESOURCE;

    mel_gpu_cmd_texture_barrier(cmd, d->color, crange, color_src, MEL_GPU_STATE_RENDER_TARGET);
    if (d->first_frame)
        mel_gpu_cmd_texture_barrier(cmd, d->depth, drange, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_DEPTH_WRITE);
    d->first_frame = false;

    Mel_Gpu_Color_Attachment color = { .view = d->color_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.04f, 0.05f, 0.08f, 1.0f) };
    Mel_Gpu_Depth_Attachment depth = { .view = d->depth_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_DONT_CARE, .clear_depth = 1.0f };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .depth = &depth, .width = OFF_W, .height = OFF_H);
    mel_gpu_cmd_bind_pipeline(cmd, d->pipeline);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 0, vbo);
    mel_gpu_cmd_draw(cmd, count, 1);
    mel_gpu_cmd_end_rendering(cmd);

    mel_gpu_cmd_texture_barrier(cmd, d->color, crange, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_SHADER_RESOURCE);

    bindless_present_blit(&d->present, cmd, d->color_slot, mel_gpu_rgba(0, 0, 0, 1));
}

static void depth3d_teardown(void* state)
{
    Depth3d* d = state;
    if (!d)
        return;
    if (d->ready)
    {
        bindless_present_teardown(&d->present);
        for (i32 i = 0; i < VBO_FRAMES; ++i)
            mel_gpu_buffer_destroy(d->dev, d->vbo[i]);
        mel_gpu_texture_view_destroy(d->dev, d->depth_view);
        mel_gpu_texture_destroy(d->dev, d->depth);
        mel_gpu_texture_view_destroy(d->dev, d->color_view);
        mel_gpu_texture_destroy(d->dev, d->color);
        mel_gpu_pipeline_destroy(d->dev, d->pipeline);
        mel_gpu_shader_destroy(d->dev, d->shader);
    }
    free(d);
}

const Graphical_App DEPTH3D_APP = {
    .title = "depth-buffered-3d",
    .init = depth3d_init,
    .render = depth3d_render,
    .teardown = depth3d_teardown,
};
