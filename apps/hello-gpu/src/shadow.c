#include <math.h>
#include <stdlib.h>

#include <log/log.h>

#include "shadow.h"
#include "hud.h"
#include "bindless_present.h"
#include "shadow_depth_vert_spv.h"
#include "shadow_depth_frag_spv.h"
#include "shadow_scene_vert_spv.h"
#include "shadow_scene_frag_spv.h"

#define SHADOW_RES  512
#define BOX_COUNT   5
#define TRIS_PER_BOX 12
#define GROUND_TRIS  2
#define MAX_VERTS   ((BOX_COUNT * TRIS_PER_BOX + GROUND_TRIS) * 3)

typedef struct
{
    f32 pos[3];
} Depth_Vertex;

typedef struct
{
    f32 pos[3];
    f32 normal[3];
    f32 color[3];
    f32 shadow_uvz[3];
} Scene_Vertex;

typedef struct
{
    u32 shadow_tex;
    u32 shadow_smp;
    u32 pad0;
    u32 pad1;
    f32 light_dir[3];
    f32 pad2;
} Scene_Root;

typedef struct
{
    Mel_Gpu_Device*  dev;
    bool             ready;

    Mel_Gpu_Shader   depth_sh;
    Mel_Gpu_Pipeline depth_pl;
    Mel_Gpu_Shader   scene_sh;
    Mel_Gpu_Pipeline scene_pl;
    Mel_Gpu_Sampler  sampler;

    Mel_Gpu_Texture      shadow_map;
    Mel_Gpu_Texture_View shadow_map_view;
    u32                  shadow_slot;

    i32                  w, h;
    Mel_Gpu_Texture      color_tex;
    Mel_Gpu_Texture_View color_view;
    u32                  color_slot;
    bool                 color_fresh;

    Mel_Gpu_Texture      scene_depth;
    Mel_Gpu_Texture_View scene_depth_view;
    bool                 scene_depth_fresh;

    Mel_Gpu_Buffer depth_vbo;
    Mel_Gpu_Buffer scene_vbo;

    Bindless_Present present;
    Hud              hud;
    f64              t;
} Shadow;

static inline bool handle_zero(Mel_SlotMap_Handle h) { return h.index == 0 && h.generation == 0; }

typedef struct { f32 x, y, z; } V3;

static V3  v3_add(V3 a, V3 b)    { return (V3){ a.x+b.x, a.y+b.y, a.z+b.z }; }
static V3  v3_sub(V3 a, V3 b)    { return (V3){ a.x-b.x, a.y-b.y, a.z-b.z }; }
static V3  v3_scale(V3 a, f32 s) { return (V3){ a.x*s, a.y*s, a.z*s }; }
static f32 v3_dot(V3 a, V3 b)    { return a.x*b.x + a.y*b.y + a.z*b.z; }
static V3  v3_cross(V3 a, V3 b)  { return (V3){ a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x }; }
static V3  v3_norm(V3 a)
{
    f32 l = sqrtf(v3_dot(a, a));
    return (l < 1e-6f) ? (V3){ 0, 1, 0 } : v3_scale(a, 1.0f / l);
}

static void ortho(V3 p, V3 eye, V3 right, V3 up, V3 fwd, f32 hw, f32 hh, f32 near, f32 far,
                  f32* ox, f32* oy, f32* oz)
{
    V3  d  = v3_sub(p, eye);
    *ox = v3_dot(d, right) / hw;
    *oy = v3_dot(d, up) / hh;
    f32 z = v3_dot(d, fwd);
    *oz = (z - near) / (far - near);
    (void)fwd;
}

static void persp(V3 p, V3 eye, V3 right, V3 up, V3 fwd, f32 ftan, f32 asp, f32 near, f32 far,
                  f32* ox, f32* oy, f32* oz)
{
    V3  d  = v3_sub(p, eye);
    f32 rz = v3_dot(d, fwd);
    if (rz < near) rz = near;
    *ox = v3_dot(d, right) / (rz * ftan * asp);
    *oy = v3_dot(d, up) / (rz * ftan);
    f32 a = far / (far - near);
    *oz = a + (-far * near / (far - near)) / rz;
}

typedef struct
{
    u32          count;
    Depth_Vertex dv[MAX_VERTS];
    Scene_Vertex sv[MAX_VERTS];
} Scene_Data;

static void push_quad(Scene_Data* sd, V3 q[4], V3 normal, V3 color,
                      V3 le, V3 lr, V3 lu, V3 lf, f32 lhw, f32 lhh, f32 lnear, f32 lfar,
                      V3 ce, V3 cr, V3 cu, V3 cf, f32 ftan, f32 asp, f32 cn, f32 cfa)
{
    i32 tri[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
    for (i32 t = 0; t < 2; ++t)
        for (i32 k = 0; k < 3; ++k)
        {
            u32 i = sd->count++;
            V3  p = q[tri[t][k]];

            f32 lx, ly, lz;
            ortho(p, le, lr, lu, lf, lhw, lhh, lnear, lfar, &lx, &ly, &lz);

            f32 cx, cy, cz;
            persp(p, ce, cr, cu, cf, ftan, asp, cn, cfa, &cx, &cy, &cz);

            sd->dv[i] = (Depth_Vertex){ { lx, ly, lz } };
            sd->sv[i] = (Scene_Vertex){
                .pos        = { cx, cy, cz },
                .normal     = { normal.x, normal.y, normal.z },
                .color      = { color.x, color.y, color.z },
                .shadow_uvz = { lx * 0.5f + 0.5f, ly * 0.5f + 0.5f, lz },
            };
        }
}

static const V3 FACE_NORMALS[6] = {
    { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 },
};

static void push_box(Scene_Data* sd, V3 c, f32 sx, f32 sy, f32 sz, V3 color,
                     V3 le, V3 lr, V3 lu, V3 lf, f32 lhw, f32 lhh, f32 lnear, f32 lfar,
                     V3 ce, V3 cr, V3 cu, V3 cf, f32 ftan, f32 asp, f32 cn, f32 cfa)
{
    f32 hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
    V3 face_quads[6][4] = {
        { {c.x+hx,c.y-hy,c.z-hz},{c.x+hx,c.y+hy,c.z-hz},{c.x+hx,c.y+hy,c.z+hz},{c.x+hx,c.y-hy,c.z+hz} },
        { {c.x-hx,c.y-hy,c.z+hz},{c.x-hx,c.y+hy,c.z+hz},{c.x-hx,c.y+hy,c.z-hz},{c.x-hx,c.y-hy,c.z-hz} },
        { {c.x-hx,c.y+hy,c.z-hz},{c.x-hx,c.y+hy,c.z+hz},{c.x+hx,c.y+hy,c.z+hz},{c.x+hx,c.y+hy,c.z-hz} },
        { {c.x-hx,c.y-hy,c.z+hz},{c.x-hx,c.y-hy,c.z-hz},{c.x+hx,c.y-hy,c.z-hz},{c.x+hx,c.y-hy,c.z+hz} },
        { {c.x-hx,c.y-hy,c.z+hz},{c.x+hx,c.y-hy,c.z+hz},{c.x+hx,c.y+hy,c.z+hz},{c.x-hx,c.y+hy,c.z+hz} },
        { {c.x+hx,c.y-hy,c.z-hz},{c.x-hx,c.y-hy,c.z-hz},{c.x-hx,c.y+hy,c.z-hz},{c.x+hx,c.y+hy,c.z-hz} },
    };
    for (i32 f = 0; f < 6; ++f)
        push_quad(sd, face_quads[f], FACE_NORMALS[f], color,
                  le, lr, lu, lf, lhw, lhh, lnear, lfar, ce, cr, cu, cf, ftan, asp, cn, cfa);
}

static void destroy_rtts(Shadow* s)
{
    if (!handle_zero(s->color_view.slot))       mel_gpu_texture_view_destroy(s->dev, s->color_view);
    if (!handle_zero(s->color_tex.slot))        mel_gpu_texture_destroy(s->dev, s->color_tex);
    if (!handle_zero(s->scene_depth_view.slot)) mel_gpu_texture_view_destroy(s->dev, s->scene_depth_view);
    if (!handle_zero(s->scene_depth.slot))      mel_gpu_texture_destroy(s->dev, s->scene_depth);
    s->color_view = s->scene_depth_view = (Mel_Gpu_Texture_View){ 0 };
    s->color_tex  = s->scene_depth      = (Mel_Gpu_Texture){ 0 };
}

static void make_rtts(Shadow* s, i32 w, i32 h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w == s->w && h == s->h && !handle_zero(s->color_tex.slot)) return;
    destroy_rtts(s);
    s->w = w; s->h = h;

    s->color_tex  = mel_gpu_texture_create(s->dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { (u32)w, (u32)h, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_SAMPLED, .name = "shadow-color").value;
    s->color_view = mel_gpu_texture_default_view(s->dev, s->color_tex).value;
    s->color_slot = mel_gpu_texture_view_bindless_slot(s->dev, s->color_view);
    s->color_fresh = true;

    s->scene_depth      = mel_gpu_texture_create(s->dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { (u32)w, (u32)h, 1 }, .format = MEL_GPU_FORMAT_D32_FLOAT, .usage = MEL_GPU_TEXTURE_ATTACHMENT, .name = "shadow-scene-depth").value;
    s->scene_depth_view = mel_gpu_texture_default_view(s->dev, s->scene_depth).value;
    s->scene_depth_fresh = true;
}

static void* shadow_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Shadow* s = calloc(1, sizeof *s);
    s->dev = dev;
    hud_init(&s->hud, dev);

    if (!mel_gpu_bindless_available(dev))
    {
        mel_log_warn("hello-gpu", "shadow: bindless heap unavailable");
        return s;
    }

    s->depth_sh = mel_gpu_shader_create_from_bytecode(dev,
                                                      .spirv_vertex        = SHADOW_DEPTH_VERT_SPV,
                                                      .spirv_vertex_size   = sizeof SHADOW_DEPTH_VERT_SPV,
                                                      .spirv_fragment      = SHADOW_DEPTH_FRAG_SPV,
                                                      .spirv_fragment_size = sizeof SHADOW_DEPTH_FRAG_SPV,
                                                      .vertex_entry = "main", .fragment_entry = "main",
                                                      .name = "shadow-depth")
                     .value;
    s->scene_sh = mel_gpu_shader_create_from_bytecode(dev,
                                                      .spirv_vertex        = SHADOW_SCENE_VERT_SPV,
                                                      .spirv_vertex_size   = sizeof SHADOW_SCENE_VERT_SPV,
                                                      .spirv_fragment      = SHADOW_SCENE_FRAG_SPV,
                                                      .spirv_fragment_size = sizeof SHADOW_SCENE_FRAG_SPV,
                                                      .vertex_entry = "main", .fragment_entry = "main",
                                                      .name = "shadow-scene")
                     .value;

    const Mel_Gpu_Vertex_Element depth_layout[] = {
        { .location = 0, .format = MEL_GPU_FORMAT_RGB32_FLOAT, .offset = 0 },
    };
    Mel_Gpu_Depth_Stencil depth_ds = { .depth_test = true, .depth_write = true, .depth_compare = MEL_GPU_COMPARE_LESS };
    s->depth_pl = mel_gpu_pipeline_create(dev,
                                          .shader              = s->depth_sh,
                                          .topology            = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                          .cull                = MEL_GPU_CULL_BACK,
                                          .front_face          = MEL_GPU_FRONT_FACE_CCW,
                                          .depth_format        = MEL_GPU_FORMAT_D32_FLOAT,
                                          .depth_stencil       = &depth_ds,
                                          .vertex_layout       = depth_layout,
                                          .vertex_layout_count = 1,
                                          .vertex_stride       = sizeof(Depth_Vertex),
                                          .name                = "shadow-depth")
                     .value;

    const Mel_Gpu_Vertex_Element scene_layout[] = {
        { .location = 0, .format = MEL_GPU_FORMAT_RGB32_FLOAT, .offset = offsetof(Scene_Vertex, pos) },
        { .location = 1, .format = MEL_GPU_FORMAT_RGB32_FLOAT, .offset = offsetof(Scene_Vertex, normal) },
        { .location = 2, .format = MEL_GPU_FORMAT_RGB32_FLOAT, .offset = offsetof(Scene_Vertex, color) },
        { .location = 3, .format = MEL_GPU_FORMAT_RGB32_FLOAT, .offset = offsetof(Scene_Vertex, shadow_uvz) },
    };
    Mel_Gpu_Depth_Stencil scene_ds = { .depth_test = true, .depth_write = true, .depth_compare = MEL_GPU_COMPARE_LESS };
    s->scene_pl = mel_gpu_pipeline_create(dev,
                                          .shader              = s->scene_sh,
                                          .topology            = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                          .cull                = MEL_GPU_CULL_BACK,
                                          .front_face          = MEL_GPU_FRONT_FACE_CCW,
                                          .color_format        = MEL_GPU_FORMAT_RGBA8_UNORM,
                                          .depth_format        = MEL_GPU_FORMAT_D32_FLOAT,
                                          .depth_stencil       = &scene_ds,
                                          .vertex_layout       = scene_layout,
                                          .vertex_layout_count = 4,
                                          .vertex_stride       = sizeof(Scene_Vertex),
                                          .push_constant_size  = sizeof(Scene_Root),
                                          .name                = "shadow-scene")
                     .value;

    s->shadow_map      = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { SHADOW_RES, SHADOW_RES, 1 }, .format = MEL_GPU_FORMAT_D32_FLOAT, .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_SAMPLED, .name = "shadow-map").value;
    s->shadow_map_view = mel_gpu_texture_default_view(dev, s->shadow_map).value;
    s->shadow_slot     = mel_gpu_texture_view_bindless_slot(dev, s->shadow_map_view);

    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev,
                                                               .min_filter = MEL_GPU_FILTER_NEAREST,
                                                               .mag_filter = MEL_GPU_FILTER_NEAREST,
                                                               .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE,
                                                               .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE,
                                                               .name = "shadow-sampler");
    if (mel_gpu_failed(smp.status)) return s;
    s->sampler = smp.value;

    s->depth_vbo = mel_gpu_buffer_create(dev, .size = MAX_VERTS * sizeof(Depth_Vertex), .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD, .name = "shadow-depth-vbo").value;
    s->scene_vbo = mel_gpu_buffer_create(dev, .size = MAX_VERTS * sizeof(Scene_Vertex), .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD, .name = "shadow-scene-vbo").value;

    if (!bindless_present_init(&s->present, dev, mel_gpu_swapchain_format(sc))) return s;

    Mel_Gpu_Swapchain_Extent ext = mel_gpu_swapchain_extent(sc);
    make_rtts(s, (i32)ext.width, (i32)ext.height);

    s->ready = true;
    return s;
}

static void shadow_resize(void* state, i32 w, i32 h)
{
    Shadow* s = state;
    if (!s->ready) return;
    make_rtts(s, w, h);
}

static void shadow_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    Shadow* s = state;
    s->t += dt;
    hud_frame(&s->hud, dt, "shadow map · depth-from-light → lit scene with shadow compare");

    if (!s->ready || handle_zero(s->shadow_map.slot))
    {
        mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.1f, 0.08f, 0.06f, 1.0f));
        mel_gpu_cmd_end_pass(cmd);
        return;
    }

    f32 aspect = (s->h > 0) ? (f32)s->w / (f32)s->h : 1.0f;
    f32 fov_tan = tanf(0.5f * 0.9f);

    V3 light_dir_raw = (V3){ 1.5f, -2.0f, 1.0f };
    V3 light_dir     = v3_norm(light_dir_raw);
    V3 light_pos     = v3_scale(light_dir, -6.0f);
    V3 lf            = v3_norm(v3_sub((V3){ 0, 0, 0 }, light_pos));
    V3 world_up      = (V3){ 0, 1, 0 };
    V3 lr            = v3_norm(v3_cross(world_up, lf));
    V3 lu            = v3_cross(lf, lr);
    f32 lhw = 3.5f, lhh = 3.5f, lnear = 0.5f, lfar = 14.0f;

    f32 yaw      = (f32)(s->t * 0.28);
    V3  cam_pos  = (V3){ 3.5f * sinf(yaw), 2.5f, 3.5f * cosf(yaw) };
    V3  cf       = v3_norm(v3_sub((V3){ 0, 0.2f, 0 }, cam_pos));
    V3  cr       = v3_norm(v3_cross(world_up, cf));
    V3  cu       = v3_cross(cf, cr);
    f32 cnear = 0.2f, cfar = 20.0f;

    static const V3 BOX_POS[BOX_COUNT]         = { {0.0f,0.35f,0.0f},{0.9f,0.25f,0.5f},{-0.8f,0.20f,-0.4f},{-0.5f,0.15f,0.7f},{0.7f,0.30f,-0.7f} };
    static const f32 BOX_SZ[BOX_COUNT][3]      = { {0.70f,0.70f,0.70f},{0.50f,0.50f,0.50f},{0.40f,0.40f,0.40f},{0.30f,0.30f,0.30f},{0.60f,0.60f,0.60f} };
    static const V3 BOX_COL[BOX_COUNT]         = { {0.85f,0.25f,0.20f},{0.20f,0.70f,0.35f},{0.25f,0.40f,0.85f},{0.85f,0.70f,0.15f},{0.65f,0.30f,0.80f} };

    Scene_Data* sd = malloc(sizeof *sd);
    sd->count = 0;

    for (i32 i = 0; i < BOX_COUNT; ++i)
        push_box(sd, BOX_POS[i], BOX_SZ[i][0], BOX_SZ[i][1], BOX_SZ[i][2], BOX_COL[i],
                 light_pos, lr, lu, lf, lhw, lhh, lnear, lfar,
                 cam_pos,   cr, cu, cf, fov_tan, aspect, cnear, cfar);

    V3 gnd_q[4] = { {-2.5f,0.0f,-2.5f},{-2.5f,0.0f,2.5f},{2.5f,0.0f,2.5f},{2.5f,0.0f,-2.5f} };
    push_quad(sd, gnd_q, (V3){0,1,0}, (V3){0.5f,0.5f,0.5f},
              light_pos, lr, lu, lf, lhw, lhh, lnear, lfar,
              cam_pos,   cr, cu, cf, fov_tan, aspect, cnear, cfar);

    u32 vert_count = sd->count;
    mel_gpu_buffer_write(s->dev, s->depth_vbo, sd->dv, vert_count * sizeof(Depth_Vertex));
    mel_gpu_buffer_write(s->dev, s->scene_vbo, sd->sv, vert_count * sizeof(Scene_Vertex));
    free(sd);

    Mel_Gpu_Subresource_Range drange = { MEL_GPU_ASPECT_DEPTH, 0, 1, 0, 1 };
    Mel_Gpu_Subresource_Range crange = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };

    mel_gpu_cmd_texture_barrier(cmd, s->shadow_map, drange, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_DEPTH_WRITE);

    Mel_Gpu_Depth_Attachment shadow_da = { .view = s->shadow_map_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear_depth = 1.0f };
    mel_gpu_cmd_begin_rendering(cmd, .depth = &shadow_da, .width = SHADOW_RES, .height = SHADOW_RES);
    mel_gpu_cmd_bind_pipeline(cmd, s->depth_pl);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 0, s->depth_vbo);
    mel_gpu_cmd_draw(cmd, vert_count, 1);
    mel_gpu_cmd_end_rendering(cmd);

    mel_gpu_cmd_texture_barrier(cmd, s->shadow_map, drange, MEL_GPU_STATE_DEPTH_WRITE, MEL_GPU_STATE_SHADER_RESOURCE);

    Mel_Gpu_Resource_State color_init = s->color_fresh ? MEL_GPU_STATE_COMMON : MEL_GPU_STATE_RENDER_TARGET;
    s->color_fresh = false;
    mel_gpu_cmd_texture_barrier(cmd, s->color_tex, crange, color_init, MEL_GPU_STATE_RENDER_TARGET);

    Mel_Gpu_Resource_State sd_init = s->scene_depth_fresh ? MEL_GPU_STATE_COMMON : MEL_GPU_STATE_DEPTH_WRITE;
    s->scene_depth_fresh = false;
    mel_gpu_cmd_texture_barrier(cmd, s->scene_depth, drange, sd_init, MEL_GPU_STATE_DEPTH_WRITE);

    Scene_Root root = {
        .shadow_tex = s->shadow_slot,
        .shadow_smp = mel_gpu_sampler_bindless_slot(s->dev, s->sampler),
        .light_dir  = { light_dir.x, light_dir.y, light_dir.z },
    };

    Mel_Gpu_Color_Attachment ca  = { .view = s->color_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.55f, 0.65f, 0.80f, 1.0f) };
    Mel_Gpu_Depth_Attachment sda = { .view = s->scene_depth_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_DONT_CARE, .clear_depth = 1.0f };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &ca, .color_count = 1, .depth = &sda, .width = (u32)s->w, .height = (u32)s->h);
    mel_gpu_cmd_bind_pipeline(cmd, s->scene_pl);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 0, s->scene_vbo);
    mel_gpu_cmd_draw(cmd, vert_count, 1);
    mel_gpu_cmd_end_rendering(cmd);

    mel_gpu_cmd_texture_barrier(cmd, s->shadow_map, drange, MEL_GPU_STATE_SHADER_RESOURCE, MEL_GPU_STATE_COMMON);
    mel_gpu_cmd_texture_barrier(cmd, s->color_tex, crange, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_SHADER_RESOURCE);

    bindless_present_blit(&s->present, cmd, s->color_slot, mel_gpu_rgba(0, 0, 0, 1));

    mel_gpu_cmd_texture_barrier(cmd, s->color_tex, crange, MEL_GPU_STATE_SHADER_RESOURCE, MEL_GPU_STATE_RENDER_TARGET);
}

static void shadow_teardown(void* state)
{
    Shadow* s = state;
    if (!s) return;
    if (s->ready)
    {
        bindless_present_teardown(&s->present);
        mel_gpu_buffer_destroy(s->dev, s->scene_vbo);
        mel_gpu_buffer_destroy(s->dev, s->depth_vbo);
        mel_gpu_sampler_destroy(s->dev, s->sampler);
        destroy_rtts(s);
        if (!handle_zero(s->shadow_map_view.slot)) mel_gpu_texture_view_destroy(s->dev, s->shadow_map_view);
        if (!handle_zero(s->shadow_map.slot))      mel_gpu_texture_destroy(s->dev, s->shadow_map);
        mel_gpu_pipeline_destroy(s->dev, s->scene_pl);
        mel_gpu_shader_destroy(s->dev, s->scene_sh);
        mel_gpu_pipeline_destroy(s->dev, s->depth_pl);
        mel_gpu_shader_destroy(s->dev, s->depth_sh);
    }
    free(s);
}

const Graphical_App SHADOW_APP = {
    .title    = "shadow-mapping",
    .init     = shadow_init,
    .resize   = shadow_resize,
    .render   = shadow_render,
    .teardown = shadow_teardown,
};
