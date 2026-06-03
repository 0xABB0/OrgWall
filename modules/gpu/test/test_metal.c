#include <test/test.h>

#if MEL_GPU_METAL

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

#include <log/log.h>

#include <stdio.h>
#include <stdlib.h>

#include "triangle_bundle.h"
#include "gradient_bundle.h"
#include "quad_bundle.h"

#define METAL_DUMP_DIR "modules/gpu/build/macos-debug"

typedef struct
{
    f32 pos[3];
    f32 color[4];
} Tri_Vertex;

static Mel_Gpu_Device* test_make_device(Mel_Gpu_Instance** out_inst)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-metal-test", .debug = { .enabled = true });
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

static void test_dump_ppm(const char* name, const u8* rgba, u32 w, u32 h)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", METAL_DUMP_DIR, name);
    FILE* f = fopen(path, "wb");
    if (!f)
    {
        mel_log_warn("gpu", "metal-visual: could not open %s for dump (skipping)", path);
        return;
    }
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (u32 i = 0; i < w * h; i++)
        fwrite(rgba + (usize)i * 4, 1, 3, f);
    fclose(f);
    mel_log_info("gpu", "metal-visual: image dumped -> %s (%ux%u)", path, w, h);
}

typedef struct
{
    u32                  w, h;
    Mel_Gpu_Texture      rt;
    Mel_Gpu_Texture_View rt_view;
    Mel_Gpu_Buffer       rb;
} Metal_Target;

static Metal_Target test_target_create(Mel_Gpu_Device* dev, u32 w, u32 h)
{
    Metal_Target                  t = { .w = w, .h = h };
    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { w, h, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "metal-rt");
    t.rt = rt.value;
    t.rt_view = mel_gpu_texture_default_view(dev, rt.value).value;
    t.rb = mel_gpu_buffer_create(dev, .size = (usize)w * h * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "metal-rb").value;
    return t;
}

static void test_target_destroy(Mel_Gpu_Device* dev, Metal_Target* t)
{
    mel_gpu_buffer_destroy(dev, t->rb);
    mel_gpu_texture_view_destroy(dev, t->rt_view);
    mel_gpu_texture_destroy(dev, t->rt);
}

MEL_TEST(metal_caps, msl_passthrough)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_EXPECT(mel_gpu_device_caps(dev)->shader.bytecode_passthrough.msl);
    MEL_EXPECT(!mel_gpu_device_caps(dev)->shader.bytecode_passthrough.spirv);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(metal_render, triangle_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const Tri_Vertex verts[] = {
        { { 0.0f, 0.6f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { 0.6f, -0.6f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { -0.6f, -0.6f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
    };
    Mel_Gpu_Buffer_Create_Result vbo = mel_gpu_buffer_create(dev, .size = sizeof verts, .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD, .data = verts, .name = "tri-vbo");
    MEL_REQUIRE(!mel_gpu_failed(vbo.status));

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .target = MEL_GPU_SHADER_TARGET_MSL,
                                                                          .vertex_blob = TRIANGLE_VERT_MSL,
                                                                          .vertex_blob_size = sizeof TRIANGLE_VERT_MSL,
                                                                          .fragment_blob = TRIANGLE_FRAG_MSL,
                                                                          .fragment_blob_size = sizeof TRIANGLE_FRAG_MSL,
                                                                          .vertex_entry = TRIANGLE_VERT_ENTRY,
                                                                          .fragment_entry = TRIANGLE_FRAG_ENTRY,
                                                                          .name = "triangle");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    const Mel_Gpu_Vertex_Element layout[] = {
        { .location = 0, .format = MEL_GPU_FORMAT_RGB32_FLOAT, .offset = offsetof(Tri_Vertex, pos) },
        { .location = 1, .format = MEL_GPU_FORMAT_RGBA32_FLOAT, .offset = offsetof(Tri_Vertex, color) },
    };
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev,
                                                                  .shader = sh.value,
                                                                  .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                  .cull = MEL_GPU_CULL_NONE,
                                                                  .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                  .vertex_layout = layout,
                                                                  .vertex_layout_count = 2,
                                                                  .vertex_stride = sizeof(Tri_Vertex),
                                                                  .name = "triangle");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Metal_Target          tgt = test_target_create(dev, 32, 32);
    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    Mel_Gpu_Color_Attachment  color = { .view = tgt.rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = tgt.w, .height = tgt.h);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 0, vbo.value);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, tgt.rt, range, tgt.rb);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_wait(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, tgt.rb);
    MEL_REQUIRE_NOT_NULL(px);
    test_dump_ppm("metal_triangle", px, tgt.w, tgt.h);

    const u8* corner = px;
    MEL_EXPECT(corner[0] == 0 && corner[1] == 0 && corner[2] == 0);

    const u8* center = px + ((usize)(tgt.h / 2) * tgt.w + tgt.w / 2) * 4;
    MEL_EXPECT(center[0] + center[1] + center[2] > 60);
    MEL_EXPECT_EQ(center[3], 255u);

    u32 colored = 0;
    for (u32 i = 0; i < tgt.w * tgt.h; i++)
        if (px[(usize)i * 4] + px[(usize)i * 4 + 1] + px[(usize)i * 4 + 2] > 30)
            colored++;
    MEL_EXPECT(colored > 80 && colored < tgt.w * tgt.h);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    test_target_destroy(dev, &tgt);
    mel_gpu_buffer_destroy(dev, vbo.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(metal_render, gradient_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .target = MEL_GPU_SHADER_TARGET_MSL,
                                                                          .vertex_blob = GRADIENT_VERT_MSL,
                                                                          .vertex_blob_size = sizeof GRADIENT_VERT_MSL,
                                                                          .fragment_blob = GRADIENT_FRAG_MSL,
                                                                          .fragment_blob_size = sizeof GRADIENT_FRAG_MSL,
                                                                          .vertex_entry = GRADIENT_VERT_ENTRY,
                                                                          .fragment_entry = GRADIENT_FRAG_ENTRY,
                                                                          .name = "gradient");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .name = "gradient");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Metal_Target          tgt = test_target_create(dev, 32, 32);
    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    Mel_Gpu_Color_Attachment  color = { .view = tgt.rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = tgt.w, .height = tgt.h);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, tgt.rt, range, tgt.rb);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_wait(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, tgt.rb);
    MEL_REQUIRE_NOT_NULL(px);
    test_dump_ppm("metal_gradient", px, tgt.w, tgt.h);

    const u8* row0 = px + ((usize)0 * tgt.w + tgt.w / 2) * 4;
    const u8* rowN = px + ((usize)(tgt.h - 1) * tgt.w + tgt.w / 2) * 4;

    MEL_EXPECT(rowN[0] >= 14 && rowN[0] <= 26);
    MEL_EXPECT(rowN[1] >= 15 && rowN[1] <= 27);
    MEL_EXPECT(rowN[2] >= 41 && rowN[2] <= 53);
    MEL_EXPECT(row0[0] > rowN[0]);
    MEL_EXPECT(row0[2] > rowN[2]);
    MEL_EXPECT_EQ(row0[3], 255u);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    test_target_destroy(dev, &tgt);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(metal_render, quad_pushconstant_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .target = MEL_GPU_SHADER_TARGET_MSL,
                                                                          .vertex_blob = QUAD_VERT_MSL,
                                                                          .vertex_blob_size = sizeof QUAD_VERT_MSL,
                                                                          .fragment_blob = QUAD_FRAG_MSL,
                                                                          .fragment_blob_size = sizeof QUAD_FRAG_MSL,
                                                                          .vertex_entry = QUAD_VERT_ENTRY,
                                                                          .fragment_entry = QUAD_FRAG_ENTRY,
                                                                          .name = "quad");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .push_constant_size = 32, .name = "quad");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    struct
    {
        f32 rect[4];
        f32 color[4];
    } root = { { 0.0f, 0.0f, 0.5f, 0.5f }, { 0.2f, 0.8f, 0.4f, 1.0f } };

    Metal_Target          tgt = test_target_create(dev, 32, 32);
    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    Mel_Gpu_Color_Attachment  color = { .view = tgt.rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = tgt.w, .height = tgt.h);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_draw(cmd, 6, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, tgt.rt, range, tgt.rb);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_wait(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, tgt.rb);
    MEL_REQUIRE_NOT_NULL(px);
    test_dump_ppm("metal_quad", px, tgt.w, tgt.h);

    const u8* center = px + ((usize)(tgt.h / 2) * tgt.w + tgt.w / 2) * 4;
    MEL_EXPECT(center[0] >= 49 && center[0] <= 53);
    MEL_EXPECT(center[1] >= 202 && center[1] <= 206);
    MEL_EXPECT(center[2] >= 100 && center[2] <= 104);
    MEL_EXPECT_EQ(center[3], 255u);

    const u8* corner = px;
    MEL_EXPECT(corner[0] == 0 && corner[1] == 0 && corner[2] == 0);

    u32 filled = 0;
    for (u32 i = 0; i < tgt.w * tgt.h; i++)
        if (px[(usize)i * 4 + 1] > 100)
            filled++;
    u32 quarter = tgt.w * tgt.h / 4;
    MEL_EXPECT(filled > quarter / 2 && filled < quarter * 2);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    test_target_destroy(dev, &tgt);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

#else

MEL_TEST(metal_render, skipped_without_metal) { MEL_SKIP("metal backend not selected (build with --gpu=metal)"); }

#endif
