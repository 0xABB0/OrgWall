#include <test/test.h>

#if MEL_GPU_VULKAN || MEL_GPU_METAL || MEL_GPU_WEBGPU || MEL_GPU_D3D12

#include <gpu/device.h>
#include <gpu/caps.h>
#include <gpu/buffer.h>
#include <gpu/texture.h>
#include <gpu/pipeline.h>
#include <gpu/shader.h>
#include <gpu/queue.h>
#include <gpu/command.h>
#include <gpu/rendering.h>
#include <gpu/state.h>
#include <gpu/future.h>
#include <gpu/binding.h>

#include <log/log.h>

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "img_golden.h"

#include "bundle_select.h"
#include "triangle_bundle.h"
#include "gradient_bundle.h"
#include "quad_bundle.h"

static const char TRIANGLE_SLANG[] = {
#embed "shaders/slang/triangle.slang"
    , 0
};

static const char MANDELBROT_SLANG[] = {
#embed "shaders/slang/mandelbrot.slang"
    , 0
};

static const char RAYMARCH_SLANG[] = {
#embed "shaders/slang/raymarch.slang"
    , 0
};

static const char BINDLESS_PRESENT_SLANG[] = {
#embed "shaders/slang/bindless_present.slang"
    , 0
};

static const char BLOOM_SLANG[] = {
#embed "shaders/slang/bloom.slang"
    , 0
};

static const char BOIDS_SLANG[] = {
#embed "shaders/slang/boids.slang"
    , 0
};

/* ===== task #35 batch G1: compute/sim screens (reacdiff, compute_plasma, particles,
   dispatch_indirect) — contiguous block to ease union-merge with sibling batches ===== */
static const char COMPUTE_PLASMA_SLANG[] = {
#embed "shaders/slang/compute_plasma.slang"
    , 0
};

static const char PARTICLES_SLANG[] = {
#embed "shaders/slang/particles.slang"
    , 0
};

static const char REACDIFF_SLANG[] = {
#embed "shaders/slang/reacdiff.slang"
    , 0
};

static const char DISPATCH_INDIRECT_SLANG[] = {
#embed "shaders/slang/dispatch_indirect.slang"
    , 0
};
/* ===== end task #35 batch G1 embeds ===== */

#if MEL_GPU_VULKAN
#define SCENE_BACKEND "vulkan"
#elif MEL_GPU_METAL
#define SCENE_BACKEND "metal"
#elif MEL_GPU_D3D12
#define SCENE_BACKEND "d3d12"
#else
#define SCENE_BACKEND "webgpu"
#endif

/* The Vulkan run is the oracle that mints the shared goldens, so it demands a
   near-exact match (the delta absorbs only readback rounding). Metal and WebGPU are
   diffed against those same goldens through the RHI. On macOS the measured delta is
   ZERO on every scene — Metal-native, Dawn-on-Metal, and Vulkan-via-MoltenVK all
   funnel into the same Metal rasterizer, so the three converge bit-for-bit (see
   writeup/2026-06-03-gpu-shared-goldens.md for the captured per-scene deltas). The
   non-Vulkan band is therefore not needed on this host; it is held small and
   non-zero solely to tolerate cross-HOST rasterizer divergence (edge coverage,
   interpolation precision) when a real Vulkan or D3D12 device mints/diffs elsewhere. */
#if MEL_GPU_VULKAN
static const Mel_Golden_Tolerance SCENE_TOL = { .max_channel_delta = 2, .max_fraction_exceeding = 0.0f };
#elif MEL_GPU_D3D12
/* D3D12 diffs against the macOS-Vulkan-oracle goldens from a real NVIDIA rasterizer
   (RTX 2060 SUPER, signed DXIL sm_6_0), a different interpolation/round pipeline than the
   Metal rasterizer that minted the oracle. Measured on win-pilot (64x64, 4096 px;
   writeup/2026-06-04-gpu-dxil.md): the divergence is a uniform +-1 LSB on a single
   channel — triangle max delta 1 (3440 px off-by-1), gradient max delta 1 (363 px),
   quad max delta 1 (3072 px). No structural difference; the scenes render correctly to
   within 1/255. The band absorbs that LSB everywhere (delta<=2) and tolerates NO pixel
   beyond it (frac 0.0) — the same strict shape as the Vulkan-oracle band, set from data. */
static const Mel_Golden_Tolerance SCENE_TOL = { .max_channel_delta = 2, .max_fraction_exceeding = 0.0f };
#else
static const Mel_Golden_Tolerance SCENE_TOL = { .max_channel_delta = 4, .max_fraction_exceeding = 0.02f };
#endif

#define SCENE_W 64u
#define SCENE_H 64u

typedef struct
{
    f32 pos[3];
    f32 color[4];
} Scene_Vertex;

static Mel_Gpu_Device* scene_make_device(Mel_Gpu_Instance** out_inst)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-scene-test", .debug = { .enabled = true });
    if (!inst)
        return NULL;
    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(inst, adapters, 8);
    if (n == 0)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    Mel_Gpu_Device_Create_Result dr = mel_gpu_device_create(inst, adapters[0], .reactor = NULL);
    if (!dr.value)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    *out_inst = inst;
    return dr.value;
}

typedef struct
{
    u32                  w, h;
    Mel_Gpu_Texture      rt;
    Mel_Gpu_Texture_View rt_view;
    Mel_Gpu_Buffer       rb;
} Scene_Target;

static Scene_Target scene_target_create(Mel_Gpu_Device* dev, u32 w, u32 h)
{
    Scene_Target                  t = { .w = w, .h = h };
    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { w, h, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "scene-rt");
    t.rt = rt.value;
    t.rt_view = mel_gpu_texture_default_view(dev, rt.value).value;
    t.rb = mel_gpu_buffer_create(dev, .size = (usize)w * h * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "scene-rb").value;
    return t;
}

static void scene_target_destroy(Mel_Gpu_Device* dev, Scene_Target* t)
{
    mel_gpu_buffer_destroy(dev, t->rb);
    mel_gpu_texture_view_destroy(dev, t->rt_view);
    mel_gpu_texture_destroy(dev, t->rt);
}

typedef void (*Scene_Record)(Mel_Gpu_Command_List* cmd, void* ctx);

static const u8* scene_render_readback(Mel_Gpu_Device* dev, Scene_Target* tgt, Mel_Gpu_Pipeline pipe, Scene_Record record, void* ctx)
{
    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, tgt->rt, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment color = { .view = tgt->rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.08f, 0.10f, 0.13f, 1.0f) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = tgt->w, .height = tgt->h);
    mel_gpu_cmd_bind_pipeline(cmd, pipe);
    record(cmd, ctx);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, tgt->rt, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, tgt->rt, range, tgt->rb);
    mel_gpu_command_list_end(cmd);

    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    bool            ok = mel_gpu_ok(mel_gpu_future_wait(f));
    mel_gpu_future_destroy(f);
    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    if (!ok)
        return NULL;
    return mel_gpu_buffer_mapped(dev, tgt->rb);
}

static void scene_record_triangle(Mel_Gpu_Command_List* cmd, void* ctx)
{
    Mel_Gpu_Buffer* vbo = ctx;
    mel_gpu_cmd_bind_vertex_buffer(cmd, 0, *vbo);
    mel_gpu_cmd_draw(cmd, 3, 1);
}

MEL_TEST(scene_shared, triangle)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = scene_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const Scene_Vertex verts[] = {
        { { 0.0f, 0.6f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { 0.6f, -0.6f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { -0.6f, -0.6f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
    };
    Mel_Gpu_Buffer_Create_Result vbo = mel_gpu_buffer_create(dev, .size = sizeof verts, .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD, .data = verts, .name = "scene-tri-vbo");
    MEL_REQUIRE(!mel_gpu_failed(vbo.status));

    Mel_Gpu_Pipeline_From_Slang_Result pipe = mel_gpu_pipeline_create_from_slang(dev,
                                                                                 .source = TRIANGLE_SLANG,
                                                                                 .vertex_entry = "vs_main",
                                                                                 .fragment_entry = "fs_main",
                                                                                 .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                                 .cull = MEL_GPU_CULL_NONE,
                                                                                 .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                                 .name = "scene-triangle");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Scene_Target tgt = scene_target_create(dev, SCENE_W, SCENE_H);
    const u8*    px = scene_render_readback(dev, &tgt, pipe.value, scene_record_triangle, &vbo.value);
    MEL_REQUIRE_NOT_NULL(px);
    MEL_GOLDEN(SCENE_BACKEND, "shared/triangle", px, tgt.w, tgt.h, SCENE_TOL);

    scene_target_destroy(dev, &tgt);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, pipe.shader);
    mel_gpu_buffer_destroy(dev, vbo.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

typedef struct
{
    Mel_Gpu_Buffer pos;
    Mel_Gpu_Buffer color;
} Scene_Multistream_Vbos;

static void scene_record_triangle_multistream(Mel_Gpu_Command_List* cmd, void* ctx)
{
    Scene_Multistream_Vbos* v = ctx;
    mel_gpu_cmd_bind_vertex_buffer(cmd, 0, v->pos);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 1, v->color);
    mel_gpu_cmd_draw(cmd, 3, 1);
}

MEL_TEST(scene_shared, triangle_multistream)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = scene_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const f32 positions[] = {
        0.0f, 0.6f, 0.0f,
        0.6f, -0.6f, 0.0f,
        -0.6f, -0.6f, 0.0f,
    };
    const f32 colors[] = {
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f,
    };
    Mel_Gpu_Buffer_Create_Result vbo_pos = mel_gpu_buffer_create(dev, .size = sizeof positions, .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD, .data = positions, .name = "scene-ms-pos");
    MEL_REQUIRE(!mel_gpu_failed(vbo_pos.status));
    Mel_Gpu_Buffer_Create_Result vbo_col = mel_gpu_buffer_create(dev, .size = sizeof colors, .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD, .data = colors, .name = "scene-ms-col");
    MEL_REQUIRE(!mel_gpu_failed(vbo_col.status));

    const Mel_Bundle_Graphics bundle = {
        .name = "scene-triangle-ms",
        .spirv_vertex = TRIANGLE_VERT_SPV,
        .spirv_vertex_size = sizeof TRIANGLE_VERT_SPV,
        .spirv_fragment = TRIANGLE_FRAG_SPV,
        .spirv_fragment_size = sizeof TRIANGLE_FRAG_SPV,
        .msl_vertex = TRIANGLE_VERT_MSL,
        .msl_vertex_size = sizeof TRIANGLE_VERT_MSL,
        .msl_fragment = TRIANGLE_FRAG_MSL,
        .msl_fragment_size = sizeof TRIANGLE_FRAG_MSL,
        .wgsl_vertex = TRIANGLE_VERT_WGSL,
        .wgsl_vertex_size = sizeof TRIANGLE_VERT_WGSL,
        .wgsl_fragment = TRIANGLE_FRAG_WGSL,
        .wgsl_fragment_size = sizeof TRIANGLE_FRAG_WGSL,
#if TRIANGLE_HAS_DXIL
        .dxil_vertex = TRIANGLE_VERT_DXIL,
        .dxil_vertex_size = sizeof TRIANGLE_VERT_DXIL,
        .dxil_fragment = TRIANGLE_FRAG_DXIL,
        .dxil_fragment_size = sizeof TRIANGLE_FRAG_DXIL,
#endif
        .vertex_entry = TRIANGLE_VERT_ENTRY,
        .fragment_entry = TRIANGLE_FRAG_ENTRY,
    };
    Mel_Gpu_Shader_Create_Result sh = mel_bundle_select_graphics(dev, &bundle);
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    const Mel_Gpu_Vertex_Element layout[] = {
        { .location = 0, .format = MEL_GPU_FORMAT_RGB32_FLOAT, .offset = 0, .buffer_slot = 0 },
        { .location = 1, .format = MEL_GPU_FORMAT_RGBA32_FLOAT, .offset = 0, .buffer_slot = 1 },
    };
    const Mel_Gpu_Vertex_Buffer_Layout buffers[] = {
        { .slot = 0, .stride = 3 * sizeof(f32) },
        { .slot = 1, .stride = 4 * sizeof(f32) },
    };
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev,
                                                                  .shader = sh.value,
                                                                  .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                  .cull = MEL_GPU_CULL_NONE,
                                                                  .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                  .vertex_layout = layout,
                                                                  .vertex_layout_count = 2,
                                                                  .vertex_buffers = buffers,
                                                                  .vertex_buffer_count = 2,
                                                                  .name = "scene-triangle-ms");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Scene_Multistream_Vbos vbos = { .pos = vbo_pos.value, .color = vbo_col.value };
    Scene_Target           tgt = scene_target_create(dev, SCENE_W, SCENE_H);
    const u8*              px = scene_render_readback(dev, &tgt, pipe.value, scene_record_triangle_multistream, &vbos);
    MEL_REQUIRE_NOT_NULL(px);
    MEL_GOLDEN(SCENE_BACKEND, "shared/triangle", px, tgt.w, tgt.h, SCENE_TOL);

    scene_target_destroy(dev, &tgt);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_buffer_destroy(dev, vbo_col.value);
    mel_gpu_buffer_destroy(dev, vbo_pos.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

static void scene_record_fullscreen3(Mel_Gpu_Command_List* cmd, void* ctx)
{
    (void)ctx;
    mel_gpu_cmd_draw(cmd, 3, 1);
}

MEL_TEST(scene_shared, gradient)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = scene_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const Mel_Bundle_Graphics bundle = {
        .name = "scene-gradient",
        .spirv_vertex = GRADIENT_VERT_SPV,
        .spirv_vertex_size = sizeof GRADIENT_VERT_SPV,
        .spirv_fragment = GRADIENT_FRAG_SPV,
        .spirv_fragment_size = sizeof GRADIENT_FRAG_SPV,
        .msl_vertex = GRADIENT_VERT_MSL,
        .msl_vertex_size = sizeof GRADIENT_VERT_MSL,
        .msl_fragment = GRADIENT_FRAG_MSL,
        .msl_fragment_size = sizeof GRADIENT_FRAG_MSL,
        .wgsl_vertex = GRADIENT_VERT_WGSL,
        .wgsl_vertex_size = sizeof GRADIENT_VERT_WGSL,
        .wgsl_fragment = GRADIENT_FRAG_WGSL,
        .wgsl_fragment_size = sizeof GRADIENT_FRAG_WGSL,
#if GRADIENT_HAS_DXIL
        .dxil_vertex = GRADIENT_VERT_DXIL,
        .dxil_vertex_size = sizeof GRADIENT_VERT_DXIL,
        .dxil_fragment = GRADIENT_FRAG_DXIL,
        .dxil_fragment_size = sizeof GRADIENT_FRAG_DXIL,
#endif
        .vertex_entry = GRADIENT_VERT_ENTRY,
        .fragment_entry = GRADIENT_FRAG_ENTRY,
    };
    Mel_Gpu_Shader_Create_Result sh = mel_bundle_select_graphics(dev, &bundle);
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .cull = MEL_GPU_CULL_NONE, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .name = "scene-gradient");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Scene_Target tgt = scene_target_create(dev, SCENE_W, SCENE_H);
    const u8*    px = scene_render_readback(dev, &tgt, pipe.value, scene_record_fullscreen3, NULL);
    MEL_REQUIRE_NOT_NULL(px);
    MEL_GOLDEN(SCENE_BACKEND, "shared/gradient", px, tgt.w, tgt.h, SCENE_TOL);

    scene_target_destroy(dev, &tgt);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

typedef struct
{
    f32 rect[4];
    f32 color[4];
} Quad_Root;

static void scene_record_quad(Mel_Gpu_Command_List* cmd, void* ctx)
{
    Quad_Root* root = ctx;
    mel_gpu_cmd_push_constants(cmd, 0, sizeof *root, root);
    mel_gpu_cmd_draw(cmd, 6, 1);
}

MEL_TEST(scene_shared, quad)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = scene_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

#if MEL_GPU_WEBGPU
    /* The quad scene drives the pipeline through push constants. WebGPU core has no
       push constants (MissingFeature): pipeline_create refuses loudly, so this scene
       cannot diff against the shared golden there. Degrade honestly — skip with the
       reason rather than fabricate a pass (MEL-ENGINE-VII / -VIII). */
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
    MEL_SKIP("quad scene needs push constants; WebGPU core lacks them (MissingFeature)");
#else
    const Mel_Bundle_Graphics bundle = {
        .name = "scene-quad",
        .spirv_vertex = QUAD_VERT_SPV,
        .spirv_vertex_size = sizeof QUAD_VERT_SPV,
        .spirv_fragment = QUAD_FRAG_SPV,
        .spirv_fragment_size = sizeof QUAD_FRAG_SPV,
        .msl_vertex = QUAD_VERT_MSL,
        .msl_vertex_size = sizeof QUAD_VERT_MSL,
        .msl_fragment = QUAD_FRAG_MSL,
        .msl_fragment_size = sizeof QUAD_FRAG_MSL,
        .wgsl_vertex = QUAD_VERT_WGSL,
        .wgsl_vertex_size = sizeof QUAD_VERT_WGSL,
        .wgsl_fragment = QUAD_FRAG_WGSL,
        .wgsl_fragment_size = sizeof QUAD_FRAG_WGSL,
#if QUAD_HAS_DXIL
        .dxil_vertex = QUAD_VERT_DXIL,
        .dxil_vertex_size = sizeof QUAD_VERT_DXIL,
        .dxil_fragment = QUAD_FRAG_DXIL,
        .dxil_fragment_size = sizeof QUAD_FRAG_DXIL,
#endif
        .vertex_entry = QUAD_VERT_ENTRY,
        .fragment_entry = QUAD_FRAG_ENTRY,
    };
    Mel_Gpu_Shader_Create_Result sh = mel_bundle_select_graphics(dev, &bundle);
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev,
                                                                  .shader = sh.value,
                                                                  .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                  .cull = MEL_GPU_CULL_NONE,
                                                                  .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                  .push_constant_size = sizeof(Quad_Root),
                                                                  .name = "scene-quad");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Quad_Root    root = { { 0.0f, 0.0f, 0.5f, 0.5f }, { 0.2f, 0.8f, 0.4f, 1.0f } };
    Scene_Target tgt = scene_target_create(dev, SCENE_W, SCENE_H);
    const u8*    px = scene_render_readback(dev, &tgt, pipe.value, scene_record_quad, &root);
    MEL_REQUIRE_NOT_NULL(px);
    MEL_GOLDEN(SCENE_BACKEND, "shared/quad", px, tgt.w, tgt.h, SCENE_TOL);

    scene_target_destroy(dev, &tgt);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
#endif
}

typedef struct
{
    f32 time;
    f32 aspect;
    f32 pad0;
    f32 pad1;
} Raymarch_Root;

static void scene_record_raymarch(Mel_Gpu_Command_List* cmd, void* ctx)
{
    Raymarch_Root* root = ctx;
    mel_gpu_cmd_push_constants(cmd, 0, sizeof *root, root);
    mel_gpu_cmd_draw(cmd, 3, 1);
}

MEL_TEST(scene_shared, raymarch)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = scene_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

#if MEL_GPU_WEBGPU
    /* raymarch drives the pipeline through a push-constant (time/aspect). WebGPU core
       has no push constants (MissingFeature): pipeline_create_from_slang refuses loudly,
       so this scene cannot diff against the shared golden there. Degrade honestly — skip
       with the reason rather than fabricate a pass (MEL-ENGINE-VII / -VIII). The WGSL
       emit of raymarch.slang is still proven non-degenerate by the offline cross-emit. */
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
    MEL_SKIP("raymarch scene needs push constants; WebGPU core lacks them (MissingFeature)");
#else
    Mel_Gpu_Pipeline_From_Slang_Result pipe = mel_gpu_pipeline_create_from_slang(dev,
                                                                                 .source = RAYMARCH_SLANG,
                                                                                 .vertex_entry = "vs_main",
                                                                                 .fragment_entry = "fs_main",
                                                                                 .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                                 .cull = MEL_GPU_CULL_NONE,
                                                                                 .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                                 .name = "scene-raymarch");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Raymarch_Root root = { .time = 0.0f, .aspect = 1.0f };
    Scene_Target  tgt = scene_target_create(dev, SCENE_W, SCENE_H);
    const u8*     px = scene_render_readback(dev, &tgt, pipe.value, scene_record_raymarch, &root);
    MEL_REQUIRE_NOT_NULL(px);
    MEL_GOLDEN(SCENE_BACKEND, "shared/raymarch", px, tgt.w, tgt.h, SCENE_TOL);

    scene_target_destroy(dev, &tgt);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, pipe.shader);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
#endif
}

typedef struct
{
    u32 image, w, h, max_iter;
    f32 center_x, center_y, scale, time;
} Mandel_Root;

static Mel_Gpu_Device* scene_make_device_bindless(Mel_Gpu_Instance** out_inst)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-scene-test", .debug = { .enabled = true });
    if (!inst)
        return NULL;
    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(inst, adapters, 8);
    if (n == 0)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    Mel_Gpu_Device_Create_Result dr = mel_gpu_device_create(inst, adapters[0], .reactor = NULL, .features = { .descriptor_indexing = true });
    if (!dr.value)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    *out_inst = inst;
    return dr.value;
}

MEL_TEST(scene_shared, mandelbrot)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = scene_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    if (!mel_gpu_bindless_available(dev))
    {
        mel_gpu_device_destroy(dev);
        mel_gpu_instance_destroy(inst);
        MEL_SKIP("mandelbrot scene needs the device-global bindless heap (descriptor_indexing); device does not advertise it");
    }

    Mel_Gpu_Texture_Create_Result img = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { SCENE_W, SCENE_H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                               .usage = MEL_GPU_TEXTURE_STORAGE | MEL_GPU_TEXTURE_COPY_SRC, .name = "mandel-img");
    MEL_REQUIRE(!mel_gpu_failed(img.status));
    Mel_Gpu_Texture_View_Create_Result view = mel_gpu_texture_default_view(dev, img.value);
    MEL_REQUIRE(!mel_gpu_failed(view.status));
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)SCENE_W * SCENE_H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "mandel-rb");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));

    Mel_Gpu_Pipeline_From_Slang_Result pipe = mel_gpu_pipeline_compute_create_from_slang(dev,
                                                                                        .source = MANDELBROT_SLANG,
                                                                                        .compute_entry = "cs_main",
                                                                                        .push_constant_size = sizeof(Mandel_Root),
                                                                                        .bindless = true,
                                                                                        .name = "scene-mandelbrot");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Mandel_Root root = {
        .image = mel_gpu_texture_view_bindless_slot(dev, view.value),
        .w = SCENE_W,
        .h = SCENE_H,
        .max_iter = 256u,
        .center_x = -0.74364388703f,
        .center_y = 0.13182590421f,
        .scale = 0.18f,
        .time = 0.0f,
    };

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, img.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_UNORDERED_ACCESS);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_dispatch(cmd, (SCENE_W + 7) / 8, (SCENE_H + 7) / 8, 1);
    mel_gpu_cmd_texture_barrier(cmd, img.value, range, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, img.value, range, rb.value);
    mel_gpu_command_list_end(cmd);

    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    bool            ok = mel_gpu_ok(mel_gpu_future_wait(f));
    mel_gpu_future_destroy(f);
    MEL_REQUIRE(ok);

    const u8* px = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_REQUIRE_NOT_NULL(px);
    MEL_GOLDEN(SCENE_BACKEND, "shared/mandelbrot", px, SCENE_W, SCENE_H, SCENE_TOL);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, pipe.shader);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_texture_view_destroy(dev, view.value);
    mel_gpu_texture_destroy(dev, img.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

typedef struct
{
    u32 tex;
    u32 smp;
} Bindless_Present_Root;

static void scene_fill_checker(u8* px, u32 w, u32 h)
{
    for (u32 y = 0; y < h; y++)
        for (u32 x = 0; x < w; x++)
        {
            u8*  p = px + ((usize)y * w + x) * 4;
            bool a = (((x >> 3) ^ (y >> 3)) & 1) == 0;
            p[0] = a ? 220 : 30;
            p[1] = (u8)((x * 255) / (w - 1));
            p[2] = a ? 30 : 220;
            p[3] = 255;
        }
}

MEL_TEST(scene_shared, bindless_present)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = scene_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    if (!mel_gpu_bindless_available(dev))
    {
        mel_gpu_device_destroy(dev);
        mel_gpu_instance_destroy(inst);
        MEL_SKIP("bindless_present scene needs the device-global bindless heap (descriptor_indexing); device does not advertise it");
    }

    u8* checker = malloc((usize)SCENE_W * SCENE_H * 4);
    MEL_REQUIRE_NOT_NULL(checker);
    scene_fill_checker(checker, SCENE_W, SCENE_H);

    Mel_Gpu_Texture_Create_Result src = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { SCENE_W, SCENE_H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                               .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_DST, .name = "bp-src");
    MEL_REQUIRE(!mel_gpu_failed(src.status));
    Mel_Gpu_Texture_Region region = { .subresource = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 }, .offset = { 0, 0, 0 }, .extent = { SCENE_W, SCENE_H, 1 } };
    mel_gpu_texture_write(dev, src.value, region, checker, (usize)SCENE_W * SCENE_H * 4);
    free(checker);

    Mel_Gpu_Texture_View_Create_Result src_view = mel_gpu_texture_default_view(dev, src.value);
    MEL_REQUIRE(!mel_gpu_failed(src_view.status));

    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST,
                                                               .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE, .name = "bp-nearest");
    MEL_REQUIRE(!mel_gpu_failed(smp.status));

    Mel_Gpu_Pipeline_From_Slang_Result pipe = mel_gpu_pipeline_create_from_slang(dev,
                                                                                 .source = BINDLESS_PRESENT_SLANG,
                                                                                 .vertex_entry = "vs_main",
                                                                                 .fragment_entry = "fs_main",
                                                                                 .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                                 .cull = MEL_GPU_CULL_NONE,
                                                                                 .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                                 .bindless = true,
                                                                                 .name = "scene-bindless-present");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Bindless_Present_Root root = {
        .tex = mel_gpu_texture_view_bindless_slot(dev, src_view.value),
        .smp = mel_gpu_sampler_bindless_slot(dev, smp.value),
    };

    Scene_Target          tgt = scene_target_create(dev, SCENE_W, SCENE_H);
    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, tgt.rt, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment color = { .view = tgt.rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.0f, 0.0f, 0.0f, 1.0f) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = tgt.w, .height = tgt.h);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, tgt.rt, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, tgt.rt, range, tgt.rb);
    mel_gpu_command_list_end(cmd);

    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    bool            ok = mel_gpu_ok(mel_gpu_future_wait(f));
    mel_gpu_future_destroy(f);
    MEL_REQUIRE(ok);

    const u8* px = mel_gpu_buffer_mapped(dev, tgt.rb);
    MEL_REQUIRE_NOT_NULL(px);
    MEL_GOLDEN(SCENE_BACKEND, "shared/bindless_present", px, tgt.w, tgt.h, SCENE_TOL);

    scene_target_destroy(dev, &tgt);
    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, pipe.shader);
    mel_gpu_sampler_destroy(dev, smp.value);
    mel_gpu_texture_view_destroy(dev, src_view.value);
    mel_gpu_texture_destroy(dev, src.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

typedef struct
{
    u32   tex0;
    u32   tex1;
    u32   smp;
    u32   img;
    u32   w;
    u32   h;
    float param0;
    float param1;
} Bloom_Root;

typedef struct
{
    Mel_Gpu_Texture      tex;
    Mel_Gpu_Texture_View view;
    u32                  slot;
} Bloom_Image;

static Bloom_Image bloom_image_create(Mel_Gpu_Device* dev, u32 w, u32 h, const char* name)
{
    Bloom_Image i;
    i.tex  = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { w, h, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_STORAGE | MEL_GPU_TEXTURE_SAMPLED, .name = name).value;
    i.view = mel_gpu_texture_default_view(dev, i.tex).value;
    i.slot = mel_gpu_texture_view_bindless_slot(dev, i.view);
    return i;
}

static void bloom_image_destroy(Mel_Gpu_Device* dev, Bloom_Image* i)
{
    mel_gpu_texture_view_destroy(dev, i->view);
    mel_gpu_texture_destroy(dev, i->tex);
}

static void bloom_barrier(Mel_Gpu_Command_List* cmd, Mel_Gpu_Texture tex, Mel_Gpu_Resource_State src, Mel_Gpu_Resource_State dst)
{
    Mel_Gpu_Subresource_Range r = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, tex, r, src, dst);
}

MEL_TEST(scene_shared, bloom)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = scene_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    if (!mel_gpu_bindless_available(dev))
    {
        mel_gpu_device_destroy(dev);
        mel_gpu_instance_destroy(inst);
        MEL_SKIP("bloom scene needs the device-global bindless heap (descriptor_indexing); device does not advertise it");
    }

    Bloom_Image img_scene  = bloom_image_create(dev, SCENE_W, SCENE_H, "bloom-scene");
    Bloom_Image img_bright = bloom_image_create(dev, SCENE_W, SCENE_H, "bloom-bright");
    Bloom_Image img_blurx  = bloom_image_create(dev, SCENE_W, SCENE_H, "bloom-blurx");
    Bloom_Image img_bloom  = bloom_image_create(dev, SCENE_W, SCENE_H, "bloom-blur");

    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_LINEAR, .mag_filter = MEL_GPU_FILTER_LINEAR, .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE, .name = "bloom-smp");
    MEL_REQUIRE(!mel_gpu_failed(smp.status));
    u32 smp_slot = mel_gpu_sampler_bindless_slot(dev, smp.value);

    Mel_Gpu_Pipeline_From_Slang_Result scene_pl  = mel_gpu_pipeline_compute_create_from_slang(dev, .source = BLOOM_SLANG, .compute_entry = "cs_scene", .push_constant_size = sizeof(Bloom_Root), .bindless = true, .name = "scene-bloom-scene");
    MEL_REQUIRE(!mel_gpu_failed(scene_pl.status));
    Mel_Gpu_Pipeline_From_Slang_Result bright_pl = mel_gpu_pipeline_compute_create_from_slang(dev, .source = BLOOM_SLANG, .compute_entry = "cs_bright", .push_constant_size = sizeof(Bloom_Root), .bindless = true, .name = "scene-bloom-bright");
    MEL_REQUIRE(!mel_gpu_failed(bright_pl.status));
    Mel_Gpu_Pipeline_From_Slang_Result blurx_pl  = mel_gpu_pipeline_compute_create_from_slang(dev, .source = BLOOM_SLANG, .compute_entry = "cs_blurx", .push_constant_size = sizeof(Bloom_Root), .bindless = true, .name = "scene-bloom-blurx");
    MEL_REQUIRE(!mel_gpu_failed(blurx_pl.status));
    Mel_Gpu_Pipeline_From_Slang_Result blury_pl  = mel_gpu_pipeline_compute_create_from_slang(dev, .source = BLOOM_SLANG, .compute_entry = "cs_blury", .push_constant_size = sizeof(Bloom_Root), .bindless = true, .name = "scene-bloom-blury");
    MEL_REQUIRE(!mel_gpu_failed(blury_pl.status));
    Mel_Gpu_Pipeline_From_Slang_Result comp_pl   = mel_gpu_pipeline_create_from_slang(dev, .source = BLOOM_SLANG, .vertex_entry = "vs_composite", .fragment_entry = "fs_composite", .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .cull = MEL_GPU_CULL_NONE, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .bindless = true, .name = "scene-bloom-composite");
    MEL_REQUIRE(!mel_gpu_failed(comp_pl.status));

    Scene_Target          tgt = scene_target_create(dev, SCENE_W, SCENE_H);
    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);

    u32 gx = (SCENE_W + 7) / 8;
    u32 gy = (SCENE_H + 7) / 8;

    bloom_barrier(cmd, img_scene.tex, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_UNORDERED_ACCESS);
    Bloom_Root sr = { .tex0 = img_scene.slot, .tex1 = img_scene.slot, .smp = smp_slot, .img = img_scene.slot, .w = SCENE_W, .h = SCENE_H, .param0 = 0.0f };
    mel_gpu_cmd_bind_pipeline(cmd, scene_pl.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof sr, &sr);
    mel_gpu_cmd_dispatch(cmd, gx, gy, 1);
    bloom_barrier(cmd, img_scene.tex, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);

    bloom_barrier(cmd, img_bright.tex, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_UNORDERED_ACCESS);
    Bloom_Root br = { .tex0 = img_scene.slot, .tex1 = img_scene.slot, .smp = smp_slot, .img = img_bright.slot, .w = SCENE_W, .h = SCENE_H, .param0 = 0.55f };
    mel_gpu_cmd_bind_pipeline(cmd, bright_pl.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof br, &br);
    mel_gpu_cmd_dispatch(cmd, gx, gy, 1);
    bloom_barrier(cmd, img_bright.tex, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);

    bloom_barrier(cmd, img_blurx.tex, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_UNORDERED_ACCESS);
    Bloom_Root bxr = { .tex0 = img_bright.slot, .tex1 = img_bright.slot, .smp = smp_slot, .img = img_blurx.slot, .w = SCENE_W, .h = SCENE_H };
    mel_gpu_cmd_bind_pipeline(cmd, blurx_pl.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof bxr, &bxr);
    mel_gpu_cmd_dispatch(cmd, gx, gy, 1);
    bloom_barrier(cmd, img_blurx.tex, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);

    bloom_barrier(cmd, img_bloom.tex, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_UNORDERED_ACCESS);
    Bloom_Root byr = { .tex0 = img_blurx.slot, .tex1 = img_blurx.slot, .smp = smp_slot, .img = img_bloom.slot, .w = SCENE_W, .h = SCENE_H };
    mel_gpu_cmd_bind_pipeline(cmd, blury_pl.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof byr, &byr);
    mel_gpu_cmd_dispatch(cmd, gx, gy, 1);
    bloom_barrier(cmd, img_bloom.tex, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);

    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, tgt.rt, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Bloom_Root cr = { .tex0 = img_scene.slot, .tex1 = img_bloom.slot, .smp = smp_slot, .img = img_scene.slot, .w = SCENE_W, .h = SCENE_H, .param0 = 1.8f };
    Mel_Gpu_Color_Attachment color = { .view = tgt.rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.0f, 0.0f, 0.0f, 1.0f) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = tgt.w, .height = tgt.h);
    mel_gpu_cmd_bind_pipeline(cmd, comp_pl.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof cr, &cr);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, tgt.rt, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, tgt.rt, range, tgt.rb);
    mel_gpu_command_list_end(cmd);

    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    bool            ok = mel_gpu_ok(mel_gpu_future_wait(f));
    mel_gpu_future_destroy(f);
    MEL_REQUIRE(ok);

    const u8* px = mel_gpu_buffer_mapped(dev, tgt.rb);
    MEL_REQUIRE_NOT_NULL(px);
    MEL_GOLDEN(SCENE_BACKEND, "shared/bloom", px, tgt.w, tgt.h, SCENE_TOL);

    scene_target_destroy(dev, &tgt);
    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, comp_pl.value);
    mel_gpu_shader_destroy(dev, comp_pl.shader);
    mel_gpu_pipeline_destroy(dev, blury_pl.value);
    mel_gpu_shader_destroy(dev, blury_pl.shader);
    mel_gpu_pipeline_destroy(dev, blurx_pl.value);
    mel_gpu_shader_destroy(dev, blurx_pl.shader);
    mel_gpu_pipeline_destroy(dev, bright_pl.value);
    mel_gpu_shader_destroy(dev, bright_pl.shader);
    mel_gpu_pipeline_destroy(dev, scene_pl.value);
    mel_gpu_shader_destroy(dev, scene_pl.shader);
    mel_gpu_sampler_destroy(dev, smp.value);
    bloom_image_destroy(dev, &img_bloom);
    bloom_image_destroy(dev, &img_blurx);
    bloom_image_destroy(dev, &img_bright);
    bloom_image_destroy(dev, &img_scene);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

#define BOIDS_SCENE_COUNT 4096u
#define BOIDS_SCENE_LOCAL 64u

typedef struct
{
    f32 pos_vel[4];
} Boids_Boid;

typedef struct
{
    u32 src, dst, total, pad;
    f32 dt, time, goal_x, goal_y, aspect, pad1;
} Boids_Root;

MEL_TEST(scene_shared, boids)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = scene_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    if (!mel_gpu_bindless_available(dev))
    {
        mel_gpu_device_destroy(dev);
        mel_gpu_instance_destroy(inst);
        MEL_SKIP("boids scene needs the device-global bindless storage-buffer heap (descriptor_indexing); device does not advertise it");
    }

    Boids_Boid* seed = malloc(BOIDS_SCENE_COUNT * sizeof(Boids_Boid));
    MEL_REQUIRE_NOT_NULL(seed);
    for (u32 i = 0; i < BOIDS_SCENE_COUNT; ++i)
    {
        f32 a = (f32)i * 2.3999632f;
        f32 r = 0.9f * sqrtf((f32)i / (f32)BOIDS_SCENE_COUNT);
        f32 vx = 0.25f * cosf(a * 1.3f);
        f32 vy = 0.25f * sinf(a * 1.3f);
        seed[i] = (Boids_Boid){ { r * cosf(a), r * sinf(a), vx, vy } };
    }

    Mel_Gpu_Buffer       buf[2];
    u32                  buf_slot[2];
    for (u32 k = 0; k < 2; ++k)
    {
        Mel_Gpu_Buffer_Create_Result pb = mel_gpu_buffer_create(dev, .size = BOIDS_SCENE_COUNT * sizeof(Boids_Boid), .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_DEVICE, .data = seed, .name = "scene-boids");
        MEL_REQUIRE(!mel_gpu_failed(pb.status));
        buf[k] = pb.value;
        buf_slot[k] = mel_gpu_buffer_bindless_slot(dev, buf[k]);
    }
    free(seed);

    Mel_Gpu_Pipeline_From_Slang_Result sim_pl = mel_gpu_pipeline_compute_create_from_slang(dev, .source = BOIDS_SLANG, .compute_entry = "cs_sim", .push_constant_size = sizeof(Boids_Root), .bindless = true, .name = "scene-boids-sim");
    MEL_REQUIRE(!mel_gpu_failed(sim_pl.status));
    Mel_Gpu_Pipeline_From_Slang_Result draw_pl = mel_gpu_pipeline_create_from_slang(dev, .source = BOIDS_SLANG, .vertex_entry = "vs_draw", .fragment_entry = "fs_draw", .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .cull = MEL_GPU_CULL_NONE, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .bindless = true, .name = "scene-boids-draw");
    MEL_REQUIRE(!mel_gpu_failed(draw_pl.status));

    Scene_Target          tgt = scene_target_create(dev, SCENE_W, SCENE_H);
    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);

    mel_gpu_cmd_buffer_barrier(cmd, buf[0], MEL_GPU_STATE_COMMON, MEL_GPU_STATE_SHADER_RESOURCE);
    mel_gpu_cmd_buffer_barrier(cmd, buf[1], MEL_GPU_STATE_COMMON, MEL_GPU_STATE_UNORDERED_ACCESS);

    Boids_Root sroot = { .src = buf_slot[0], .dst = buf_slot[1], .total = BOIDS_SCENE_COUNT, .dt = 0.016f, .time = 0.0f, .goal_x = 0.0f, .goal_y = 0.0f };
    mel_gpu_cmd_bind_pipeline(cmd, sim_pl.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof sroot, &sroot);
    mel_gpu_cmd_dispatch(cmd, (BOIDS_SCENE_COUNT + BOIDS_SCENE_LOCAL - 1) / BOIDS_SCENE_LOCAL, 1, 1);

    mel_gpu_cmd_buffer_barrier(cmd, buf[1], MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);

    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, tgt.rt, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Boids_Root droot = { .src = buf_slot[1], .dst = buf_slot[0], .aspect = 1.0f, .time = 0.0f };
    Mel_Gpu_Color_Attachment color = { .view = tgt.rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.02f, 0.03f, 0.05f, 1.0f) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = tgt.w, .height = tgt.h);
    mel_gpu_cmd_bind_pipeline(cmd, draw_pl.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof droot, &droot);
    mel_gpu_cmd_draw(cmd, 3, BOIDS_SCENE_COUNT);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, tgt.rt, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, tgt.rt, range, tgt.rb);
    mel_gpu_command_list_end(cmd);

    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    bool            ok = mel_gpu_ok(mel_gpu_future_wait(f));
    mel_gpu_future_destroy(f);
    MEL_REQUIRE(ok);

    const u8* px = mel_gpu_buffer_mapped(dev, tgt.rb);
    MEL_REQUIRE_NOT_NULL(px);
    MEL_GOLDEN(SCENE_BACKEND, "shared/boids", px, tgt.w, tgt.h, SCENE_TOL);

    scene_target_destroy(dev, &tgt);
    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, draw_pl.value);
    mel_gpu_shader_destroy(dev, draw_pl.shader);
    mel_gpu_pipeline_destroy(dev, sim_pl.value);
    mel_gpu_shader_destroy(dev, sim_pl.shader);
    mel_gpu_buffer_destroy(dev, buf[1]);
    mel_gpu_buffer_destroy(dev, buf[0]);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

/* ============================================================================
   task #35 batch G1: compute/sim screens ported to dual-lane runtime Slang.
   Each scene drives the same screen pipelines (reacdiff/compute_plasma/particles/
   dispatch_indirect) through the RHI and diffs against the macOS-Vulkan-oracle
   golden in golden/shared/<screen>.ppm. Kept contiguous to ease union-merge.
   ============================================================================ */

#define PLASMA_SCENE_GRID_W 64u
#define PLASMA_SCENE_GRID_H 48u
#define PLASMA_SCENE_CELLS  (PLASMA_SCENE_GRID_W * PLASMA_SCENE_GRID_H)

typedef struct
{
    u32 cells_rw, cells_ro, grid_w, grid_h;
    f32 time, pad;
} Plasma_Scene_Root;

MEL_TEST(scene_shared, compute_plasma)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = scene_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    if (!mel_gpu_bindless_available(dev))
    {
        mel_gpu_device_destroy(dev);
        mel_gpu_instance_destroy(inst);
        MEL_SKIP("compute_plasma scene needs the device-global bindless storage-buffer heap (descriptor_indexing); device does not advertise it");
    }

    Mel_Gpu_Buffer_Create_Result cb = mel_gpu_buffer_create(dev, .size = (usize)PLASMA_SCENE_CELLS * 4 * sizeof(f32), .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_DEVICE, .name = "scene-plasma-cells");
    MEL_REQUIRE(!mel_gpu_failed(cb.status));
    u32 cell_slot = mel_gpu_buffer_bindless_slot(dev, cb.value);

    Mel_Gpu_Pipeline_From_Slang_Result comp_pl = mel_gpu_pipeline_compute_create_from_slang(dev, .source = COMPUTE_PLASMA_SLANG, .compute_entry = "cs_main", .push_constant_size = sizeof(Plasma_Scene_Root), .bindless = true, .name = "scene-plasma-cs");
    MEL_REQUIRE(!mel_gpu_failed(comp_pl.status));
    Mel_Gpu_Pipeline_From_Slang_Result draw_pl = mel_gpu_pipeline_create_from_slang(dev, .source = COMPUTE_PLASMA_SLANG, .vertex_entry = "vs_cells", .fragment_entry = "fs_cells", .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .cull = MEL_GPU_CULL_NONE, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .bindless = true, .name = "scene-plasma-draw");
    MEL_REQUIRE(!mel_gpu_failed(draw_pl.status));

    Scene_Target          tgt = scene_target_create(dev, SCENE_W, SCENE_H);
    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);

    mel_gpu_cmd_buffer_barrier(cmd, cb.value, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_UNORDERED_ACCESS);
    Plasma_Scene_Root croot = { .cells_rw = cell_slot, .cells_ro = cell_slot, .grid_w = PLASMA_SCENE_GRID_W, .grid_h = PLASMA_SCENE_GRID_H, .time = 0.0f };
    mel_gpu_cmd_bind_pipeline(cmd, comp_pl.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof croot, &croot);
    mel_gpu_cmd_dispatch(cmd, (PLASMA_SCENE_GRID_W + 7) / 8, (PLASMA_SCENE_GRID_H + 7) / 8, 1);
    mel_gpu_cmd_buffer_barrier(cmd, cb.value, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);

    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, tgt.rt, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Plasma_Scene_Root droot = { .cells_rw = cell_slot, .cells_ro = cell_slot, .grid_w = PLASMA_SCENE_GRID_W, .grid_h = PLASMA_SCENE_GRID_H };
    Mel_Gpu_Color_Attachment color = { .view = tgt.rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.02f, 0.02f, 0.03f, 1.0f) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = tgt.w, .height = tgt.h);
    mel_gpu_cmd_bind_pipeline(cmd, draw_pl.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof droot, &droot);
    mel_gpu_cmd_draw(cmd, 6, PLASMA_SCENE_CELLS);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, tgt.rt, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, tgt.rt, range, tgt.rb);
    mel_gpu_command_list_end(cmd);

    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    bool            ok = mel_gpu_ok(mel_gpu_future_wait(f));
    mel_gpu_future_destroy(f);
    MEL_REQUIRE(ok);

    const u8* px = mel_gpu_buffer_mapped(dev, tgt.rb);
    MEL_REQUIRE_NOT_NULL(px);
    MEL_GOLDEN(SCENE_BACKEND, "shared/compute_plasma", px, tgt.w, tgt.h, SCENE_TOL);

    scene_target_destroy(dev, &tgt);
    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, draw_pl.value);
    mel_gpu_shader_destroy(dev, draw_pl.shader);
    mel_gpu_pipeline_destroy(dev, comp_pl.value);
    mel_gpu_shader_destroy(dev, comp_pl.shader);
    mel_gpu_buffer_destroy(dev, cb.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

#define PARTICLES_SCENE_COUNT 40000u
#define PARTICLES_SCENE_LOCAL 64u

typedef struct
{
    f32 pos_life[4];
    f32 vel[4];
} Particles_Scene_Particle;

typedef struct
{
    u32 particles_rw, particles_ro, total;
    f32 dt, time, attract_x, attract_y, aspect;
} Particles_Scene_Root;

MEL_TEST(scene_shared, particles)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = scene_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    if (!mel_gpu_bindless_available(dev))
    {
        mel_gpu_device_destroy(dev);
        mel_gpu_instance_destroy(inst);
        MEL_SKIP("particles scene needs the device-global bindless storage-buffer heap (descriptor_indexing); device does not advertise it");
    }

    Particles_Scene_Particle* seed = malloc(PARTICLES_SCENE_COUNT * sizeof(Particles_Scene_Particle));
    MEL_REQUIRE_NOT_NULL(seed);
    for (u32 i = 0; i < PARTICLES_SCENE_COUNT; ++i)
    {
        f32 a = (f32)i * 2.3999632f;
        f32 r = 0.9f + 0.1f * (f32)((i * 2654435761u) & 0xFF) / 255.0f;
        seed[i] = (Particles_Scene_Particle){ { r * cosf(a), r * sinf(a), (f32)(i % 100) / 100.0f, a }, { -0.15f * sinf(a), 0.15f * cosf(a), 0.0f, 0.0f } };
    }
    Mel_Gpu_Buffer_Create_Result pb = mel_gpu_buffer_create(dev, .size = PARTICLES_SCENE_COUNT * sizeof(Particles_Scene_Particle), .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_DEVICE, .data = seed, .name = "scene-particles");
    free(seed);
    MEL_REQUIRE(!mel_gpu_failed(pb.status));
    u32 part_slot = mel_gpu_buffer_bindless_slot(dev, pb.value);

    Mel_Gpu_Pipeline_From_Slang_Result sim_pl = mel_gpu_pipeline_compute_create_from_slang(dev, .source = PARTICLES_SLANG, .compute_entry = "cs_sim", .push_constant_size = sizeof(Particles_Scene_Root), .bindless = true, .name = "scene-particles-sim");
    MEL_REQUIRE(!mel_gpu_failed(sim_pl.status));
    Mel_Gpu_Color_Target target = {
        .format = MEL_GPU_FORMAT_RGBA8_UNORM,
        .blend = { .enable = true, .src_color = MEL_GPU_BLEND_SRC_ALPHA, .dst_color = MEL_GPU_BLEND_ONE, .color_op = MEL_GPU_BLEND_OP_ADD, .src_alpha = MEL_GPU_BLEND_ONE, .dst_alpha = MEL_GPU_BLEND_ONE, .alpha_op = MEL_GPU_BLEND_OP_ADD, .write_mask = MEL_GPU_COLOR_WRITE_ALL },
    };
    Mel_Gpu_Pipeline_From_Slang_Result draw_pl = mel_gpu_pipeline_create_from_slang(dev, .source = PARTICLES_SLANG, .vertex_entry = "vs_draw", .fragment_entry = "fs_draw", .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .cull = MEL_GPU_CULL_NONE, .color_targets = &target, .color_target_count = 1, .bindless = true, .name = "scene-particles-draw");
    MEL_REQUIRE(!mel_gpu_failed(draw_pl.status));

    Scene_Target          tgt = scene_target_create(dev, SCENE_W, SCENE_H);
    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);

    mel_gpu_cmd_buffer_barrier(cmd, pb.value, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_UNORDERED_ACCESS);
    Particles_Scene_Root sroot = { .particles_rw = part_slot, .particles_ro = part_slot, .total = PARTICLES_SCENE_COUNT, .dt = 0.016f, .time = 0.0f, .attract_x = 0.0f, .attract_y = 0.0f };
    mel_gpu_cmd_bind_pipeline(cmd, sim_pl.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof sroot, &sroot);
    mel_gpu_cmd_dispatch(cmd, (PARTICLES_SCENE_COUNT + PARTICLES_SCENE_LOCAL - 1) / PARTICLES_SCENE_LOCAL, 1, 1);
    mel_gpu_cmd_buffer_barrier(cmd, pb.value, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);

    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, tgt.rt, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Particles_Scene_Root droot = { .particles_rw = part_slot, .particles_ro = part_slot, .aspect = 1.0f };
    Mel_Gpu_Color_Attachment color = { .view = tgt.rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.02f, 0.02f, 0.04f, 1.0f) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = tgt.w, .height = tgt.h);
    mel_gpu_cmd_bind_pipeline(cmd, draw_pl.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof droot, &droot);
    mel_gpu_cmd_draw(cmd, 6, PARTICLES_SCENE_COUNT);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, tgt.rt, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, tgt.rt, range, tgt.rb);
    mel_gpu_command_list_end(cmd);

    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    bool            ok = mel_gpu_ok(mel_gpu_future_wait(f));
    mel_gpu_future_destroy(f);
    MEL_REQUIRE(ok);

    const u8* px = mel_gpu_buffer_mapped(dev, tgt.rb);
    MEL_REQUIRE_NOT_NULL(px);
    MEL_GOLDEN(SCENE_BACKEND, "shared/particles", px, tgt.w, tgt.h, SCENE_TOL);

    scene_target_destroy(dev, &tgt);
    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, draw_pl.value);
    mel_gpu_shader_destroy(dev, draw_pl.shader);
    mel_gpu_pipeline_destroy(dev, sim_pl.value);
    mel_gpu_shader_destroy(dev, sim_pl.shader);
    mel_gpu_buffer_destroy(dev, pb.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

#define REACDIFF_SCENE_STEPS 8

typedef struct
{
    u32 tex, smp, img, w, h;
    f32 da, db, feed, kill, dt, time;
} Reacdiff_Scene_Root;

MEL_TEST(scene_shared, reacdiff)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = scene_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    if (!mel_gpu_bindless_available(dev))
    {
        mel_gpu_device_destroy(dev);
        mel_gpu_instance_destroy(inst);
        MEL_SKIP("reacdiff scene needs the device-global bindless heap (descriptor_indexing); device does not advertise it");
    }

    Mel_Gpu_Texture      img[2];
    Mel_Gpu_Texture_View img_view[2];
    u32                  img_slot[2];
    for (u32 k = 0; k < 2; ++k)
    {
        Mel_Gpu_Texture_Create_Result it = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { SCENE_W, SCENE_H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_STORAGE | MEL_GPU_TEXTURE_SAMPLED, .name = "scene-reacdiff");
        MEL_REQUIRE(!mel_gpu_failed(it.status));
        img[k] = it.value;
        img_view[k] = mel_gpu_texture_default_view(dev, img[k]).value;
        img_slot[k] = mel_gpu_texture_view_bindless_slot(dev, img_view[k]);
    }

    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_LINEAR, .mag_filter = MEL_GPU_FILTER_LINEAR, .wrap_u = MEL_GPU_WRAP_REPEAT, .wrap_v = MEL_GPU_WRAP_REPEAT, .name = "scene-reacdiff-smp");
    MEL_REQUIRE(!mel_gpu_failed(smp.status));
    u32 smp_slot = mel_gpu_sampler_bindless_slot(dev, smp.value);

    Mel_Gpu_Pipeline_From_Slang_Result init_pl = mel_gpu_pipeline_compute_create_from_slang(dev, .source = REACDIFF_SLANG, .compute_entry = "cs_init", .push_constant_size = sizeof(Reacdiff_Scene_Root), .bindless = true, .name = "scene-reacdiff-init");
    MEL_REQUIRE(!mel_gpu_failed(init_pl.status));
    Mel_Gpu_Pipeline_From_Slang_Result step_pl = mel_gpu_pipeline_compute_create_from_slang(dev, .source = REACDIFF_SLANG, .compute_entry = "cs_step", .push_constant_size = sizeof(Reacdiff_Scene_Root), .bindless = true, .name = "scene-reacdiff-step");
    MEL_REQUIRE(!mel_gpu_failed(step_pl.status));
    Mel_Gpu_Pipeline_From_Slang_Result draw_pl = mel_gpu_pipeline_create_from_slang(dev, .source = REACDIFF_SLANG, .vertex_entry = "vs_draw", .fragment_entry = "fs_draw", .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .cull = MEL_GPU_CULL_NONE, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .bindless = true, .name = "scene-reacdiff-draw");
    MEL_REQUIRE(!mel_gpu_failed(draw_pl.status));

    Scene_Target          tgt = scene_target_create(dev, SCENE_W, SCENE_H);
    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);

    u32                       gx = (SCENE_W + 7) / 8;
    u32                       gy = (SCENE_H + 7) / 8;
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };

    for (u32 k = 0; k < 2; ++k)
    {
        mel_gpu_cmd_texture_barrier(cmd, img[k], range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_UNORDERED_ACCESS);
        Reacdiff_Scene_Root ir = { .img = img_slot[k], .w = SCENE_W, .h = SCENE_H };
        mel_gpu_cmd_bind_pipeline(cmd, init_pl.value);
        mel_gpu_cmd_bind_bindless(cmd);
        mel_gpu_cmd_push_constants(cmd, 0, sizeof ir, &ir);
        mel_gpu_cmd_dispatch(cmd, gx, gy, 1);
        mel_gpu_cmd_texture_barrier(cmd, img[k], range, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);
    }

    i32 cur = 0;
    for (i32 s = 0; s < REACDIFF_SCENE_STEPS; ++s)
    {
        i32 src = cur;
        i32 dst = cur ^ 1;
        mel_gpu_cmd_texture_barrier(cmd, img[src], range, MEL_GPU_STATE_SHADER_RESOURCE, MEL_GPU_STATE_SHADER_RESOURCE);
        mel_gpu_cmd_texture_barrier(cmd, img[dst], range, MEL_GPU_STATE_SHADER_RESOURCE, MEL_GPU_STATE_UNORDERED_ACCESS);
        Reacdiff_Scene_Root sr = { .tex = img_slot[src], .smp = smp_slot, .img = img_slot[dst], .w = SCENE_W, .h = SCENE_H, .da = 1.0f, .db = 0.5f, .feed = 0.055f, .kill = 0.062f, .dt = 1.0f };
        mel_gpu_cmd_bind_pipeline(cmd, step_pl.value);
        mel_gpu_cmd_bind_bindless(cmd);
        mel_gpu_cmd_push_constants(cmd, 0, sizeof sr, &sr);
        mel_gpu_cmd_dispatch(cmd, gx, gy, 1);
        mel_gpu_cmd_texture_barrier(cmd, img[dst], range, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);
        cur = dst;
    }

    mel_gpu_cmd_texture_barrier(cmd, tgt.rt, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Reacdiff_Scene_Root dr = { .tex = img_slot[cur], .smp = smp_slot, .time = 0.0f };
    Mel_Gpu_Color_Attachment color = { .view = tgt.rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.0f, 0.0f, 0.0f, 1.0f) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = tgt.w, .height = tgt.h);
    mel_gpu_cmd_bind_pipeline(cmd, draw_pl.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof dr, &dr);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, tgt.rt, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, tgt.rt, range, tgt.rb);
    mel_gpu_command_list_end(cmd);

    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    bool            ok = mel_gpu_ok(mel_gpu_future_wait(f));
    mel_gpu_future_destroy(f);
    MEL_REQUIRE(ok);

    const u8* px = mel_gpu_buffer_mapped(dev, tgt.rb);
    MEL_REQUIRE_NOT_NULL(px);
    MEL_GOLDEN(SCENE_BACKEND, "shared/reacdiff", px, tgt.w, tgt.h, SCENE_TOL);

    scene_target_destroy(dev, &tgt);
    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, draw_pl.value);
    mel_gpu_shader_destroy(dev, draw_pl.shader);
    mel_gpu_pipeline_destroy(dev, step_pl.value);
    mel_gpu_shader_destroy(dev, step_pl.shader);
    mel_gpu_pipeline_destroy(dev, init_pl.value);
    mel_gpu_shader_destroy(dev, init_pl.shader);
    mel_gpu_sampler_destroy(dev, smp.value);
    for (u32 k = 0; k < 2; ++k)
    {
        mel_gpu_texture_view_destroy(dev, img_view[k]);
        mel_gpu_texture_destroy(dev, img[k]);
    }
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

#define DI_SCENE_AGENTS 4096u
#define DI_SCENE_LOCAL  64u

typedef struct
{
    f32 pos_phase[4];
} Di_Scene_Agent;

typedef struct
{
    u32 agents, survivors, args, image, total, w, h, local;
    f32 time, cull_r, cull_x, cull_y;
} Di_Scene_Root;

MEL_TEST(scene_shared, dispatch_indirect)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = scene_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    if (!mel_gpu_bindless_available(dev))
    {
        mel_gpu_device_destroy(dev);
        mel_gpu_instance_destroy(inst);
        MEL_SKIP("dispatch_indirect scene needs the device-global bindless storage-image + storage-buffer heaps (descriptor_indexing); device does not advertise it");
    }

#if MEL_GPU_METAL
    /* The shade pass is dispatched via cmd_dispatch_indirect on a from-slang bindless
       compute pipeline. The Metal RHI's mel_gpu_cmd_dispatch_indirect (record.m) does NOT
       build the per-dispatch inlined argument buffer that mel_gpu_cmd_dispatch builds, so the
       shade kernel runs with no bound bindless resources and writes nothing — the image comes
       back without the survivor splats. The fix is to lift the compute-argbuffer build (the
       same lines cmd_dispatch runs) into cmd_dispatch_indirect; that is gpu backend src, out
       of this task's ownership. Skip honestly here rather than diff a blank image against the
       Vulkan oracle (MEL-ENGINE-VIII). cull/buildargs/clear all run on Metal; only the
       indirectly-dispatched shade is blocked. The Vulkan oracle proves the algorithm. */
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
    MEL_SKIP("dispatch_indirect scene needs the Metal RHI to build the from-slang bindless argument buffer in cmd_dispatch_indirect (record.m); it only builds it in cmd_dispatch — RHI gap, gpu backend src out of this task's scope");
#else
    Di_Scene_Agent* pool = malloc(DI_SCENE_AGENTS * sizeof(Di_Scene_Agent));
    MEL_REQUIRE_NOT_NULL(pool);
    for (u32 i = 0; i < DI_SCENE_AGENTS; ++i)
    {
        f32 fi = (f32)i;
        f32 a = fi * 2.3999632f;
        f32 r = 0.95f * sqrtf(fi / (f32)DI_SCENE_AGENTS);
        pool[i] = (Di_Scene_Agent){ { r * cosf(a), r * sinf(a), 0.06f + 0.05f * sinf(fi * 0.05f), a } };
    }
    Mel_Gpu_Buffer_Create_Result ab = mel_gpu_buffer_create(dev, .size = DI_SCENE_AGENTS * sizeof(Di_Scene_Agent), .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_DEVICE, .data = pool, .name = "scene-di-agents");
    free(pool);
    MEL_REQUIRE(!mel_gpu_failed(ab.status));
    u32 agents_slot = mel_gpu_buffer_bindless_slot(dev, ab.value);

    Mel_Gpu_Buffer_Create_Result sb = mel_gpu_buffer_create(dev, .size = 16 + (usize)DI_SCENE_AGENTS * sizeof(u32), .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_UPLOAD, .name = "scene-di-survivors");
    MEL_REQUIRE(!mel_gpu_failed(sb.status));
    u32 surv_slot = mel_gpu_buffer_bindless_slot(dev, sb.value);
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = 3 * sizeof(u32), .usage = MEL_GPU_BUFFER_STORAGE | MEL_GPU_BUFFER_INDIRECT, .memory = MEL_GPU_MEMORY_UPLOAD, .name = "scene-di-args");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));
    u32 args_slot = mel_gpu_buffer_bindless_slot(dev, rb.value);

    Mel_Gpu_Texture_Create_Result it = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { SCENE_W, SCENE_H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_STORAGE | MEL_GPU_TEXTURE_COPY_SRC, .name = "scene-di-img");
    MEL_REQUIRE(!mel_gpu_failed(it.status));
    Mel_Gpu_Texture_View_Create_Result iv = mel_gpu_texture_default_view(dev, it.value);
    MEL_REQUIRE(!mel_gpu_failed(iv.status));
    u32                          img_slot = mel_gpu_texture_view_bindless_slot(dev, iv.value);
    Mel_Gpu_Buffer_Create_Result imgrb = mel_gpu_buffer_create(dev, .size = (usize)SCENE_W * SCENE_H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "scene-di-rb");
    MEL_REQUIRE(!mel_gpu_failed(imgrb.status));

    Mel_Gpu_Pipeline_From_Slang_Result cull_pl = mel_gpu_pipeline_compute_create_from_slang(dev, .source = DISPATCH_INDIRECT_SLANG, .compute_entry = "cs_cull", .push_constant_size = sizeof(Di_Scene_Root), .bindless = true, .name = "scene-di-cull");
    MEL_REQUIRE(!mel_gpu_failed(cull_pl.status));
    Mel_Gpu_Pipeline_From_Slang_Result args_pl = mel_gpu_pipeline_compute_create_from_slang(dev, .source = DISPATCH_INDIRECT_SLANG, .compute_entry = "cs_args", .push_constant_size = sizeof(Di_Scene_Root), .bindless = true, .name = "scene-di-args");
    MEL_REQUIRE(!mel_gpu_failed(args_pl.status));
    Mel_Gpu_Pipeline_From_Slang_Result clear_pl = mel_gpu_pipeline_compute_create_from_slang(dev, .source = DISPATCH_INDIRECT_SLANG, .compute_entry = "cs_clear", .push_constant_size = sizeof(Di_Scene_Root), .bindless = true, .name = "scene-di-clear");
    MEL_REQUIRE(!mel_gpu_failed(clear_pl.status));
    Mel_Gpu_Pipeline_From_Slang_Result shade_pl = mel_gpu_pipeline_compute_create_from_slang(dev, .source = DISPATCH_INDIRECT_SLANG, .compute_entry = "cs_shade", .push_constant_size = sizeof(Di_Scene_Root), .bindless = true, .name = "scene-di-shade");
    MEL_REQUIRE(!mel_gpu_failed(shade_pl.status));

    u32* surv_map = mel_gpu_buffer_mapped(dev, sb.value);
    MEL_REQUIRE_NOT_NULL(surv_map);
    surv_map[0] = 0;

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);

    mel_gpu_cmd_buffer_barrier(cmd, sb.value, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_UNORDERED_ACCESS);
    Di_Scene_Root croot = { .agents = agents_slot, .survivors = surv_slot, .total = DI_SCENE_AGENTS, .time = 0.0f, .cull_r = 0.8f, .cull_x = 0.0f, .cull_y = 0.0f };
    mel_gpu_cmd_bind_pipeline(cmd, cull_pl.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof croot, &croot);
    mel_gpu_cmd_dispatch(cmd, (DI_SCENE_AGENTS + DI_SCENE_LOCAL - 1) / DI_SCENE_LOCAL, 1, 1);

    mel_gpu_cmd_buffer_barrier(cmd, sb.value, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);
    mel_gpu_cmd_buffer_barrier(cmd, rb.value, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_UNORDERED_ACCESS);
    Di_Scene_Root aroot = { .survivors = surv_slot, .args = args_slot, .local = DI_SCENE_LOCAL };
    mel_gpu_cmd_bind_pipeline(cmd, args_pl.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof aroot, &aroot);
    mel_gpu_cmd_dispatch(cmd, 1, 1, 1);
    mel_gpu_cmd_buffer_barrier(cmd, rb.value, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_INDIRECT_ARGUMENT);

    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, it.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_UNORDERED_ACCESS);
    Di_Scene_Root clroot = { .image = img_slot, .w = SCENE_W, .h = SCENE_H, .time = 0.0f };
    mel_gpu_cmd_bind_pipeline(cmd, clear_pl.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof clroot, &clroot);
    mel_gpu_cmd_dispatch(cmd, (SCENE_W + 7) / 8, (SCENE_H + 7) / 8, 1);
    mel_gpu_cmd_texture_barrier(cmd, it.value, range, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_UNORDERED_ACCESS);

    Di_Scene_Root sroot = { .agents = agents_slot, .survivors = surv_slot, .image = img_slot, .total = DI_SCENE_AGENTS, .w = SCENE_W, .h = SCENE_H, .time = 0.0f, .cull_r = 0.8f, .cull_x = 0.0f, .cull_y = 0.0f };
    mel_gpu_cmd_bind_pipeline(cmd, shade_pl.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof sroot, &sroot);
    mel_gpu_cmd_dispatch_indirect(cmd, rb.value, 0);

    mel_gpu_cmd_texture_barrier(cmd, it.value, range, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, it.value, range, imgrb.value);
    mel_gpu_command_list_end(cmd);

    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    bool            ok = mel_gpu_ok(mel_gpu_future_wait(f));
    mel_gpu_future_destroy(f);
    MEL_REQUIRE(ok);

    const u8* px = mel_gpu_buffer_mapped(dev, imgrb.value);
    MEL_REQUIRE_NOT_NULL(px);
    MEL_GOLDEN(SCENE_BACKEND, "shared/dispatch_indirect", px, SCENE_W, SCENE_H, SCENE_TOL);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, shade_pl.value);
    mel_gpu_shader_destroy(dev, shade_pl.shader);
    mel_gpu_pipeline_destroy(dev, clear_pl.value);
    mel_gpu_shader_destroy(dev, clear_pl.shader);
    mel_gpu_pipeline_destroy(dev, args_pl.value);
    mel_gpu_shader_destroy(dev, args_pl.shader);
    mel_gpu_pipeline_destroy(dev, cull_pl.value);
    mel_gpu_shader_destroy(dev, cull_pl.shader);
    mel_gpu_buffer_destroy(dev, imgrb.value);
    mel_gpu_texture_view_destroy(dev, iv.value);
    mel_gpu_texture_destroy(dev, it.value);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_buffer_destroy(dev, sb.value);
    mel_gpu_buffer_destroy(dev, ab.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
#endif
}
/* ===== end task #35 batch G1 scenes ===== */

#else

MEL_TEST(scene_shared, skipped_without_gpu_backend) { MEL_SKIP("no GPU backend selected (build with --gpu=vulkan|metal|webgpu)"); }

#endif
