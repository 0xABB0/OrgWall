#include <math.h>
#include <stdlib.h>

#include <log/log.h>

#include "prepass.h"
#include "hud.h"
#include "passthrough.h"
#include "bindless_present.h"
#include "scene3d_spv.h"
#include "depth_only_spv.h"

#define CUBE_COUNT  14
#define CUBE_VERTS  36
#define SCENE_VERTS (CUBE_COUNT * CUBE_VERTS)
#define VBO_FRAMES  3

typedef struct
{
    f32 x, y, z;
} V3;

typedef struct
{
    Mel_Gpu_Device*  dev;
    bool             ready;
    Mel_Gpu_Shader   shader;
    Mel_Gpu_Shader   depth_shader;
    Mel_Gpu_Pipeline depth_pl;
    Mel_Gpu_Pipeline lit_pl;

    i32                  w, h;
    Mel_Gpu_Texture      color;
    Mel_Gpu_Texture_View color_view;
    u32                  color_slot;
    Mel_Gpu_Texture      depth;
    Mel_Gpu_Texture_View depth_view;
    bool                 targets_fresh;

    Mel_Gpu_Buffer vbo[VBO_FRAMES];
    i32            vframe;

    Bindless_Present present;
    Hud              hud;
    f64              angle;
} Prepass;

static inline bool handle_zero(Mel_SlotMap_Handle h) { return h.index == 0 && h.generation == 0; }

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

static void destroy_targets(Prepass* p)
{
    if (!handle_zero(p->color_view.slot))
        mel_gpu_texture_view_destroy(p->dev, p->color_view);
    if (!handle_zero(p->color.slot))
        mel_gpu_texture_destroy(p->dev, p->color);
    if (!handle_zero(p->depth_view.slot))
        mel_gpu_texture_view_destroy(p->dev, p->depth_view);
    if (!handle_zero(p->depth.slot))
        mel_gpu_texture_destroy(p->dev, p->depth);
    p->color_view = p->depth_view = (Mel_Gpu_Texture_View){ 0 };
    p->color = p->depth = (Mel_Gpu_Texture){ 0 };
}

static void* prepass_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Prepass* p = calloc(1, sizeof *p);
    p->dev = dev;
    hud_init(&p->hud, dev);

    if (!mel_gpu_bindless_available(dev))
    {
        mel_log_warn("hello-gpu", "prepass: bindless heap unavailable; cannot present the lit scene");
        return p;
    }

    p->shader = mel_gpu_shader_create_from_bytecode(dev,
                                                    .spirv_vertex = SCENE3D_VERT_SPV,
                                                    .spirv_vertex_size = sizeof SCENE3D_VERT_SPV,
                                                    .spirv_fragment = SCENE3D_FRAG_SPV,
                                                    .spirv_fragment_size = sizeof SCENE3D_FRAG_SPV,
                                                    .vertex_entry = "main",
                                                    .fragment_entry = "main",
                                                    .name = "prepass-scene")
                    .value;

    p->depth_shader = mel_gpu_shader_create_from_bytecode(dev,
                                                          .spirv_vertex = SCENE3D_VERT_SPV,
                                                          .spirv_vertex_size = sizeof SCENE3D_VERT_SPV,
                                                          .spirv_fragment = DEPTH_ONLY_FRAG_SPV,
                                                          .spirv_fragment_size = sizeof DEPTH_ONLY_FRAG_SPV,
                                                          .vertex_entry = "main",
                                                          .fragment_entry = "main",
                                                          .name = "prepass-depth")
                          .value;

    const Mel_Gpu_Vertex_Element layout[] = {
        { .location = 0, .format = MEL_GPU_FORMAT_RGB32_FLOAT, .offset = offsetof(Pt_Vertex, pos) },
        { .location = 1, .format = MEL_GPU_FORMAT_RGBA32_FLOAT, .offset = offsetof(Pt_Vertex, color) },
    };

    Mel_Gpu_Depth_Stencil depth_ds = { .depth_test = true, .depth_write = true, .depth_compare = MEL_GPU_COMPARE_LESS };
    p->depth_pl = mel_gpu_pipeline_create(dev,
                                          .shader = p->depth_shader,
                                          .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                          .cull = MEL_GPU_CULL_BACK,
                                          .front_face = MEL_GPU_FRONT_FACE_CCW,
                                          .depth_format = MEL_GPU_FORMAT_D32_FLOAT,
                                          .depth_stencil = &depth_ds,
                                          .vertex_layout = layout,
                                          .vertex_layout_count = 2,
                                          .vertex_stride = sizeof(Pt_Vertex),
                                          .name = "prepass-depth")
                      .value;

    Mel_Gpu_Depth_Stencil lit_ds = { .depth_test = true, .depth_write = false, .depth_compare = MEL_GPU_COMPARE_EQUAL };
    p->lit_pl = mel_gpu_pipeline_create(dev,
                                        .shader = p->shader,
                                        .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                        .cull = MEL_GPU_CULL_BACK,
                                        .front_face = MEL_GPU_FRONT_FACE_CCW,
                                        .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                        .depth_format = MEL_GPU_FORMAT_D32_FLOAT,
                                        .depth_stencil = &lit_ds,
                                        .vertex_layout = layout,
                                        .vertex_layout_count = 2,
                                        .vertex_stride = sizeof(Pt_Vertex),
                                        .name = "prepass-lit")
                    .value;

    for (i32 i = 0; i < VBO_FRAMES; ++i)
        p->vbo[i] = mel_gpu_buffer_create(dev, .size = SCENE_VERTS * sizeof(Pt_Vertex), .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD, .name = "prepass-vbo").value;

    if (!bindless_present_init(&p->present, dev, mel_gpu_swapchain_format(sc)))
        return p;

    p->ready = true;
    return p;
}

static void prepass_resize(void* state, i32 w, i32 h)
{
    Prepass* p = state;
    if (!p->ready)
        return;
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    if (w == p->w && h == p->h && !handle_zero(p->color.slot))
        return;

    destroy_targets(p);
    p->w = w;
    p->h = h;
    p->color = mel_gpu_texture_create(p->dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { (u32)w, (u32)h, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_SAMPLED, .name = "prepass-color").value;
    p->color_view = mel_gpu_texture_default_view(p->dev, p->color).value;
    p->color_slot = mel_gpu_texture_view_bindless_slot(p->dev, p->color_view);
    p->depth = mel_gpu_texture_create(p->dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { (u32)w, (u32)h, 1 }, .format = MEL_GPU_FORMAT_D32_FLOAT, .usage = MEL_GPU_TEXTURE_ATTACHMENT, .name = "prepass-depth").value;
    p->depth_view = mel_gpu_texture_default_view(p->dev, p->depth).value;
    p->targets_fresh = true;
}

static u32 build_scene(Prepass* p, Pt_Vertex* out)
{
    const f32 aspect = (f32)p->w / (f32)p->h;
    const f32 f = 1.0f / tanf(0.5f * 1.0472f);
    const f32 znear = 0.5f, zfar = 24.0f;

    u32 count = 0;
    for (i32 c = 0; c < CUBE_COUNT; ++c)
    {
        f32 t = (f32)c / (f32)CUBE_COUNT;
        f32 depth = -3.0f - t * 14.0f;
        f32 swirl = (f32)p->angle * 0.4f + t * 3.0f;
        V3  centre = { 0.6f * cosf(swirl) * t, 0.6f * sinf(swirl * 1.1f) * t, depth };
        f32 ax = (f32)(p->angle * 0.6) + t * 4.0f;
        f32 ay = (f32)(p->angle * 0.9) + t * 2.0f;
        f32 scale = 1.6f - 0.6f * t;

        V3 view[8];
        for (i32 i = 0; i < 8; ++i)
        {
            V3 r = rotate((V3){ CORNERS[i].x * scale, CORNERS[i].y * scale, CORNERS[i].z * scale }, ax, ay);
            view[i] = (V3){ r.x + centre.x, r.y + centre.y, r.z + centre.z };
        }

        for (i32 face = 0; face < 6; ++face)
        {
            const i32* q = FACES[face];
            const i32  idx[2][3] = { { q[0], q[1], q[2] }, { q[0], q[2], q[3] } };
            V3         base = FACE_COLOR[face];
            f32        shade = 0.45f + 0.55f * (1.0f - t);
            for (i32 tri = 0; tri < 2; ++tri)
                for (i32 k = 0; k < 3; ++k)
                {
                    V3  vp = view[idx[tri][k]];
                    f32 z = -vp.z;
                    if (z < znear)
                        z = znear;
                    f32 ndc_x = (vp.x * f / aspect) / z;
                    f32 ndc_y = (vp.y * f) / z;
                    f32 ndc_z = (zfar / (zfar - znear)) * (1.0f - znear / z);
                    out[count++] = (Pt_Vertex){ { ndc_x, ndc_y, ndc_z }, { base.x * shade, base.y * shade, base.z * shade, 1.0f } };
                }
        }
    }
    return count;
}

static void prepass_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    Prepass* p = state;
    p->angle += dt;
    hud_frame(&p->hud, dt, "depth prepass → EQUAL lit pass (overdraw killed)");

    if (!p->ready || handle_zero(p->color.slot))
    {
        mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.20f, 0.10f, 0.02f, 1.0f));
        mel_gpu_cmd_end_pass(cmd);
        return;
    }

    Pt_Vertex* verts = malloc(SCENE_VERTS * sizeof(Pt_Vertex));
    u32        count = build_scene(p, verts);
    p->vframe = (p->vframe + 1) % VBO_FRAMES;
    Mel_Gpu_Buffer vbo = p->vbo[p->vframe];
    mel_gpu_buffer_write(p->dev, vbo, verts, count * sizeof(Pt_Vertex));
    free(verts);

    Mel_Gpu_Subresource_Range crange = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    Mel_Gpu_Subresource_Range drange = { MEL_GPU_ASPECT_DEPTH, 0, 1, 0, 1 };
    bool                      fresh = p->targets_fresh;
    p->targets_fresh = false;

    Mel_Gpu_Resource_State color_src = fresh ? MEL_GPU_STATE_COMMON : MEL_GPU_STATE_SHADER_RESOURCE;
    mel_gpu_cmd_texture_barrier(cmd, p->color, crange, color_src, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Resource_State depth_src = fresh ? MEL_GPU_STATE_COMMON : MEL_GPU_STATE_DEPTH_WRITE;
    mel_gpu_cmd_texture_barrier(cmd, p->depth, drange, depth_src, MEL_GPU_STATE_DEPTH_WRITE);

    Mel_Gpu_Depth_Attachment pre_depth = { .view = p->depth_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear_depth = 1.0f };
    mel_gpu_cmd_begin_rendering(cmd, .depth = &pre_depth, .width = (u32)p->w, .height = (u32)p->h);
    mel_gpu_cmd_bind_pipeline(cmd, p->depth_pl);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 0, vbo);
    mel_gpu_cmd_draw(cmd, count, 1);
    mel_gpu_cmd_end_rendering(cmd);

    Mel_Gpu_Color_Attachment lit_color = { .view = p->color_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.03f, 0.04f, 0.06f, 1.0f) };
    Mel_Gpu_Depth_Attachment lit_depth = { .view = p->depth_view, .load = MEL_GPU_LOAD_LOAD, .store = MEL_GPU_STORE_DONT_CARE, .clear_depth = 1.0f };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &lit_color, .color_count = 1, .depth = &lit_depth, .width = (u32)p->w, .height = (u32)p->h);
    mel_gpu_cmd_bind_pipeline(cmd, p->lit_pl);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 0, vbo);
    mel_gpu_cmd_draw(cmd, count, 1);
    mel_gpu_cmd_end_rendering(cmd);

    mel_gpu_cmd_texture_barrier(cmd, p->color, crange, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_SHADER_RESOURCE);
    bindless_present_blit(&p->present, cmd, p->color_slot, mel_gpu_rgba(0, 0, 0, 1));
}

static void prepass_teardown(void* state)
{
    Prepass* p = state;
    if (!p)
        return;
    if (p->ready)
    {
        destroy_targets(p);
        bindless_present_teardown(&p->present);
        for (i32 i = 0; i < VBO_FRAMES; ++i)
            mel_gpu_buffer_destroy(p->dev, p->vbo[i]);
        mel_gpu_pipeline_destroy(p->dev, p->lit_pl);
        mel_gpu_pipeline_destroy(p->dev, p->depth_pl);
        mel_gpu_shader_destroy(p->dev, p->depth_shader);
        mel_gpu_shader_destroy(p->dev, p->shader);
    }
    free(p);
}

const Graphical_App PREPASS_APP = {
    .title = "depth-prepass",
    .init = prepass_init,
    .resize = prepass_resize,
    .render = prepass_render,
    .teardown = prepass_teardown,
};
