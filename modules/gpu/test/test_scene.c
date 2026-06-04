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

#else

MEL_TEST(scene_shared, skipped_without_gpu_backend) { MEL_SKIP("no GPU backend selected (build with --gpu=vulkan|metal|webgpu)"); }

#endif
