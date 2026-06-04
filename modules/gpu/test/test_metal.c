#include <test/test.h>

#if MEL_GPU_METAL

#include <gpu/device.h>
#include <gpu/caps.h>
#include <gpu/buffer.h>
#include <gpu/texture.h>
#include <gpu/sampler.h>
#include <gpu/binding.h>
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

static const char STATE_VERT_MSL[] = "#include <metal_stdlib>\n"
                                     "using namespace metal;\n"
                                     "struct VIn { float3 pos [[attribute(0)]]; float4 color [[attribute(1)]]; };\n"
                                     "struct VOut { float4 pos [[position]]; float4 color; };\n"
                                     "vertex VOut vs_main(VIn in [[stage_in]]) {\n"
                                     "    VOut o; o.pos = float4(in.pos, 1.0); o.color = in.color; return o;\n"
                                     "}\n";

static const char STATE_FRAG_MSL[] = "#include <metal_stdlib>\n"
                                     "using namespace metal;\n"
                                     "struct VOut { float4 pos [[position]]; float4 color; };\n"
                                     "fragment float4 fs_main(VOut in [[stage_in]]) { return in.color; }\n";

static Mel_Gpu_Shader mel_test_state_shader(Mel_Gpu_Device* dev)
{
    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .target = MEL_GPU_SHADER_TARGET_MSL,
                                                                          .vertex_blob = STATE_VERT_MSL,
                                                                          .vertex_blob_size = sizeof STATE_VERT_MSL,
                                                                          .fragment_blob = STATE_FRAG_MSL,
                                                                          .fragment_blob_size = sizeof STATE_FRAG_MSL,
                                                                          .vertex_entry = "vs_main",
                                                                          .fragment_entry = "fs_main",
                                                                          .name = "state-test");
    if (mel_gpu_failed(sh.status))
        return (Mel_Gpu_Shader){ mel_gpu_handle_null() };
    return sh.value;
}

MEL_TEST(metal_state, depth_occlusion)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Shader sh = mel_test_state_shader(dev);
    MEL_REQUIRE(!mel_gpu_handle_eq(sh.slot, mel_gpu_handle_null()));

    const Mel_Gpu_Vertex_Element layout[] = {
        { .location = 0, .format = MEL_GPU_FORMAT_RGB32_FLOAT, .offset = offsetof(Tri_Vertex, pos) },
        { .location = 1, .format = MEL_GPU_FORMAT_RGBA32_FLOAT, .offset = offsetof(Tri_Vertex, color) },
    };

    const Mel_Gpu_Depth_Stencil    depth_less = { .depth_test = true, .depth_write = true, .depth_compare = MEL_GPU_COMPARE_LESS };
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev,
                                                                  .shader = sh,
                                                                  .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                  .cull = MEL_GPU_CULL_NONE,
                                                                  .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                  .depth_format = MEL_GPU_FORMAT_D32_FLOAT,
                                                                  .depth_stencil = &depth_less,
                                                                  .vertex_layout = layout,
                                                                  .vertex_layout_count = 2,
                                                                  .vertex_stride = sizeof(Tri_Vertex),
                                                                  .name = "depth-less");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    const Tri_Vertex near_red[] = {
        { { 0.0f, 0.8f, 0.3f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { 0.8f, -0.8f, 0.3f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { -0.8f, -0.8f, 0.3f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
    };
    const Tri_Vertex far_green[] = {
        { { 0.0f, 0.8f, 0.7f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { 0.8f, -0.8f, 0.7f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { -0.8f, -0.8f, 0.7f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
    };
    Mel_Gpu_Buffer vb_near = mel_gpu_buffer_create(dev, .size = sizeof near_red, .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD, .data = near_red, .name = "near").value;
    Mel_Gpu_Buffer vb_far = mel_gpu_buffer_create(dev, .size = sizeof far_green, .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD, .data = far_green, .name = "far").value;

    Metal_Target         tgt = test_target_create(dev, 32, 32);
    Mel_Gpu_Texture      depth_tex = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { 32, 32, 1 }, .format = MEL_GPU_FORMAT_D32_FLOAT, .usage = MEL_GPU_TEXTURE_ATTACHMENT, .name = "depth").value;
    Mel_Gpu_Texture_View depth_view = mel_gpu_texture_default_view(dev, depth_tex).value;

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    Mel_Gpu_Color_Attachment  color = { .view = tgt.rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    Mel_Gpu_Depth_Attachment  depth = { .view = depth_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_DONT_CARE, .clear_depth = 1.0f };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .depth = &depth, .width = tgt.w, .height = tgt.h);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 0, vb_near);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 0, vb_far);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, tgt.rt, range, tgt.rb);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_wait(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, tgt.rb);
    MEL_REQUIRE_NOT_NULL(px);
    test_dump_ppm("metal_depth_occlusion", px, tgt.w, tgt.h);

    const u8* center = px + ((usize)(tgt.h / 2) * tgt.w + tgt.w / 2) * 4;
    MEL_EXPECT(center[0] > 200);
    MEL_EXPECT(center[1] < 60);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_texture_view_destroy(dev, depth_view);
    mel_gpu_texture_destroy(dev, depth_tex);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_buffer_destroy(dev, vb_near);
    mel_gpu_buffer_destroy(dev, vb_far);
    mel_gpu_shader_destroy(dev, sh);
    test_target_destroy(dev, &tgt);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

static u32 mel_test_cull_center_green(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh, const Mel_Gpu_Vertex_Element* layout, Mel_Gpu_Cull cull, Mel_Gpu_Front_Face front, const Tri_Vertex* verts, const char* dump)
{
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev,
                                                                  .shader = sh,
                                                                  .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                  .cull = cull,
                                                                  .front_face = front,
                                                                  .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                  .vertex_layout = layout,
                                                                  .vertex_layout_count = 2,
                                                                  .vertex_stride = sizeof(Tri_Vertex),
                                                                  .name = "cull");
    if (mel_gpu_failed(pipe.status))
        return 0xffffffffu;

    Mel_Gpu_Buffer vb = mel_gpu_buffer_create(dev, .size = sizeof(Tri_Vertex) * 3, .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD, .data = verts, .name = "cull-vb").value;
    Metal_Target   tgt = test_target_create(dev, 32, 32);

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    Mel_Gpu_Color_Attachment  color = { .view = tgt.rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = tgt.w, .height = tgt.h);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 0, vb);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, tgt.rt, range, tgt.rb);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    bool            ok = mel_gpu_ok(mel_gpu_future_wait(f));
    mel_gpu_future_destroy(f);

    u32       green = 0xffffffffu;
    const u8* px = mel_gpu_buffer_mapped(dev, tgt.rb);
    if (ok && px)
    {
        test_dump_ppm(dump, px, tgt.w, tgt.h);
        const u8* center = px + ((usize)(tgt.h / 2) * tgt.w + tgt.w / 2) * 4;
        green = center[1];
    }

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_buffer_destroy(dev, vb);
    test_target_destroy(dev, &tgt);
    return green;
}

MEL_TEST(metal_state, back_face_cull)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Shader sh = mel_test_state_shader(dev);
    MEL_REQUIRE(!mel_gpu_handle_eq(sh.slot, mel_gpu_handle_null()));

    const Mel_Gpu_Vertex_Element layout[] = {
        { .location = 0, .format = MEL_GPU_FORMAT_RGB32_FLOAT, .offset = offsetof(Tri_Vertex, pos) },
        { .location = 1, .format = MEL_GPU_FORMAT_RGBA32_FLOAT, .offset = offsetof(Tri_Vertex, color) },
    };

    const Tri_Vertex back_face[] = {
        { { 0.0f, 0.8f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { 0.8f, -0.8f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { -0.8f, -0.8f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
    };

    u32 culled = mel_test_cull_center_green(dev, sh, layout, MEL_GPU_CULL_BACK, MEL_GPU_FRONT_FACE_CCW, back_face, "metal_cull_back");
    u32 visible = mel_test_cull_center_green(dev, sh, layout, MEL_GPU_CULL_NONE, MEL_GPU_FRONT_FACE_CCW, back_face, "metal_cull_none");

    MEL_REQUIRE(culled != 0xffffffffu && visible != 0xffffffffu);
    MEL_EXPECT(culled < 40);
    MEL_EXPECT(visible > 200);

    mel_gpu_shader_destroy(dev, sh);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

static const char FILL_COMP_MSL[] = "#include <metal_stdlib>\n"
                                    "using namespace metal;\n"
                                    "struct Params { uint n; };\n"
                                    "kernel void cs_main(device uint* out [[buffer(1)]],\n"
                                    "                    constant Params& p [[buffer(0)]],\n"
                                    "                    uint gid [[thread_position_in_grid]]) {\n"
                                    "    if (gid >= p.n) return;\n"
                                    "    out[gid] = gid * 3u + 7u;\n"
                                    "}\n";

MEL_TEST(metal_compute, storage_buffer_write)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 N = 256;

    Mel_Gpu_Buffer_Create_Result out_buf = mel_gpu_buffer_create(dev, .size = (usize)N * sizeof(u32), .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_READBACK, .name = "compute-out");
    MEL_REQUIRE(!mel_gpu_failed(out_buf.status));

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_compute_from_bytecode(dev, .target = MEL_GPU_SHADER_TARGET_MSL, .compute_blob = FILL_COMP_MSL, .compute_blob_size = sizeof FILL_COMP_MSL, .entry = "cs_main", .name = "fill");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_compute_create(dev, .shader = sh.value, .name = "fill");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));
    MEL_REQUIRE(mel_gpu_pipeline_alive(dev, pipe.value));

    struct
    {
        u32 n;
    } root = { N };

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 1, out_buf.value);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_dispatch(cmd, (N + 31) / 32, 1, 1);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_wait(f)));
    mel_gpu_future_destroy(f);

    const u32* out = mel_gpu_buffer_mapped(dev, out_buf.value);
    MEL_REQUIRE_NOT_NULL(out);
    bool ok = true;
    for (u32 i = 0; i < N; i++)
        if (out[i] != i * 3u + 7u)
            ok = false;
    MEL_EXPECT(ok);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_buffer_destroy(dev, out_buf.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

static const char GEN_VERTS_COMP_MSL[] = "#include <metal_stdlib>\n"
                                         "using namespace metal;\n"
                                         "struct Vert { packed_float3 pos; packed_float4 color; };\n"
                                         "kernel void cs_main(device Vert* out [[buffer(1)]],\n"
                                         "                    uint gid [[thread_position_in_grid]]) {\n"
                                         "    if (gid >= 3u) return;\n"
                                         "    float2 p[3] = { float2(0.0, 0.8), float2(0.8, -0.8), float2(-0.8, -0.8) };\n"
                                         "    out[gid].pos = float3(p[gid], 0.0);\n"
                                         "    out[gid].color = float4(0.0, 1.0, 0.0, 1.0);\n"
                                         "}\n";

MEL_TEST(metal_compute, compute_to_graphics)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Buffer_Create_Result vbo = mel_gpu_buffer_create(dev, .size = sizeof(Tri_Vertex) * 3, .usage = MEL_GPU_BUFFER_STORAGE | MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_DEVICE, .name = "compute-vbo");
    MEL_REQUIRE(!mel_gpu_failed(vbo.status));

    Mel_Gpu_Shader_Create_Result csh = mel_gpu_shader_create_compute_from_bytecode(dev,
                                                                                   .target = MEL_GPU_SHADER_TARGET_MSL,
                                                                                   .compute_blob = GEN_VERTS_COMP_MSL,
                                                                                   .compute_blob_size = sizeof GEN_VERTS_COMP_MSL,
                                                                                   .entry = "cs_main",
                                                                                   .name = "gen-verts");
    MEL_REQUIRE(!mel_gpu_failed(csh.status));
    Mel_Gpu_Pipeline_Create_Result cpipe = mel_gpu_pipeline_compute_create(dev, .shader = csh.value, .name = "gen-verts");
    MEL_REQUIRE(!mel_gpu_failed(cpipe.status));

    Mel_Gpu_Shader gsh = mel_test_state_shader(dev);
    MEL_REQUIRE(!mel_gpu_handle_eq(gsh.slot, mel_gpu_handle_null()));
    const Mel_Gpu_Vertex_Element layout[] = {
        { .location = 0, .format = MEL_GPU_FORMAT_RGB32_FLOAT, .offset = offsetof(Tri_Vertex, pos) },
        { .location = 1, .format = MEL_GPU_FORMAT_RGBA32_FLOAT, .offset = offsetof(Tri_Vertex, color) },
    };
    Mel_Gpu_Pipeline_Create_Result gpipe = mel_gpu_pipeline_create(dev,
                                                                   .shader = gsh,
                                                                   .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                   .cull = MEL_GPU_CULL_NONE,
                                                                   .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                   .vertex_layout = layout,
                                                                   .vertex_layout_count = 2,
                                                                   .vertex_stride = sizeof(Tri_Vertex),
                                                                   .name = "c2g-draw");
    MEL_REQUIRE(!mel_gpu_failed(gpipe.status));

    Metal_Target          tgt = test_target_create(dev, 32, 32);
    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);

    mel_gpu_cmd_bind_pipeline(cmd, cpipe.value);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 1, vbo.value);
    mel_gpu_cmd_dispatch(cmd, 1, 1, 1);
    mel_gpu_cmd_buffer_barrier(cmd, vbo.value, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_VERTEX_BUFFER);

    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    Mel_Gpu_Color_Attachment  color = { .view = tgt.rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = tgt.w, .height = tgt.h);
    mel_gpu_cmd_bind_pipeline(cmd, gpipe.value);
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
    test_dump_ppm("metal_compute_to_graphics", px, tgt.w, tgt.h);

    const u8* center = px + ((usize)(tgt.h / 2) * tgt.w + tgt.w / 2) * 4;
    MEL_EXPECT(center[1] > 200);
    MEL_EXPECT(center[0] < 60);
    MEL_EXPECT(center[2] < 60);
    MEL_EXPECT_EQ(center[3], 255u);

    const u8* corner = px;
    MEL_EXPECT(corner[0] == 0 && corner[1] == 0 && corner[2] == 0);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, gpipe.value);
    mel_gpu_pipeline_destroy(dev, cpipe.value);
    mel_gpu_shader_destroy(dev, gsh);
    mel_gpu_shader_destroy(dev, csh.value);
    test_target_destroy(dev, &tgt);
    mel_gpu_buffer_destroy(dev, vbo.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

static Mel_Gpu_Device* test_make_device_bindless(Mel_Gpu_Instance** out_inst)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-metal-bindless-test", .debug = { .enabled = true });
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

MEL_TEST(metal_bindless, heap_caps_reported)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dev);
    MEL_REQUIRE(caps->memory.bindless.tier == MEL_GPU_TIER_FULL);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));
    MEL_EXPECT_EQ((int)caps->memory.bindless.binding_model, (int)MEL_GPU_BINDING_MODEL_ROOT_RECORD);
    MEL_EXPECT_EQ((int)caps->memory.bindless.root_record_payload, (int)MEL_GPU_ROOT_RECORD_PAYLOAD_MIXED);
    MEL_EXPECT(caps->memory.bindless.max_storage_buffer_slots > 0);
    MEL_EXPECT(caps->memory.bindless.max_sampler_slots > 0);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(metal_bindless, missing_feature_without_heap)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(!mel_gpu_bindless_available(dev));

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_compute_from_bytecode(dev, .target = MEL_GPU_SHADER_TARGET_MSL, .compute_blob = FILL_COMP_MSL, .compute_blob_size = sizeof FILL_COMP_MSL, .entry = "cs_main", .name = "no-heap");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_compute_create(dev, .shader = sh.value, .bindless = true, .name = "no-heap");
    MEL_EXPECT(mel_gpu_failed(pipe.status));
    MEL_EXPECT_EQ((int)pipe.status, (int)MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE);

    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

static const char BINDLESS_FILL_COMP_MSL[] = "#include <metal_stdlib>\n"
                                             "using namespace metal;\n"
                                             "struct BufSlot { device uint* p [[id(0)]]; };\n"
                                             "struct Root { uint count; uint n; uint slot[8]; };\n"
                                             "kernel void cs_main(constant Root& root [[buffer(0)]],\n"
                                             "                    device BufSlot* storage_heap [[buffer(3)]],\n"
                                             "                    uint gid [[thread_position_in_grid]]) {\n"
                                             "    if (gid >= root.n) return;\n"
                                             "    for (uint k = 0; k < root.count; ++k) {\n"
                                             "        device uint* dst = storage_heap[root.slot[k]].p;\n"
                                             "        dst[gid] = (k + 1u) * 1000u + gid * 3u + 7u;\n"
                                             "    }\n"
                                             "}\n";

MEL_TEST(metal_bindless, compute_storage_buffer)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    const u32 K = 4;
    const u32 N = 64;

    Mel_Gpu_Buffer bufs[4];
    u32            slots[4];
    for (u32 k = 0; k < K; k++)
    {
        Mel_Gpu_Buffer_Create_Result b = mel_gpu_buffer_create(dev, .size = (usize)N * sizeof(u32), .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_READBACK, .name = "bindless-out");
        MEL_REQUIRE(!mel_gpu_failed(b.status));
        bufs[k] = b.value;
        slots[k] = mel_gpu_buffer_bindless_slot(dev, b.value);
    }

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_compute_from_bytecode(dev,
                                                                                  .target = MEL_GPU_SHADER_TARGET_MSL,
                                                                                  .compute_blob = BINDLESS_FILL_COMP_MSL,
                                                                                  .compute_blob_size = sizeof BINDLESS_FILL_COMP_MSL,
                                                                                  .entry = "cs_main",
                                                                                  .name = "bindless-fill");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_compute_create(dev, .shader = sh.value, .bindless = true, .name = "bindless-fill");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    struct
    {
        u32 count;
        u32 n;
        u32 slot[8];
    } root = { K, N, { 0 } };
    for (u32 k = 0; k < K; k++)
        root.slot[k] = slots[k];

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_dispatch(cmd, (N + 31) / 32, 1, 1);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_wait(f)));
    mel_gpu_future_destroy(f);

    bool ok = true;
    for (u32 k = 0; k < K; k++)
    {
        const u32* out = mel_gpu_buffer_mapped(dev, bufs[k]);
        MEL_REQUIRE_NOT_NULL(out);
        for (u32 i = 0; i < N; i++)
            if (out[i] != (k + 1u) * 1000u + i * 3u + 7u)
                ok = false;
    }
    MEL_EXPECT(ok);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    for (u32 k = 0; k < K; k++)
        mel_gpu_buffer_destroy(dev, bufs[k]);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

static const char BINDLESS_BLIT_VERT_MSL[] = "#include <metal_stdlib>\n"
                                             "using namespace metal;\n"
                                             "struct VOut { float4 pos [[position]]; float2 uv; };\n"
                                             "vertex VOut vs_main(uint vid [[vertex_id]]) {\n"
                                             "    float2 p[3] = { float2(-1.0, -3.0), float2(-1.0, 1.0), float2(3.0, 1.0) };\n"
                                             "    float2 t[3] = { float2(0.0, 2.0), float2(0.0, 0.0), float2(2.0, 0.0) };\n"
                                             "    VOut o; o.pos = float4(p[vid], 0.0, 1.0); o.uv = t[vid]; return o;\n"
                                             "}\n";

static const char BINDLESS_BLIT_FRAG_MSL[] = "#include <metal_stdlib>\n"
                                             "using namespace metal;\n"
                                             "struct TexSlot { texture2d<float> t [[id(0)]]; };\n"
                                             "struct SmpSlot { sampler s [[id(0)]]; };\n"
                                             "struct Root { uint tex; uint smp; };\n"
                                             "struct VOut { float4 pos [[position]]; float2 uv; };\n"
                                             "fragment float4 fs_main(VOut in [[stage_in]],\n"
                                             "                        constant Root& root [[buffer(0)]],\n"
                                             "                        device const TexSlot* tex_heap [[buffer(1)]],\n"
                                             "                        device const SmpSlot* smp_heap [[buffer(2)]]) {\n"
                                             "    texture2d<float> tex = tex_heap[root.tex].t;\n"
                                             "    sampler smp = smp_heap[root.smp].s;\n"
                                             "    return tex.sample(smp, in.uv);\n"
                                             "}\n";

MEL_TEST(metal_bindless, sample_texture_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    const u32                     SRC = 8;
    Mel_Gpu_Texture_Create_Result src = mel_gpu_texture_create(dev,
                                                               .kind = MEL_GPU_TEXTURE_2D,
                                                               .extent = { SRC, SRC, 1 },
                                                               .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                               .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_ATTACHMENT,
                                                               .name = "bindless-src");
    MEL_REQUIRE(!mel_gpu_failed(src.status));
    Mel_Gpu_Texture_View_Create_Result src_view = mel_gpu_texture_default_view(dev, src.value);
    MEL_REQUIRE(!mel_gpu_failed(src_view.status));

    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev,
                                                               .min_filter = MEL_GPU_FILTER_NEAREST,
                                                               .mag_filter = MEL_GPU_FILTER_NEAREST,
                                                               .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE,
                                                               .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE,
                                                               .name = "bindless-nearest");
    MEL_REQUIRE(!mel_gpu_failed(smp.status));

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .target = MEL_GPU_SHADER_TARGET_MSL,
                                                                          .vertex_blob = BINDLESS_BLIT_VERT_MSL,
                                                                          .vertex_blob_size = sizeof BINDLESS_BLIT_VERT_MSL,
                                                                          .fragment_blob = BINDLESS_BLIT_FRAG_MSL,
                                                                          .fragment_blob_size = sizeof BINDLESS_BLIT_FRAG_MSL,
                                                                          .vertex_entry = "vs_main",
                                                                          .fragment_entry = "fs_main",
                                                                          .name = "bindless-blit");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev,
                                                                  .shader = sh.value,
                                                                  .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                  .cull = MEL_GPU_CULL_NONE,
                                                                  .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                  .push_constant_size = 8,
                                                                  .bindless = true,
                                                                  .name = "bindless-blit");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Metal_Target tgt = test_target_create(dev, 16, 16);

    struct
    {
        u32 tex;
        u32 smp;
    } root = { mel_gpu_texture_view_bindless_slot(dev, src_view.value), mel_gpu_sampler_bindless_slot(dev, smp.value) };

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);

    Mel_Gpu_Color_Attachment src_clear = { .view = src_view.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.2f, 0.4f, 0.6f, 1.0f) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &src_clear, .color_count = 1, .width = SRC, .height = SRC);
    mel_gpu_cmd_end_rendering(cmd);

    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    Mel_Gpu_Color_Attachment  color = { .view = tgt.rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = tgt.w, .height = tgt.h);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, tgt.rt, range, tgt.rb);
    mel_gpu_command_list_end(cmd);

    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_wait(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, tgt.rb);
    MEL_REQUIRE_NOT_NULL(px);
    test_dump_ppm("metal_bindless_sample", px, tgt.w, tgt.h);

    const u8* center = px + ((usize)(tgt.h / 2) * tgt.w + tgt.w / 2) * 4;
    MEL_EXPECT(center[0] >= 49 && center[0] <= 53);
    MEL_EXPECT(center[1] >= 100 && center[1] <= 104);
    MEL_EXPECT(center[2] >= 151 && center[2] <= 155);
    MEL_EXPECT_EQ(center[3], 255u);

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

#else

MEL_TEST(metal_render, skipped_without_metal) { MEL_SKIP("metal backend not selected (build with --gpu=metal)"); }

#endif
