#include <test/test.h>

#if MEL_GPU_VULKAN

#include <gpu/device.h>
#include <gpu/caps.h>
#include <gpu/buffer.h>
#include <gpu/texture.h>
#include <gpu/sampler.h>
#include <gpu/binding.h>
#include <gpu/bind_group.h>
#include <gpu/pipeline.h>
#include <gpu/shader.h>
#include <gpu/queue.h>
#include <gpu/command.h>
#include <gpu/rendering.h>
#include <gpu/state.h>

#include <log/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "visual_spv.h"

// The "visual" half of the gpu test surface (gpu-rhi.md §6.7 / §7): every technique here renders offscreen,
// copies the RGBA8 target into a READBACK buffer, asserts specific pixels on the CPU (machine-checkable), AND
// dumps the readback as a binary PPM so a human can eyeball the golden image. The readback rows are tight
// (cmd_copy_texture_to_buffer issues bufferRowLength=0), so the W*H*4 buffer is a packed RGBA8 image.

// Where the human-viewable dumps land: alongside the test binary under build/<platform>-<config>/, so a
// human inspecting the run finds them next to what produced them. Absolute path is logged at each dump.
#define VISUAL_DUMP_DIR "modules/gpu/build/macos-debug"

static Mel_Gpu_Device* test_make_device_bindless(Mel_Gpu_Instance** out_inst)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-visual-test", .debug = { .enabled = true });
    if (!inst)
        return NULL;
    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(inst, adapters, 8);
    if (n == 0)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    Mel_Gpu_Device_Create_Result dr = mel_gpu_device_create(inst, adapters[0], .features = { .timeline_semaphores = true, .descriptor_indexing = true });
    if (!dr.value)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    *out_inst = inst;
    return dr.value;
}

// Dump a tight RGBA8 readback as a binary PPM (P6, 3 bytes/pixel, alpha dropped) and log the absolute path so
// a human can open the golden image. Failure to write is a warning, never a test failure — the dump is an aid,
// the pixel asserts are the proof.
static void test_dump_ppm(const char* name, const u8* rgba, u32 w, u32 h)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", VISUAL_DUMP_DIR, name);

    FILE* f = fopen(path, "wb");
    if (!f)
    {
        mel_log_warn("gpu", "visual: could not open %s for the golden dump (skipping)", path);
        return;
    }
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (u32 i = 0; i < w * h; i++)
        fwrite(rgba + (usize)i * 4, 1, 3, f);
    fclose(f);

    char abs[1024] = { 0 };
    if (!realpath(path, abs))
        snprintf(abs, sizeof abs, "%s", path);
    mel_log_info("gpu", "visual: golden image dumped -> %s (%ux%u, P6 PPM)", abs, w, h);
}

// One offscreen color attachment + a readback buffer, the shared scaffold of every visual test.
typedef struct
{
    u32                  w, h;
    Mel_Gpu_Texture      rt;
    Mel_Gpu_Texture_View rt_view;
    Mel_Gpu_Buffer       rb;
} Visual_Target;

static Visual_Target test_target_create(Mel_Gpu_Device* dev, u32 w, u32 h)
{
    Visual_Target                 t = { .w = w, .h = h };
    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { w, h, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                              .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "visual-rt");
    t.rt = rt.value;
    t.rt_view = mel_gpu_texture_default_view(dev, rt.value).value;
    t.rb = mel_gpu_buffer_create(dev, .size = (usize)w * h * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "visual-rb").value;
    return t;
}

static void test_target_destroy(Mel_Gpu_Device* dev, Visual_Target* t)
{
    mel_gpu_buffer_destroy(dev, t->rb);
    mel_gpu_texture_view_destroy(dev, t->rt_view);
    mel_gpu_texture_destroy(dev, t->rt);
}

// ============================================================================================================
// Uniform-buffer bindless (gpu-rhi.md §6.7) — the heap class the binding-finish writeup flagged as registered
// but lacking a dedicated pixel test. A UBO created with MEL_GPU_BUFFER_UNIFORM auto-registers into the set-0
// uniform-buffer heap class (binding 3, slot == handle.index). The fragment reads u_ubos[root.ubo].color and
// writes it to every fragment; the readback must equal the UBO's colour — the value reached the output through
// the uniform-buffer heap, proving the class end-to-end.
// ============================================================================================================
MEL_TEST(visual_bindless, uniform_buffer_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));
    MEL_REQUIRE(mel_gpu_device_caps(dev)->memory.bindless.max_uniform_buffer_slots > 0u);

    // The UBO payload: one vec4 colour. Created with UNIFORM usage so it registers in the uniform-buffer heap.
    const f32                    ubo_color[4] = { 0.30f, 0.55f, 0.80f, 1.0f };
    Mel_Gpu_Buffer_Create_Result ubo = mel_gpu_buffer_create(dev, .size = sizeof ubo_color, .usage = MEL_GPU_BUFFER_UNIFORM, .memory = MEL_GPU_MEMORY_UPLOAD,
                                                             .data = ubo_color, .name = "visual-ubo");
    MEL_REQUIRE(!mel_gpu_failed(ubo.status));
    u32 ubo_slot = mel_gpu_buffer_bindless_slot(dev, ubo.value);

    Visual_Target tgt = test_target_create(dev, 8, 8);

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = VISUAL_FULLSCREEN_VERT_SPV, .spirv_vertex_size = sizeof VISUAL_FULLSCREEN_VERT_SPV,
                                                                          .spirv_fragment = VISUAL_UBO_FRAG_SPV, .spirv_fragment_size = sizeof VISUAL_UBO_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "ubo-bindless");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    // Reflection derives both the bindless heap signature (set-0 runtime UBO array) and the 4-byte root record.
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .name = "ubo-bindless");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, tgt.rt, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment color = { .view = tgt.rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = tgt.w, .height = tgt.h);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof ubo_slot, &ubo_slot);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, tgt.rt, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, tgt.rt, range, tgt.rb);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, tgt.rb);
    MEL_REQUIRE_NOT_NULL(px);
    test_dump_ppm("ubo_bindless", px, tgt.w, tgt.h);

    // Every fragment read the same UBO colour: the whole target equals (0.30, 0.55, 0.80).
    MEL_EXPECT(px[0] >= 75 && px[0] <= 78);     // 0.30 -> 76.5
    MEL_EXPECT(px[1] >= 139 && px[1] <= 142);   // 0.55 -> 140.25
    MEL_EXPECT(px[2] >= 202 && px[2] <= 205);   // 0.80 -> 204
    MEL_EXPECT_EQ(px[3], 255u);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    test_target_destroy(dev, &tgt);
    mel_gpu_buffer_destroy(dev, ubo.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

// ============================================================================================================
// Bindless sampled texture with a procedural checker (gpu-rhi.md §6.7). A 4x4 RGBA8 texture is filled with a
// red/blue checker via texture_write, registered in the sampled-image heap, and sampled through a NEAREST
// heap-resident sampler at the fragment UV. The fullscreen triangle maps UV [0,1] across an 8x8 target, so the
// checker is magnified 2x; corner pixels land on known texels and are asserted, and the whole frame is dumped.
// ============================================================================================================
MEL_TEST(visual_bindless, sampled_checker_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    // Source checker: 4x4 RGBA8, (x^y) parity picks red vs blue. Texel (0,0) red, (1,0) blue, etc.
    const u32 SW = 4, SH = 4;
    u8        checker[SW * SH * 4];
    for (u32 y = 0; y < SH; y++)
        for (u32 x = 0; x < SW; x++)
        {
            u8*  p = checker + ((usize)y * SW + x) * 4;
            bool red = ((x ^ y) & 1) == 0;
            p[0] = red ? 230 : 20;
            p[1] = 20;
            p[2] = red ? 20 : 230;
            p[3] = 255;
        }

    Mel_Gpu_Texture_Create_Result src = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { SW, SH, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                               .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_DST, .name = "checker");
    MEL_REQUIRE(!mel_gpu_failed(src.status));
    Mel_Gpu_Texture_Region region = { .subresource = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 }, .offset = { 0, 0, 0 }, .extent = { SW, SH, 1 } };
    mel_gpu_texture_write(dev, src.value, region, checker, sizeof checker);
    Mel_Gpu_Texture_View_Create_Result src_view = mel_gpu_texture_default_view(dev, src.value);
    MEL_REQUIRE(!mel_gpu_failed(src_view.status));

    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST,
                                                               .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE, .name = "nearest");
    MEL_REQUIRE(!mel_gpu_failed(smp.status));

    Visual_Target tgt = test_target_create(dev, 8, 8);

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = VISUAL_FULLSCREEN_VERT_SPV, .spirv_vertex_size = sizeof VISUAL_FULLSCREEN_VERT_SPV,
                                                                          .spirv_fragment = VISUAL_SAMPLED_FRAG_SPV, .spirv_fragment_size = sizeof VISUAL_SAMPLED_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "sampled");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .name = "sampled");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    struct
    {
        u32 tex, smp;
    } root = { mel_gpu_texture_view_bindless_slot(dev, src_view.value), mel_gpu_sampler_bindless_slot(dev, smp.value) };

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, tgt.rt, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment color = { .view = tgt.rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = tgt.w, .height = tgt.h);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, tgt.rt, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, tgt.rt, range, tgt.rb);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, tgt.rb);
    MEL_REQUIRE_NOT_NULL(px);
    test_dump_ppm("sampled_checker", px, tgt.w, tgt.h);

    // The 4x4 checker is sampled across an 8x8 target (NEAREST + CLAMP_EDGE), magnifying each texel into a 2x2
    // block. The fullscreen triangle's UV maps clip-space to framebuffer with the usual Y inversion, so the
    // rendered top-left lands on the checker's last texel row: TL is blue, the adjacent block to its right is
    // red. The assertion below pins those two saturated taps — the checker reached the output through the
    // sampled-image heap — and confirms a hard parity flip across the 2-pixel block boundary (no smearing).
    const u8* tl = px + 0;                          // target (0,0)
    const u8* mid = px + (usize)2 * 4;              // target (2,0): next checker block
    MEL_EXPECT(tl[2] >= 228 && tl[0] <= 22);        // blue
    MEL_EXPECT(mid[0] >= 228 && mid[2] <= 22);      // red — parity flips one block over
    MEL_EXPECT_EQ(tl[3], 255u);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    test_target_destroy(dev, &tgt);
    mel_gpu_sampler_destroy(dev, smp.value);
    mel_gpu_texture_view_destroy(dev, src_view.value);
    mel_gpu_texture_destroy(dev, src.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

// ============================================================================================================
// Alpha blending (gpu-rhi.md §6.5). Clear to opaque (0.2,0.4,0.6,1); draw a fullscreen src (1,0,0,0.5) through
// a MEL_GPU_BLEND_ALPHA pipeline. src-over gives 0.5*src + 0.5*dst per channel; the covered pixel is asserted
// and the frame dumped. Drives the bindless sampled fragment with a 1x1 red-half-alpha texture so the visual
// suite stays self-contained (no extra solid-colour push-constant shader to embed).
// ============================================================================================================
MEL_TEST(visual_state, alpha_blend_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    // A 1x1 translucent-red source: (255,0,0,128). Sampled everywhere, then alpha-blended over the clear.
    const u8                      src_px[4] = { 255, 0, 0, 128 };
    Mel_Gpu_Texture_Create_Result src = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { 1, 1, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                               .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_DST, .name = "translucent");
    MEL_REQUIRE(!mel_gpu_failed(src.status));
    Mel_Gpu_Texture_Region region = { .subresource = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 }, .offset = { 0, 0, 0 }, .extent = { 1, 1, 1 } };
    mel_gpu_texture_write(dev, src.value, region, src_px, sizeof src_px);
    Mel_Gpu_Texture_View_Create_Result src_view = mel_gpu_texture_default_view(dev, src.value);
    Mel_Gpu_Sampler_Create_Result      smp = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST,
                                                               .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE, .name = "n");
    MEL_REQUIRE(!mel_gpu_failed(smp.status));

    Visual_Target tgt = test_target_create(dev, 8, 8);

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = VISUAL_FULLSCREEN_VERT_SPV, .spirv_vertex_size = sizeof VISUAL_FULLSCREEN_VERT_SPV,
                                                                          .spirv_fragment = VISUAL_SAMPLED_FRAG_SPV, .spirv_fragment_size = sizeof VISUAL_SAMPLED_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "blend");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Color_Target           target = { .format = MEL_GPU_FORMAT_RGBA8_UNORM, .blend = MEL_GPU_BLEND_ALPHA };
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_targets = &target, .color_target_count = 1, .name = "blend");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    struct
    {
        u32 tex, smp;
    } root = { mel_gpu_texture_view_bindless_slot(dev, src_view.value), mel_gpu_sampler_bindless_slot(dev, smp.value) };

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, tgt.rt, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment color = { .view = tgt.rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.2f, 0.4f, 0.6f, 1.0f) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = tgt.w, .height = tgt.h);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, tgt.rt, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, tgt.rt, range, tgt.rb);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, tgt.rb);
    MEL_REQUIRE_NOT_NULL(px);
    test_dump_ppm("alpha_blend", px, tgt.w, tgt.h);

    // src-over with src=(1,0,0,a=0.5), dst=(0.2,0.4,0.6): out = 0.5*src + 0.5*dst = (0.6, 0.2, 0.3).
    MEL_EXPECT(px[0] >= 151 && px[0] <= 155);   // 0.6
    MEL_EXPECT(px[1] >= 49 && px[1] <= 53);     // 0.2
    MEL_EXPECT(px[2] >= 74 && px[2] <= 78);     // 0.3
    MEL_EXPECT_EQ(px[3], 255u);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    test_target_destroy(dev, &tgt);
    mel_gpu_sampler_destroy(dev, smp.value);
    mel_gpu_texture_view_destroy(dev, src_view.value);
    mel_gpu_texture_destroy(dev, src.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

// ============================================================================================================
// Two distinct render targets, each painted a different UBO colour and read back (gpu-rhi.md §6.5). Exercises
// two passes in one command list plus the UBO heap class with two live slots; both frames are dumped and each
// is pixel-verified. (The single-pipeline MRT-with-two-outputs variant is covered by test_vulkan's
// vk_pipeline.mrt_two_targets; this is the multi-pass visual cousin.)
// ============================================================================================================
MEL_TEST(visual_state, two_targets_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    const f32                    c0[4] = { 0.25f, 0.50f, 0.75f, 1.0f };
    const f32                    c1[4] = { 0.90f, 0.10f, 0.40f, 1.0f };
    Mel_Gpu_Buffer_Create_Result ubo0 = mel_gpu_buffer_create(dev, .size = sizeof c0, .usage = MEL_GPU_BUFFER_UNIFORM, .memory = MEL_GPU_MEMORY_UPLOAD, .data = c0, .name = "ubo0");
    Mel_Gpu_Buffer_Create_Result ubo1 = mel_gpu_buffer_create(dev, .size = sizeof c1, .usage = MEL_GPU_BUFFER_UNIFORM, .memory = MEL_GPU_MEMORY_UPLOAD, .data = c1, .name = "ubo1");
    MEL_REQUIRE(!mel_gpu_failed(ubo0.status) && !mel_gpu_failed(ubo1.status));
    u32 slots[2] = { mel_gpu_buffer_bindless_slot(dev, ubo0.value), mel_gpu_buffer_bindless_slot(dev, ubo1.value) };

    Visual_Target a = test_target_create(dev, 8, 8);
    Visual_Target b = test_target_create(dev, 8, 8);

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = VISUAL_FULLSCREEN_VERT_SPV, .spirv_vertex_size = sizeof VISUAL_FULLSCREEN_VERT_SPV,
                                                                          .spirv_fragment = VISUAL_UBO_FRAG_SPV, .spirv_fragment_size = sizeof VISUAL_UBO_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "two-ubo");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .name = "two-ubo");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };

    Visual_Target* targets[2] = { &a, &b };
    for (u32 i = 0; i < 2; i++)
    {
        Visual_Target* t = targets[i];
        mel_gpu_cmd_texture_barrier(cmd, t->rt, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
        Mel_Gpu_Color_Attachment color = { .view = t->rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
        mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = t->w, .height = t->h);
        mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
        mel_gpu_cmd_push_constants(cmd, 0, sizeof slots[i], &slots[i]);
        mel_gpu_cmd_draw(cmd, 3, 1);
        mel_gpu_cmd_end_rendering(cmd);
        mel_gpu_cmd_texture_barrier(cmd, t->rt, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
        mel_gpu_cmd_copy_texture_to_buffer(cmd, t->rt, range, t->rb);
    }
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* p0 = mel_gpu_buffer_mapped(dev, a.rb);
    const u8* p1 = mel_gpu_buffer_mapped(dev, b.rb);
    MEL_REQUIRE_NOT_NULL(p0);
    MEL_REQUIRE_NOT_NULL(p1);
    test_dump_ppm("two_targets_0", p0, a.w, a.h);
    test_dump_ppm("two_targets_1", p1, b.w, b.h);

    MEL_EXPECT(p0[0] >= 62 && p0[0] <= 66);     // 0.25
    MEL_EXPECT(p0[1] >= 126 && p0[1] <= 130);   // 0.50
    MEL_EXPECT(p0[2] >= 189 && p0[2] <= 193);   // 0.75
    MEL_EXPECT(p1[0] >= 227 && p1[0] <= 231);   // 0.90
    MEL_EXPECT(p1[1] >= 23 && p1[1] <= 27);     // 0.10
    MEL_EXPECT(p1[2] >= 100 && p1[2] <= 104);   // 0.40

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    test_target_destroy(dev, &a);
    test_target_destroy(dev, &b);
    mel_gpu_buffer_destroy(dev, ubo0.value);
    mel_gpu_buffer_destroy(dev, ubo1.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

#else

MEL_TEST(visual_bindless, skipped_without_vulkan) { MEL_SKIP("vulkan backend not selected (build with --gpu=vulkan)"); }

#endif
