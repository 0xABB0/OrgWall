#include <test/test.h>

#if MEL_GPU_WEBGPU

#include <gpu/device.h>
#include <gpu/caps.h>
#include <gpu/buffer.h>
#include <gpu/texture.h>
#include <gpu/pipeline.h>
#include <gpu/binding.h>
#include <gpu/shader.h>
#include <gpu/queue.h>
#include <gpu/command.h>
#include <gpu/rendering.h>
#include <gpu/state.h>

#include <log/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "img_golden.h"

#include "../../../apps/hello-gpu/src/triangle_bundle.h"

#define VISUAL_BACKEND "webgpu"

/* Cross-backend rasterization (Dawn-on-Metal vs the Vulkan goldens) diverges on
   coverage at triangle edges and on sRGB-free interpolation; a generous edge
   tolerance is justified. The triangle here is rendered from the shared hello-gpu
   WGSL bundle (browser-parity), not the bindless Vulkan scenes, so no Vulkan golden
   covers it — the reference is webgpu's own dump. Deltas are reported. */
static const Mel_Golden_Tolerance VISUAL_TOL_TRIANGLE = { .max_channel_delta = 6, .max_fraction_exceeding = 0.05f };

typedef struct
{
    f32 pos[3];
    f32 color[4];
} Vertex;

static Mel_Gpu_Device* test_make_device(Mel_Gpu_Instance** out_inst)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-webgpu-test", .debug = { .enabled = true });
    if (!inst)
        return NULL;
    Mel_Gpu_Adapter* adapters[4];
    u32              n = mel_gpu_adapters(inst, adapters, 4);
    if (n == 0)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    Mel_Gpu_Device_Create_Result dr = mel_gpu_device_create(inst, adapters[0], .features = { 0 });
    if (!dr.value)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    *out_inst = inst;
    return dr.value;
}

static void test_dump_ppm(const char* name, const u8* rgba, u32 w, u32 h)
{
    char  path[512];
    snprintf(path, sizeof path, "modules/gpu/build/macos-debug/%s.ppm", name);
    FILE* f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (u32 i = 0; i < w * h; i++)
        fwrite(rgba + (usize)i * 4, 1, 3, f);
    fclose(f);
}

MEL_TEST(webgpu_caps, honest_tiers)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dev);
    MEL_REQUIRE_NOT_NULL(caps);

    /* WebGPU core: bindless capped (never full), timeline emulated, tile-local emulated. */
    MEL_EXPECT_EQ(caps->memory.bindless.tier, MEL_GPU_TIER_CAPPED);
    MEL_EXPECT_EQ(caps->queues.timeline, MEL_GPU_TIMELINE_EMULATED);
    MEL_EXPECT_EQ(caps->raster.tile_local, MEL_GPU_TILE_LOCAL_EMULATED);
    MEL_EXPECT_EQ(caps->features.ray_tracing, MEL_GPU_RT_NONE);
    MEL_EXPECT(!caps->features.mesh_shaders);
    MEL_EXPECT(!caps->memory.persistent_map);
    /* WGSL pass-through is the honest browser-parity path; the vendored Dawn Release
       prebuilt has no SPIR-V reader, so spirv must report absent. */
    MEL_EXPECT(caps->shader.bytecode_passthrough.wgsl);
    MEL_EXPECT(!caps->shader.bytecode_passthrough.spirv);
    MEL_EXPECT(!mel_gpu_bindless_available(dev));

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

static void test_render_triangle(Mel_Gpu_Device* dev, Mel_Gpu_Shader shader, const char* dump_name)
{
    const u32 W = 64, H = 64;

    const Vertex verts[] = {
        { { 0.0f, 0.6f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { 0.6f, -0.6f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { -0.6f, -0.6f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
    };
    Mel_Gpu_Buffer_Create_Result vbo = mel_gpu_buffer_create(dev, .size = sizeof verts, .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD, .data = verts, .name = "triangle-vbo");
    MEL_REQUIRE(!mel_gpu_failed(vbo.status));

    const Mel_Gpu_Vertex_Element layout[] = {
        { .location = 0, .format = MEL_GPU_FORMAT_RGB32_FLOAT, .offset = offsetof(Vertex, pos) },
        { .location = 1, .format = MEL_GPU_FORMAT_RGBA32_FLOAT, .offset = offsetof(Vertex, color) },
    };
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev,
                                                                  .shader = shader, .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .cull = MEL_GPU_CULL_NONE,
                                                                  .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                  .vertex_layout = layout, .vertex_layout_count = 2, .vertex_stride = sizeof(Vertex), .name = dump_name);
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                              .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "triangle-rt");
    MEL_REQUIRE(!mel_gpu_failed(rt.status));
    Mel_Gpu_Texture_View_Create_Result rt_view = mel_gpu_texture_default_view(dev, rt.value);
    MEL_REQUIRE(!mel_gpu_failed(rt_view.status));
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "triangle-rb");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    Mel_Gpu_Color_Attachment  color = { .view = rt_view.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.08f, 0.10f, 0.13f, 1.0f) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = W, .height = H);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 0, vbo.value);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, rt.value, range, rb.value);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_REQUIRE_NOT_NULL(px);
    test_dump_ppm(dump_name, px, W, H);

    /* Center pixel sits inside the triangle: a blend of the three vertex colors,
       well away from the dark clear. */
    const u8* center = px + ((usize)(H / 2) * W + (W / 2)) * 4;
    MEL_EXPECT(center[0] + center[1] + center[2] > 120);
    MEL_EXPECT_EQ(center[3], 255u);
    /* Top-left corner is outside the triangle: the dark clear color (~0.08..0.13). */
    const u8* tl = px;
    MEL_EXPECT(tl[0] < 40 && tl[1] < 40 && tl[2] < 50);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_texture_view_destroy(dev, rt_view.value);
    mel_gpu_texture_destroy(dev, rt.value);
    mel_gpu_buffer_destroy(dev, vbo.value);
}

/* The WGSL path (browser-parity): consume the Slang-emitted WGSL bundle directly. */
MEL_TEST(webgpu_visual, triangle_wgsl_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    /* Golden diffing against the macOS-Vulkan references is deliberately not wired:
       every committed golden is a bindless / compute scene that WebGPU core cannot
       reproduce (no true bindless, no push constants), so none covers this WGSL
       triangle. Rendering is proved programmatically; the produced PPM is dumped as an
       artifact. The MEL_GOLDEN harness and webgpu label stay available for a future
       cross-backend reference. */
    (void)VISUAL_TOL_TRIANGLE;

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .target = MEL_GPU_SHADER_TARGET_WGSL,
                                                                          .vertex_blob = TRIANGLE_VERT_WGSL, .vertex_blob_size = sizeof TRIANGLE_VERT_WGSL,
                                                                          .fragment_blob = TRIANGLE_FRAG_WGSL, .fragment_blob_size = sizeof TRIANGLE_FRAG_WGSL,
                                                                          .vertex_entry = TRIANGLE_VERT_ENTRY, .fragment_entry = TRIANGLE_FRAG_ENTRY, .name = "triangle-wgsl");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    test_render_triangle(dev, sh.value, "webgpu_triangle_wgsl");

    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

/* The SPIR-V path: hello-gpu's triangle scene passes SPIR-V, but the vendored Dawn
   Release prebuilt has no SPIR-V reader. The backend must refuse loudly (MEL-ENGINE-VIII),
   never silently no-op. This pins that honest-failure contract. */
MEL_TEST(webgpu_visual, triangle_spirv_refused_loudly)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .target = MEL_GPU_SHADER_TARGET_SPIRV,
                                                                          .vertex_blob = TRIANGLE_VERT_SPV, .vertex_blob_size = sizeof TRIANGLE_VERT_SPV,
                                                                          .fragment_blob = TRIANGLE_FRAG_SPV, .fragment_blob_size = sizeof TRIANGLE_FRAG_SPV,
                                                                          .vertex_entry = TRIANGLE_VERT_ENTRY, .fragment_entry = TRIANGLE_FRAG_ENTRY, .name = "triangle-spirv");
    MEL_EXPECT(mel_gpu_failed(sh.status));
    MEL_EXPECT_EQ(sh.status, MEL_GPU_SHADER_CREATE_TARGET_UNSUPPORTED);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

static void test_clear_to_readback(Mel_Gpu_Device* dev, Mel_Gpu_Queue* q, Mel_Gpu_Texture rt, Mel_Gpu_Texture_View rt_view, Mel_Gpu_Buffer rb, u32 w, u32 h, Mel_Gpu_Color clear)
{
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    Mel_Gpu_Color_Attachment  color = { .view = rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = clear };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = w, .height = h);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, rt, range, rb);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);
    mel_gpu_command_list_destroy(cmd);
}

/* The readback buffer is mapped, read, then reused as a copy target by a second submit
   and read again. A sticky one-shot map would freeze the first snapshot and fail Dawn
   validation on the second copy-into-still-mapped buffer. mel_gpu_buffer_mapped must
   re-map per call so the second read reflects the second render (MEL-ENGINE-VIII/IX). */
MEL_TEST(webgpu_resources, mapped_remaps_per_call)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 W = 64, H = 64;
    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                              .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "remap-rt");
    MEL_REQUIRE(!mel_gpu_failed(rt.status));
    Mel_Gpu_Texture_View_Create_Result rt_view = mel_gpu_texture_default_view(dev, rt.value);
    MEL_REQUIRE(!mel_gpu_failed(rt_view.status));
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "remap-rb");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));

    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);

    test_clear_to_readback(dev, q, rt.value, rt_view.value, rb.value, W, H, mel_gpu_rgba(1.0f, 0.0f, 0.0f, 1.0f));
    const u8* first = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_REQUIRE_NOT_NULL(first);
    MEL_EXPECT(first[0] > 200 && first[1] < 40 && first[2] < 40);

    test_clear_to_readback(dev, q, rt.value, rt_view.value, rb.value, W, H, mel_gpu_rgba(0.0f, 1.0f, 0.0f, 1.0f));
    const u8* second = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_REQUIRE_NOT_NULL(second);
    MEL_EXPECT(second[0] < 40 && second[1] > 200 && second[2] < 40);

    mel_gpu_queue_release(q);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_texture_view_destroy(dev, rt_view.value);
    mel_gpu_texture_destroy(dev, rt.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

#endif
