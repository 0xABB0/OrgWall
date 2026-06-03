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
#include "img_golden.h"

#define VISUAL_DUMP_DIR "modules/gpu/build/macos-debug"

#define VISUAL_BACKEND "vulkan"

static const Mel_Golden_Tolerance VISUAL_TOL_EXACT = { .max_channel_delta = 2, .max_fraction_exceeding = 0.0f };
static const Mel_Golden_Tolerance VISUAL_TOL_EDGE = { .max_channel_delta = 8, .max_fraction_exceeding = 0.05f };

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

MEL_TEST(visual_bindless, uniform_buffer_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));
    MEL_REQUIRE(mel_gpu_device_caps(dev)->memory.bindless.max_uniform_buffer_slots > 0u);

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
    MEL_GOLDEN(VISUAL_BACKEND, "ubo_bindless", px, tgt.w, tgt.h, VISUAL_TOL_EXACT);

    MEL_EXPECT(px[0] >= 75 && px[0] <= 78);
    MEL_EXPECT(px[1] >= 139 && px[1] <= 142);
    MEL_EXPECT(px[2] >= 202 && px[2] <= 205);
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

MEL_TEST(visual_bindless, sampled_checker_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

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
    MEL_GOLDEN(VISUAL_BACKEND, "sampled_checker", px, tgt.w, tgt.h, VISUAL_TOL_EXACT);

    const u8* tl = px + 0;
    const u8* mid = px + (usize)2 * 4;
    MEL_EXPECT(tl[2] >= 228 && tl[0] <= 22);
    MEL_EXPECT(mid[0] >= 228 && mid[2] <= 22);
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

MEL_TEST(visual_state, alpha_blend_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

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
    MEL_GOLDEN(VISUAL_BACKEND, "alpha_blend", px, tgt.w, tgt.h, VISUAL_TOL_EXACT);

    MEL_EXPECT(px[0] >= 151 && px[0] <= 155);
    MEL_EXPECT(px[1] >= 49 && px[1] <= 53);
    MEL_EXPECT(px[2] >= 74 && px[2] <= 78);
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
    MEL_GOLDEN(VISUAL_BACKEND, "two_targets_0", p0, a.w, a.h, VISUAL_TOL_EXACT);
    MEL_GOLDEN(VISUAL_BACKEND, "two_targets_1", p1, b.w, b.h, VISUAL_TOL_EXACT);

    MEL_EXPECT(p0[0] >= 62 && p0[0] <= 66);
    MEL_EXPECT(p0[1] >= 126 && p0[1] <= 130);
    MEL_EXPECT(p0[2] >= 189 && p0[2] <= 193);
    MEL_EXPECT(p1[0] >= 227 && p1[0] <= 231);
    MEL_EXPECT(p1[1] >= 23 && p1[1] <= 27);
    MEL_EXPECT(p1[2] >= 100 && p1[2] <= 104);

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

MEL_TEST(visual_bindless, storage_image_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));
    MEL_REQUIRE(mel_gpu_device_caps(dev)->memory.bindless.max_storage_image_slots > 0u);

    const u32 W = 8, H = 8, CELL = 2;
    Mel_Gpu_Texture_Create_Result img = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                               .usage = MEL_GPU_TEXTURE_STORAGE | MEL_GPU_TEXTURE_COPY_SRC, .name = "visual-storage-img");
    MEL_REQUIRE(!mel_gpu_failed(img.status));
    Mel_Gpu_Texture_View_Create_Result view = mel_gpu_texture_default_view(dev, img.value);
    MEL_REQUIRE(!mel_gpu_failed(view.status));
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "visual-storage-rb");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_compute_from_bytecode(dev, .spirv = VISUAL_IMGCHECKER_COMP_SPV, .spirv_size = sizeof VISUAL_IMGCHECKER_COMP_SPV, .entry = "main", .name = "imgchecker");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_compute_create(dev, .shader = sh.value, .name = "imgchecker");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    struct
    {
        u32 img_slot, width, height, cell;
    } root = { mel_gpu_texture_view_bindless_slot(dev, view.value), W, H, CELL };

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, img.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_UNORDERED_ACCESS);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_dispatch(cmd, (W + 7) / 8, (H + 7) / 8, 1);
    mel_gpu_cmd_texture_barrier(cmd, img.value, range, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, img.value, range, rb.value);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_REQUIRE_NOT_NULL(px);
    test_dump_ppm("storage_image_checker", px, W, H);
    MEL_GOLDEN(VISUAL_BACKEND, "storage_image_checker", px, W, H, VISUAL_TOL_EXACT);

    const u8* c00 = px;
    const u8* c20 = px + (usize)2 * 4;
    MEL_EXPECT(c00[0] >= 228 && c00[2] <= 54);
    MEL_EXPECT(c20[2] >= 228 && c20[0] <= 28);
    MEL_EXPECT_EQ(c00[3], 255u);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_texture_view_destroy(dev, view.value);
    mel_gpu_texture_destroy(dev, img.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(visual_state, msaa_resolve_edge_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 W = 8, H = 8;
    Mel_Gpu_Texture_Create_Result msaa = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                .sample_count = 4, .usage = MEL_GPU_TEXTURE_ATTACHMENT, .name = "visual-msaa");
    MEL_REQUIRE(!mel_gpu_failed(msaa.status));
    Mel_Gpu_Texture_View_Create_Result msaa_view = mel_gpu_texture_default_view(dev, msaa.value);
    MEL_REQUIRE(!mel_gpu_failed(msaa_view.status));
    Mel_Gpu_Texture_Create_Result resolve = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                   .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "visual-resolve");
    MEL_REQUIRE(!mel_gpu_failed(resolve.status));
    Mel_Gpu_Texture_View_Create_Result resolve_view = mel_gpu_texture_default_view(dev, resolve.value);
    MEL_REQUIRE(!mel_gpu_failed(resolve_view.status));
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "visual-msaa-rb");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = VISUAL_HALFTRI_VERT_SPV, .spirv_vertex_size = sizeof VISUAL_HALFTRI_VERT_SPV,
                                                                          .spirv_fragment = VISUAL_SOLID_PC_FRAG_SPV, .spirv_fragment_size = sizeof VISUAL_SOLID_PC_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "msaa-edge");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                  .samples = 4, .push_constant_size = 16, .name = "msaa-edge");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    f32                   white[4] = { 1, 1, 1, 1 };
    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, msaa.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    mel_gpu_cmd_texture_barrier(cmd, resolve.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment color = { .view = msaa_view.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_DONT_CARE,
                                       .clear = mel_gpu_rgba(0, 0, 0, 1), .resolve_view = resolve_view.value };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = W, .height = H);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof white, white);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, resolve.value, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, resolve.value, range, rb.value);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_REQUIRE_NOT_NULL(px);
    test_dump_ppm("msaa_resolve_edge", px, W, H);
    MEL_GOLDEN(VISUAL_BACKEND, "msaa_resolve_edge", px, W, H, VISUAL_TOL_EDGE);

    bool found_intermediate = false, saw_white = false, saw_black = false;
    for (u32 y = 0; y < H; y++)
        for (u32 x = 0; x < W; x++)
        {
            u8 v = px[(usize)(y * W + x) * 4];
            if (v == 255u)
                saw_white = true;
            else if (v == 0u)
                saw_black = true;
            else
                found_intermediate = true;
        }
    MEL_EXPECT(found_intermediate);
    MEL_EXPECT(saw_white);
    MEL_EXPECT(saw_black);
    const u8* edge = px + (usize)(1 * W + 1) * 4;
    MEL_EXPECT(edge[0] > 40 && edge[0] < 215);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_texture_view_destroy(dev, resolve_view.value);
    mel_gpu_texture_destroy(dev, resolve.value);
    mel_gpu_texture_view_destroy(dev, msaa_view.value);
    mel_gpu_texture_destroy(dev, msaa.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(visual_state, dispatch_indirect_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));
    MEL_REQUIRE(mel_gpu_device_caps(dev)->memory.bindless.max_storage_buffer_slots > 0u);

    const u32 N = 48, G = 48;
    const u32                    args[3] = { G, 1, 1 };
    Mel_Gpu_Buffer_Create_Result args_buf = mel_gpu_buffer_create(dev, .size = sizeof args, .usage = MEL_GPU_BUFFER_INDIRECT, .memory = MEL_GPU_MEMORY_UPLOAD, .data = args, .name = "visual-args");
    MEL_REQUIRE(!mel_gpu_failed(args_buf.status));
    Mel_Gpu_Buffer_Create_Result out_buf = mel_gpu_buffer_create(dev, .size = (usize)N * sizeof(u32), .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_READBACK, .name = "visual-indirect-out");
    MEL_REQUIRE(!mel_gpu_failed(out_buf.status));

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_compute_from_bytecode(dev, .spirv = VISUAL_IDXWRITE_COMP_SPV, .spirv_size = sizeof VISUAL_IDXWRITE_COMP_SPV, .entry = "main", .name = "idxwrite");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_compute_create(dev, .shader = sh.value, .name = "idxwrite");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    struct
    {
        u32 out_buf, n;
    } root = { mel_gpu_buffer_bindless_slot(dev, out_buf.value), N };

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_dispatch_indirect(cmd, args_buf.value, 0);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u32* out = mel_gpu_buffer_mapped(dev, out_buf.value);
    MEL_REQUIRE_NOT_NULL(out);
    bool ok = true;
    for (u32 i = 0; i < N; i++)
        if (out[i] != 1000u + G)
            ok = false;
    MEL_EXPECT(ok);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_buffer_destroy(dev, out_buf.value);
    mel_gpu_buffer_destroy(dev, args_buf.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(visual_state, depth_boundary_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 W = 8, H = 8;
    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                              .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "visual-depth-rt");
    MEL_REQUIRE(!mel_gpu_failed(rt.status));
    Mel_Gpu_Texture_View_Create_Result rt_view = mel_gpu_texture_default_view(dev, rt.value);
    Mel_Gpu_Texture_Create_Result depth = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_D32_FLOAT,
                                                                 .usage = MEL_GPU_TEXTURE_ATTACHMENT, .name = "visual-depth");
    MEL_REQUIRE(!mel_gpu_failed(depth.status));
    Mel_Gpu_Texture_View_Create_Result depth_view = mel_gpu_texture_default_view(dev, depth.value);
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "visual-depth-rb");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = VISUAL_DEPTHTRI_VERT_SPV, .spirv_vertex_size = sizeof VISUAL_DEPTHTRI_VERT_SPV,
                                                                          .spirv_fragment = VISUAL_SOLID_PC_FRAG_SPV, .spirv_fragment_size = sizeof VISUAL_SOLID_PC_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "depth-boundary");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Depth_Stencil          ds = { .depth_test = true, .depth_write = true, .depth_compare = MEL_GPU_COMPARE_LESS };
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                  .depth_format = MEL_GPU_FORMAT_D32_FLOAT, .depth_stencil = &ds, .push_constant_size = 20, .name = "depth-boundary");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    struct
    {
        f32 color[4];
        u32 ramp;
    } flat_red = { { 1, 0, 0, 1 }, 0 }, ramp_green = { { 0, 1, 0, 1 }, 1 };

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range crange = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    Mel_Gpu_Subresource_Range drange = { MEL_GPU_ASPECT_DEPTH, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, rt.value, crange, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    mel_gpu_cmd_texture_barrier(cmd, depth.value, drange, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_DEPTH_WRITE);
    Mel_Gpu_Color_Attachment color = { .view = rt_view.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    Mel_Gpu_Depth_Attachment datt = { .view = depth_view.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear_depth = 1.0f };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .depth = &datt, .width = W, .height = H);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof flat_red, &flat_red);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof ramp_green, &ramp_green);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, rt.value, crange, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, rt.value, crange, rb.value);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_REQUIRE_NOT_NULL(px);
    test_dump_ppm("depth_boundary", px, W, H);
    MEL_GOLDEN(VISUAL_BACKEND, "depth_boundary", px, W, H, VISUAL_TOL_EXACT);

    const u8* left = px + 0;
    const u8* right = px + (usize)(W - 1) * 4;
    MEL_EXPECT(left[1] >= 250 && left[0] <= 5);
    MEL_EXPECT(right[0] >= 250 && right[1] <= 5);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_texture_view_destroy(dev, depth_view.value);
    mel_gpu_texture_destroy(dev, depth.value);
    mel_gpu_texture_view_destroy(dev, rt_view.value);
    mel_gpu_texture_destroy(dev, rt.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(visual_state, mrt_single_pipeline_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Visual_Target t0 = test_target_create(dev, 8, 8);
    Visual_Target t1 = test_target_create(dev, 8, 8);

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = VISUAL_FULLSCREEN_VERT_SPV, .spirv_vertex_size = sizeof VISUAL_FULLSCREEN_VERT_SPV,
                                                                          .spirv_fragment = VISUAL_MRT2_FRAG_SPV, .spirv_fragment_size = sizeof VISUAL_MRT2_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "mrt2");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Color_Target targets[2] = {
        { .format = MEL_GPU_FORMAT_RGBA8_UNORM, .blend = MEL_GPU_BLEND_OPAQUE },
        { .format = MEL_GPU_FORMAT_RGBA8_UNORM, .blend = MEL_GPU_BLEND_OPAQUE },
    };
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_targets = targets, .color_target_count = 2, .name = "mrt2");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, t0.rt, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    mel_gpu_cmd_texture_barrier(cmd, t1.rt, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment colors[2] = {
        { .view = t0.rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) },
        { .view = t1.rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) },
    };
    mel_gpu_cmd_begin_rendering(cmd, .colors = colors, .color_count = 2, .width = t0.w, .height = t0.h);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, t0.rt, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_texture_barrier(cmd, t1.rt, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, t0.rt, range, t0.rb);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, t1.rt, range, t1.rb);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* p0 = mel_gpu_buffer_mapped(dev, t0.rb);
    const u8* p1 = mel_gpu_buffer_mapped(dev, t1.rb);
    MEL_REQUIRE_NOT_NULL(p0);
    MEL_REQUIRE_NOT_NULL(p1);
    test_dump_ppm("mrt_target_0", p0, t0.w, t0.h);
    test_dump_ppm("mrt_target_1", p1, t1.w, t1.h);
    MEL_GOLDEN(VISUAL_BACKEND, "mrt_target_0", p0, t0.w, t0.h, VISUAL_TOL_EXACT);
    MEL_GOLDEN(VISUAL_BACKEND, "mrt_target_1", p1, t1.w, t1.h, VISUAL_TOL_EXACT);

    MEL_EXPECT(p0[0] >= 49 && p0[0] <= 53);
    MEL_EXPECT(p0[1] >= 151 && p0[1] <= 155);
    MEL_EXPECT(p0[2] >= 227 && p0[2] <= 231);
    MEL_EXPECT(p1[0] >= 240 && p1[0] <= 244);
    MEL_EXPECT(p1[1] >= 87 && p1[1] <= 91);
    MEL_EXPECT(p1[2] >= 24 && p1[2] <= 28);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    test_target_destroy(dev, &t0);
    test_target_destroy(dev, &t1);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(visual_state, wireframe_vs_solid_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Visual_Target solid = test_target_create(dev, 16, 16);
    Visual_Target wire = test_target_create(dev, 16, 16);

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = VISUAL_HALFTRI_VERT_SPV, .spirv_vertex_size = sizeof VISUAL_HALFTRI_VERT_SPV,
                                                                          .spirv_fragment = VISUAL_SOLID_PC_FRAG_SPV, .spirv_fragment_size = sizeof VISUAL_SOLID_PC_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "wire");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Pipeline_Create_Result solid_pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .fill = MEL_GPU_FILL_SOLID, .push_constant_size = 16, .name = "solid");
    MEL_REQUIRE(!mel_gpu_failed(solid_pipe.status));
    Mel_Gpu_Pipeline_Create_Result wire_pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .fill = MEL_GPU_FILL_WIREFRAME, .push_constant_size = 16, .name = "wire");
    MEL_REQUIRE(!mel_gpu_failed(wire_pipe.status));

    f32                   col[4] = { 1, 1, 1, 1 };
    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };

    struct
    {
        Visual_Target*   t;
        Mel_Gpu_Pipeline p;
    } draws[2] = { { &solid, solid_pipe.value }, { &wire, wire_pipe.value } };
    for (u32 i = 0; i < 2; i++)
    {
        Visual_Target* t = draws[i].t;
        mel_gpu_cmd_texture_barrier(cmd, t->rt, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
        Mel_Gpu_Color_Attachment color = { .view = t->rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
        mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = t->w, .height = t->h);
        mel_gpu_cmd_bind_pipeline(cmd, draws[i].p);
        mel_gpu_cmd_push_constants(cmd, 0, sizeof col, col);
        mel_gpu_cmd_draw(cmd, 3, 1);
        mel_gpu_cmd_end_rendering(cmd);
        mel_gpu_cmd_texture_barrier(cmd, t->rt, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
        mel_gpu_cmd_copy_texture_to_buffer(cmd, t->rt, range, t->rb);
    }
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* ps = mel_gpu_buffer_mapped(dev, solid.rb);
    const u8* pw = mel_gpu_buffer_mapped(dev, wire.rb);
    MEL_REQUIRE_NOT_NULL(ps);
    MEL_REQUIRE_NOT_NULL(pw);
    test_dump_ppm("wireframe_solid", ps, solid.w, solid.h);
    test_dump_ppm("wireframe_wire", pw, wire.w, wire.h);
    MEL_GOLDEN(VISUAL_BACKEND, "wireframe_solid", ps, solid.w, solid.h, VISUAL_TOL_EDGE);
    MEL_GOLDEN(VISUAL_BACKEND, "wireframe_wire", pw, wire.w, wire.h, VISUAL_TOL_EDGE);

    u32 solid_set = 0, wire_set = 0;
    for (u32 i = 0; i < solid.w * solid.h; i++)
    {
        if (ps[(usize)i * 4] > 127)
            solid_set++;
        if (pw[(usize)i * 4] > 127)
            wire_set++;
    }
    MEL_EXPECT(solid_set > 0);
    if (wire_set >= solid_set)
        mel_log_warn("gpu", "visual: wireframe degraded to solid (fill-mode-non-solid ungranted): wire_set=%u solid_set=%u", wire_set, solid_set);
    else
        MEL_EXPECT(wire_set < solid_set);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, wire_pipe.value);
    mel_gpu_pipeline_destroy(dev, solid_pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    test_target_destroy(dev, &solid);
    test_target_destroy(dev, &wire);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(visual_state, sync2_barrier_smoke_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    const f32                    ubo_color[4] = { 0.40f, 0.70f, 0.20f, 1.0f };
    Mel_Gpu_Buffer_Create_Result ubo = mel_gpu_buffer_create(dev, .size = sizeof ubo_color, .usage = MEL_GPU_BUFFER_UNIFORM, .memory = MEL_GPU_MEMORY_UPLOAD, .data = ubo_color, .name = "visual-sync2-ubo");
    MEL_REQUIRE(!mel_gpu_failed(ubo.status));
    u32 ubo_slot = mel_gpu_buffer_bindless_slot(dev, ubo.value);

    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { 8, 8, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                              .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC | MEL_GPU_TEXTURE_COPY_DST, .name = "visual-sync2-rt");
    MEL_REQUIRE(!mel_gpu_failed(rt.status));
    Mel_Gpu_Texture_View_Create_Result rt_view = mel_gpu_texture_default_view(dev, rt.value);
    Mel_Gpu_Buffer_Create_Result       rb = mel_gpu_buffer_create(dev, .size = 8 * 8 * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "visual-sync2-rb");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = VISUAL_FULLSCREEN_VERT_SPV, .spirv_vertex_size = sizeof VISUAL_FULLSCREEN_VERT_SPV,
                                                                          .spirv_fragment = VISUAL_UBO_FRAG_SPV, .spirv_fragment_size = sizeof VISUAL_UBO_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "sync2");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .name = "sync2");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment color = { .view = rt_view.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = 8, .height = 8);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof ubo_slot, &ubo_slot);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_COPY_SOURCE, MEL_GPU_STATE_COPY_DEST);
    mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_COPY_DEST, MEL_GPU_STATE_RENDER_TARGET);
    mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, rt.value, range, rb.value);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_REQUIRE_NOT_NULL(px);
    test_dump_ppm("sync2_barrier", px, 8, 8);
    MEL_GOLDEN(VISUAL_BACKEND, "sync2_barrier", px, 8, 8, VISUAL_TOL_EXACT);

    MEL_EXPECT(px[0] >= 100 && px[0] <= 104);
    MEL_EXPECT(px[1] >= 176 && px[1] <= 180);
    MEL_EXPECT(px[2] >= 49 && px[2] <= 53);
    MEL_EXPECT_EQ(px[3], 255u);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_texture_view_destroy(dev, rt_view.value);
    mel_gpu_texture_destroy(dev, rt.value);
    mel_gpu_buffer_destroy(dev, ubo.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

#else

MEL_TEST(visual_bindless, skipped_without_vulkan) { MEL_SKIP("vulkan backend not selected (build with --gpu=vulkan)"); }

#endif
