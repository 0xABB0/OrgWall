#include <test/test.h>

#if MEL_GPU_D3D12

#include <gpu/device.h>
#include <gpu/caps.h>
#include <gpu/queue.h>
#include <gpu/buffer.h>
#include <gpu/memory.h>
#include <gpu/texture.h>
#include <gpu/command.h>
#include <gpu/rendering.h>
#include <gpu/state.h>
#include <gpu/shader.h>
#include <gpu/pipeline.h>
#include <gpu/sampler.h>
#include <gpu/binding.h>
#include <gpu/bind_group.h>
#include <gpu/format.h>
#include <gpu/format_props.h>
#include <gpu/surface.h>
#include <gpu/swapchain.h>

#include <allocator/heap.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool               mel_gpu__swapchain_readback_back(Mel_Gpu_Swapchain* sc, Mel_Gpu_Buffer dst);
Mel_Gpu_Swapchain* mel_gpu__swapchain_create_headless(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain_Opt opt);
u32                mel_gpu__dxil_reflect_test(const void* dxil, usize bytes, const Mel_Alloc* alloc, char (*semantics)[32], u32* sem_indices, i32* formats, u32* offsets, u32 max, u32* out_stride);
u32                mel_gpu__classic_res_in_use(Mel_Gpu_Device* dev);
u32                mel_gpu__classic_smp_in_use(Mel_Gpu_Device* dev);

static bool test_interactive_session(void)
{
    char    name[256] = { 0 };
    DWORD   len = 0;
    HWINSTA ws = GetProcessWindowStation();
    if (!GetUserObjectInformationA(ws, UOI_NAME, name, sizeof name, &len))
        return false;
    return strncmp(name, "WinSta0", 7) == 0;
}

static bool dxc_compile(const char* hlsl, const char* profile, void** out_blob, usize* out_size)
{
    const char* tmp = getenv("TEMP");
    if (!tmp)
        tmp = ".";
    char src_path[600], out_path[600], cmd[2048];
    snprintf(src_path, sizeof src_path, "%s\\mel_d3d12_%s.hlsl", tmp, profile);
    snprintf(out_path, sizeof out_path, "%s\\mel_d3d12_%s.cso", tmp, profile);

    FILE* f = fopen(src_path, "wb");
    if (!f)
        return false;
    fwrite(hlsl, 1, strlen(hlsl), f);
    fclose(f);

    snprintf(cmd, sizeof cmd, "dxc -T %s -E main -Fo \"%s\" \"%s\"", profile, out_path, src_path);
    if (system(cmd) != 0)
        return false;

    FILE* g = fopen(out_path, "rb");
    if (!g)
        return false;
    fseek(g, 0, SEEK_END);
    long n = ftell(g);
    fseek(g, 0, SEEK_SET);
    if (n <= 0)
    {
        fclose(g);
        return false;
    }
    void* buf = malloc((size_t)n);
    size_t rd = fread(buf, 1, (size_t)n, g);
    fclose(g);
    if (rd != (size_t)n)
    {
        free(buf);
        return false;
    }
    *out_blob = buf;
    *out_size = (usize)n;
    return true;
}

static Mel_Gpu_Device* test_make_device(Mel_Gpu_Instance** out_inst)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-d3d12-test", .debug = { .enabled = true });
    if (!inst)
        return NULL;
    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(inst, adapters, 8);
    if (n == 0)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    Mel_Gpu_Device_Create_Result dr = mel_gpu_device_create(inst, adapters[0], .features = { .descriptor_indexing = true });
    if (!dr.value)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    *out_inst = inst;
    return dr.value;
}

MEL_TEST(d3d12_device, instance_adapters_caps)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-d3d12-test", .debug = { .enabled = true });
    MEL_REQUIRE_NOT_NULL(inst);

    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(inst, adapters, 8);
    MEL_REQUIRE(n > 0);

    Mel_Gpu_Caps caps = mel_gpu_adapter_caps(adapters[0]);
    MEL_EXPECT(caps.adapter.name[0] != 0);
    MEL_EXPECT(caps.adapter.has_luid);
    MEL_EXPECT(caps.queries.timestamp_period_ns >= 0.0);

    mel_gpu_instance_destroy(inst);
}

MEL_TEST(d3d12_device, create_and_destroy_headless)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-d3d12-test", .debug = { .enabled = true, .thread_safety_tracker = true });
    MEL_REQUIRE_NOT_NULL(inst);

    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(inst, adapters, 8);
    MEL_REQUIRE(n > 0);

    Mel_Gpu_Device_Create_Result dr = mel_gpu_device_create(inst, adapters[0], .features = { .descriptor_indexing = true });
    MEL_REQUIRE(!mel_gpu_failed(dr.status));
    MEL_REQUIRE_NOT_NULL(dr.value);

    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dr.value);
    MEL_REQUIRE_NOT_NULL(caps);
    MEL_EXPECT(caps->adapter.name[0] != 0);
    MEL_EXPECT(caps->sampler.max_anisotropy >= 1.0f);
    MEL_EXPECT(caps->queries.timestamp == MEL_GPU_TIMESTAMP_NATIVE);
    MEL_EXPECT(caps->queries.timestamp_period_ns > 0.0);

    mel_gpu_device_destroy(dr.value);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(d3d12_alloc, buffer_upload_and_device)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const f32 verts[] = { 0, 1, 2, 3, 4, 5, 6, 7 };

    Mel_Gpu_Buffer_Create_Result up = mel_gpu_buffer_create(dev, .size = sizeof verts, .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD, .data = verts, .name = "upload-vbo");
    MEL_REQUIRE(!mel_gpu_failed(up.status));
    MEL_REQUIRE(mel_gpu_buffer_alive(dev, up.value));
    void* mapped = mel_gpu_buffer_mapped(dev, up.value);
    MEL_REQUIRE_NOT_NULL(mapped);
    MEL_EXPECT_FLOAT_EQ(((f32*)mapped)[3], 3.0f, 0.0001f);

    Mel_Gpu_Buffer_Create_Result dv = mel_gpu_buffer_create(dev, .size = sizeof verts, .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_DEVICE, .data = verts, .name = "device-vbo");
    MEL_REQUIRE(!mel_gpu_failed(dv.status));
    MEL_REQUIRE(mel_gpu_buffer_alive(dev, dv.value));

    mel_gpu_buffer_destroy(dev, up.value);
    mel_gpu_buffer_destroy(dev, dv.value);
    MEL_EXPECT(!mel_gpu_buffer_alive(dev, up.value));
    MEL_EXPECT(!mel_gpu_buffer_alive(dev, dv.value));

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(d3d12_queue, request_info_submit)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    MEL_EXPECT_EQ(mel_gpu_queue_available(dev, MEL_GPU_QUEUE_GRAPHICS, MEL_GPU_QUEUE_PRIORITY_NORMAL), 1u);
    MEL_EXPECT_EQ(mel_gpu_queue_available(dev, MEL_GPU_QUEUE_VIDEO_DECODE, MEL_GPU_QUEUE_PRIORITY_NORMAL), 0u);

    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    MEL_REQUIRE_NOT_NULL(q);
    Mel_Gpu_Queue_Info info = mel_gpu_queue_info(q);
    MEL_EXPECT(info.supports_graphics);
    MEL_EXPECT(info.timestamp_valid_bits > 0);

    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_list_count = 0 });
    MEL_REQUIRE_NOT_NULL(f);
    MEL_EXPECT(mel_gpu_future_resolved(f));
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    mel_gpu_queue_release(q);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(d3d12_residency, budget_and_caps)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dev);
    MEL_EXPECT(caps->memory.residency_control == MEL_GPU_RESIDENCY_BUDGET_ONLY);

    Mel_Gpu_Memory_Budget b = mel_gpu_memory_budget(dev);
    MEL_EXPECT(b.budget_bytes > 0);

    Mel_Gpu_Buffer_Create_Result buf = mel_gpu_buffer_create(dev, .size = 4096, .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_DEVICE, .name = "resident");
    MEL_REQUIRE(!mel_gpu_failed(buf.status));
    MEL_EXPECT(mel_gpu_warned(mel_gpu_buffer_make_resident(dev, buf.value)));
    MEL_EXPECT(mel_gpu_warned(mel_gpu_buffer_evict(dev, buf.value)));
    mel_gpu_buffer_destroy(dev, buf.value);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(d3d12_format, properties_honesty)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Format_Properties color = mel_gpu_format_properties(dev, MEL_GPU_FORMAT_RGBA8_UNORM, MEL_GPU_TILING_OPTIMAL);
    MEL_EXPECT(color.tiling_features & MEL_GPU_FMT_SAMPLED);
    MEL_EXPECT(color.tiling_features & MEL_GPU_FMT_LINEAR_FILTER);
    MEL_EXPECT(color.tiling_features & MEL_GPU_FMT_COLOR_ATTACHMENT);
    MEL_EXPECT(color.tiling_features & MEL_GPU_FMT_TRANSFER_SRC);
    MEL_EXPECT(color.tiling_features & MEL_GPU_FMT_TRANSFER_DST);
    MEL_EXPECT(color.tiling_features & MEL_GPU_FMT_BLIT_SRC);
    MEL_EXPECT(color.tiling_features & MEL_GPU_FMT_BLIT_DST);
    MEL_EXPECT_EQ(color.tiling_features & MEL_GPU_FMT_DEPTH_ATTACHMENT, 0u);
    MEL_EXPECT(color.sample_counts != 0u);

    Mel_Gpu_Format_Properties depth = mel_gpu_format_properties(dev, MEL_GPU_FORMAT_D32_FLOAT, MEL_GPU_TILING_OPTIMAL);
    MEL_EXPECT(depth.tiling_features & MEL_GPU_FMT_DEPTH_ATTACHMENT);
    MEL_EXPECT(depth.tiling_features & MEL_GPU_FMT_TRANSFER_SRC);
    MEL_EXPECT(depth.tiling_features & MEL_GPU_FMT_TRANSFER_DST);
    MEL_EXPECT_EQ(depth.tiling_features & MEL_GPU_FMT_COLOR_ATTACHMENT, 0u);
    MEL_EXPECT_EQ(depth.tiling_features & MEL_GPU_FMT_COLOR_BLEND, 0u);

    Mel_Gpu_Format_Properties vbuf = mel_gpu_format_properties(dev, MEL_GPU_FORMAT_RGB32_FLOAT, MEL_GPU_TILING_OPTIMAL);
    MEL_EXPECT(vbuf.buffer_features & MEL_GPU_FMT_VERTEX_BUFFER);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(d3d12_texture, create_view_and_alive)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Texture_Create_Result t = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { 64, 64, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                             .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_ATTACHMENT, .name = "tex");
    MEL_REQUIRE(!mel_gpu_failed(t.status));
    MEL_REQUIRE(mel_gpu_texture_alive(dev, t.value));

    Mel_Gpu_Texture_View_Create_Result v = mel_gpu_texture_default_view(dev, t.value);
    MEL_REQUIRE(!mel_gpu_failed(v.status));
    MEL_REQUIRE(mel_gpu_texture_view_alive(dev, v.value));

    mel_gpu_texture_view_destroy(dev, v.value);
    mel_gpu_texture_destroy(dev, t.value);
    MEL_EXPECT(!mel_gpu_texture_view_alive(dev, v.value));
    MEL_EXPECT(!mel_gpu_texture_alive(dev, t.value));

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(d3d12_render, offscreen_clear_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 W = 64, H = 64;

    Mel_Gpu_Texture_Create_Result t = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                             .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "rt");
    MEL_REQUIRE(!mel_gpu_failed(t.status));
    Mel_Gpu_Texture_View_Create_Result v = mel_gpu_texture_default_view(dev, t.value);
    MEL_REQUIRE(!mel_gpu_failed(v.status));

    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "readback");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));

    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    MEL_REQUIRE_NOT_NULL(q);

    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    MEL_REQUIRE_NOT_NULL(cmd);
    mel_gpu_command_list_begin(cmd);

    Mel_Gpu_Subresource_Range range = { .aspect = MEL_GPU_ASPECT_COLOR, .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 };
    mel_gpu_cmd_texture_barrier(cmd, t.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);

    Mel_Gpu_Color_Attachment color = { .view = v.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.25f, 0.5f, 0.75f, 1.0f) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = W, .height = H);
    mel_gpu_cmd_end_rendering(cmd);

    mel_gpu_cmd_texture_barrier(cmd, t.value, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, t.value, range, rb.value);

    mel_gpu_command_list_end(cmd);

    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_REQUIRE_NOT_NULL(f);
    MEL_EXPECT(mel_gpu_future_resolved(f));
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_REQUIRE_NOT_NULL(px);
    MEL_EXPECT(px[0] >= 62 && px[0] <= 66);
    MEL_EXPECT(px[1] >= 126 && px[1] <= 130);
    MEL_EXPECT(px[2] >= 189 && px[2] <= 193);
    MEL_EXPECT_EQ(px[3], 255u);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_texture_view_destroy(dev, v.value);
    mel_gpu_texture_destroy(dev, t.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(d3d12_texture, write_and_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 W = 64, H = 4;
    u8        src[W * H * 4];
    for (u32 i = 0; i < W * H * 4; i++)
        src[i] = (u8)(i * 3 + 1);

    Mel_Gpu_Texture_Create_Result t = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                             .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_SRC | MEL_GPU_TEXTURE_COPY_DST, .name = "wtex");
    MEL_REQUIRE(!mel_gpu_failed(t.status));

    Mel_Gpu_Texture_Region region = { .subresource = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 }, .offset = { 0, 0, 0 }, .extent = { W, H, 1 } };
    mel_gpu_texture_write(dev, t.value, region, src, sizeof src);

    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = sizeof src, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "wreadback");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, t.value, range, MEL_GPU_STATE_SHADER_RESOURCE, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, t.value, range, rb.value);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* got = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_REQUIRE_NOT_NULL(got);
    bool match = true;
    for (u32 i = 0; i < W * H * 4; i++)
        if (got[i] != src[i])
            match = false;
    MEL_EXPECT(match);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_texture_destroy(dev, t.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(d3d12_render, buffer_barrier_submits_clean)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Buffer_Create_Result b = mel_gpu_buffer_create(dev, .size = 1024, .usage = MEL_GPU_BUFFER_STORAGE | MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_DEVICE, .name = "ssbo");
    MEL_REQUIRE(!mel_gpu_failed(b.status));

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    mel_gpu_cmd_buffer_barrier(cmd, b.value, MEL_GPU_STATE_COPY_DEST, MEL_GPU_STATE_UNORDERED_ACCESS);
    mel_gpu_cmd_buffer_barrier(cmd, b.value, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_VERTEX_BUFFER);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_buffer_destroy(dev, b.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

static const char* VS_HLSL =
    "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "VSOut main(uint vid : SV_VertexID) {\n"
    "  VSOut o;\n"
    "  float2 p = float2((vid << 1) & 2, vid & 2);\n"
    "  o.uv = p;\n"
    "  o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "  return o;\n"
    "}\n";

static const char* SOLID_PS_HLSL =
    "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "float4 main(PSIn i) : SV_Target { return float4(i.uv, 0.0, 1.0); }\n";

static const char* SAMPLE_PS_HLSL =
    "Texture2D<float4> g_textures[] : register(t0, space0);\n"
    "SamplerState g_samplers[] : register(s0, space0);\n"
    "cbuffer Root : register(b0) { uint g_tex; uint g_smp; }\n"
    "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "float4 main(PSIn i) : SV_Target {\n"
    "  return g_textures[g_tex].Sample(g_samplers[g_smp], i.uv);\n"
    "}\n";

static const char* ADD_CS_HLSL =
    "RWByteAddressBuffer g_buffers[] : register(u0, space0);\n"
    "cbuffer Root : register(b0) { uint g_in; uint g_out; uint g_n; }\n"
    "[numthreads(64,1,1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) {\n"
    "  if (id.x >= g_n) return;\n"
    "  uint v = g_buffers[g_in].Load(id.x * 4);\n"
    "  g_buffers[g_out].Store(id.x * 4, v + 1);\n"
    "}\n";

static const char* IMGWRITE_CS_HLSL =
    "RWTexture2D<float4> g_images[] : register(u0, space1);\n"
    "cbuffer Root : register(b0) { uint g_img; uint g_w; uint g_h; }\n"
    "[numthreads(8,8,1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) {\n"
    "  if (id.x >= g_w || id.y >= g_h) return;\n"
    "  float2 uv = float2(id.xy) / float2(g_w, g_h);\n"
    "  g_images[g_img][id.xy] = float4(uv, 0.5, 1.0);\n"
    "}\n";

static const char* CLASSIC_PS_HLSL =
    "Texture2D<float4> g_tex : register(t0, space0);\n"
    "SamplerState g_smp : register(s0, space0);\n"
    "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "float4 main(PSIn i) : SV_Target { return g_tex.Sample(g_smp, i.uv); }\n";

static const char* REFLECT_VS_HLSL =
    "struct VSIn { float3 pos : POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };\n"
    "float4 main(VSIn i) : SV_Position { return float4(i.pos, 1.0) + i.col + float4(i.uv, 0, 0); }\n";

MEL_TEST(d3d12_bindless, binding_model_caps)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));
    MEL_EXPECT_EQ(caps->memory.bindless.tier, MEL_GPU_TIER_FULL);
    MEL_EXPECT_EQ(caps->memory.bindless.binding_model, MEL_GPU_BINDING_MODEL_ROOT_RECORD);
    MEL_EXPECT_EQ(caps->memory.bindless.root_record_payload, MEL_GPU_ROOT_RECORD_PAYLOAD_DESCRIPTOR_INDICES);
    MEL_EXPECT(caps->memory.bindless.max_texture_view_slots > 0);
    MEL_EXPECT(caps->memory.bindless.max_sampler_slots > 0);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(d3d12_pipeline, graphics_create)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    void *vs = NULL, *ps = NULL;
    usize vss = 0, pss = 0;
    MEL_REQUIRE(dxc_compile(VS_HLSL, "vs_6_0", &vs, &vss));
    MEL_REQUIRE(dxc_compile(SOLID_PS_HLSL, "ps_6_0", &ps, &pss));
    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev, .spirv_vertex = vs, .spirv_vertex_size = vss, .spirv_fragment = ps, .spirv_fragment_size = pss, .name = "solid");
    free(vs);
    free(ps);
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Pipeline_Create_Result p = mel_gpu_pipeline_create(dev, .shader = sh.value, .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .name = "solid-pso");
    MEL_REQUIRE(!mel_gpu_failed(p.status));
    MEL_REQUIRE(mel_gpu_pipeline_alive(dev, p.value));

    mel_gpu_pipeline_destroy(dev, p.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(d3d12_bindless, sample_texture_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 TW = 4, TH = 4;
    u8        texel[4 * 4 * 4];
    for (u32 i = 0; i < TW * TH; i++)
    {
        texel[i * 4 + 0] = 64;
        texel[i * 4 + 1] = 128;
        texel[i * 4 + 2] = 192;
        texel[i * 4 + 3] = 255;
    }
    Mel_Gpu_Texture_Create_Result tex = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { TW, TH, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_DST, .name = "srctex");
    MEL_REQUIRE(!mel_gpu_failed(tex.status));
    Mel_Gpu_Texture_Region region = { .subresource = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 }, .offset = { 0, 0, 0 }, .extent = { TW, TH, 1 } };
    mel_gpu_texture_write(dev, tex.value, region, texel, sizeof texel);
    Mel_Gpu_Texture_View_Create_Result view = mel_gpu_texture_default_view(dev, tex.value);
    MEL_REQUIRE(!mel_gpu_failed(view.status));
    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST, .mip_filter = MEL_GPU_MIPMAP_NEAREST, .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_w = MEL_GPU_WRAP_CLAMP_EDGE, .name = "point");
    MEL_REQUIRE(!mel_gpu_failed(smp.status));

    u32 tex_slot = mel_gpu_texture_view_bindless_slot(dev, view.value);
    u32 smp_slot = mel_gpu_sampler_bindless_slot(dev, smp.value);

    const u32                     W = 64, H = 64;
    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "rt");
    MEL_REQUIRE(!mel_gpu_failed(rt.status));
    Mel_Gpu_Texture_View_Create_Result rtv = mel_gpu_texture_default_view(dev, rt.value);
    MEL_REQUIRE(!mel_gpu_failed(rtv.status));
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));

    void *vs = NULL, *ps = NULL;
    usize vss = 0, pss = 0;
    MEL_REQUIRE(dxc_compile(VS_HLSL, "vs_6_0", &vs, &vss));
    MEL_REQUIRE(dxc_compile(SAMPLE_PS_HLSL, "ps_6_0", &ps, &pss));
    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev, .spirv_vertex = vs, .spirv_vertex_size = vss, .spirv_fragment = ps, .spirv_fragment_size = pss, .name = "sample");
    free(vs);
    free(ps);
    MEL_REQUIRE(!mel_gpu_failed(sh.status));
    Mel_Gpu_Pipeline_Create_Result pso = mel_gpu_pipeline_create(dev, .shader = sh.value, .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .push_constant_size = 8, .bindless = true, .name = "sample-pso");
    MEL_REQUIRE(!mel_gpu_failed(pso.status));

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment color = { .view = rtv.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.0f, 0.0f, 0.0f, 1.0f) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = W, .height = H);
    mel_gpu_cmd_bind_pipeline(cmd, pso.value);
    u32 root[2] = { tex_slot, smp_slot };
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, root);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, rt.value, range, rb.value);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_REQUIRE_NOT_NULL(px);
    const u8* c = px + (usize)32 * 256 + 32 * 4;
    MEL_EXPECT(c[0] >= 62 && c[0] <= 66);
    MEL_EXPECT(c[1] >= 126 && c[1] <= 130);
    MEL_EXPECT(c[2] >= 190 && c[2] <= 194);
    MEL_EXPECT_EQ(c[3], 255u);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_pipeline_destroy(dev, pso.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_sampler_destroy(dev, smp.value);
    mel_gpu_texture_view_destroy(dev, rtv.value);
    mel_gpu_texture_view_destroy(dev, view.value);
    mel_gpu_texture_destroy(dev, rt.value);
    mel_gpu_texture_destroy(dev, tex.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(d3d12_compute, storage_buffer_bindless)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 N = 64;
    u32       input[64];
    for (u32 i = 0; i < N; i++)
        input[i] = i;

    Mel_Gpu_Buffer_Create_Result in = mel_gpu_buffer_create(dev, .size = sizeof input, .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_DEVICE, .data = input, .name = "in");
    Mel_Gpu_Buffer_Create_Result out = mel_gpu_buffer_create(dev, .size = sizeof input, .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_DEVICE, .name = "out");
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = sizeof input, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb");
    MEL_REQUIRE(!mel_gpu_failed(in.status) && !mel_gpu_failed(out.status) && !mel_gpu_failed(rb.status));

    u32 in_slot = mel_gpu_buffer_bindless_slot(dev, in.value);
    u32 out_slot = mel_gpu_buffer_bindless_slot(dev, out.value);

    void* cs = NULL;
    usize css = 0;
    MEL_REQUIRE(dxc_compile(ADD_CS_HLSL, "cs_6_0", &cs, &css));
    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_compute_from_bytecode(dev, .spirv = cs, .spirv_size = css, .name = "add");
    free(cs);
    MEL_REQUIRE(!mel_gpu_failed(sh.status));
    Mel_Gpu_Pipeline_Create_Result pso = mel_gpu_pipeline_compute_create(dev, .shader = sh.value, .push_constant_size = 12, .bindless = true, .name = "add-pso");
    MEL_REQUIRE(!mel_gpu_failed(pso.status));

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);

    mel_gpu_command_list_begin(cmd);
    mel_gpu_cmd_bind_pipeline(cmd, pso.value);
    u32 root[3] = { in_slot, out_slot, N };
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, root);
    mel_gpu_cmd_dispatch(cmd, 1, 1, 1);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f1 = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f1)));
    mel_gpu_future_destroy(f1);

    mel_gpu_command_list_begin(cmd);
    mel_gpu_cmd_copy_buffer(cmd, out.value, rb.value, sizeof input);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f2 = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f2)));
    mel_gpu_future_destroy(f2);

    const u32* got = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_REQUIRE_NOT_NULL(got);
    bool ok = true;
    for (u32 i = 0; i < N; i++)
        if (got[i] != i + 1)
            ok = false;
    MEL_EXPECT(ok);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_buffer_destroy(dev, in.value);
    mel_gpu_buffer_destroy(dev, out.value);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_pipeline_destroy(dev, pso.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(d3d12_compute, storage_image_bindless)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    const u32 W = 8, H = 8;
    Mel_Gpu_Texture_Create_Result img = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                               .usage = MEL_GPU_TEXTURE_STORAGE | MEL_GPU_TEXTURE_COPY_SRC, .name = "img");
    MEL_REQUIRE(!mel_gpu_failed(img.status));
    Mel_Gpu_Texture_View_Create_Result view = mel_gpu_texture_default_view(dev, img.value);
    MEL_REQUIRE(!mel_gpu_failed(view.status));
    const u32                    ROW = 256;
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)ROW * H, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));

    u32 img_slot = mel_gpu_texture_view_bindless_slot(dev, view.value);

    void* cs = NULL;
    usize css = 0;
    MEL_REQUIRE(dxc_compile(IMGWRITE_CS_HLSL, "cs_6_0", &cs, &css));
    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_compute_from_bytecode(dev, .spirv = cs, .spirv_size = css, .name = "imgwrite");
    free(cs);
    MEL_REQUIRE(!mel_gpu_failed(sh.status));
    Mel_Gpu_Pipeline_Create_Result pso = mel_gpu_pipeline_compute_create(dev, .shader = sh.value, .push_constant_size = 12, .bindless = true, .name = "imgwrite-pso");
    MEL_REQUIRE(!mel_gpu_failed(pso.status));

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, img.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_UNORDERED_ACCESS);
    mel_gpu_cmd_bind_pipeline(cmd, pso.value);
    u32 root[3] = { img_slot, W, H };
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, root);
    mel_gpu_cmd_dispatch(cmd, 1, 1, 1);
    mel_gpu_cmd_texture_barrier(cmd, img.value, range, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, img.value, range, rb.value);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_REQUIRE_NOT_NULL(px);
    const u8* p00 = px;
    const u8* p44 = px + (usize)4 * ROW + 4 * 4;
    MEL_EXPECT_EQ(p00[0], 0u);
    MEL_EXPECT_EQ(p00[1], 0u);
    MEL_EXPECT(p00[2] >= 126 && p00[2] <= 130);
    MEL_EXPECT_EQ(p00[3], 255u);
    MEL_EXPECT(p44[0] >= 126 && p44[0] <= 130);
    MEL_EXPECT(p44[1] >= 126 && p44[1] <= 130);
    MEL_EXPECT(p44[2] >= 126 && p44[2] <= 130);
    MEL_EXPECT_EQ(p44[3], 255u);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_pipeline_destroy(dev, pso.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_texture_view_destroy(dev, view.value);
    mel_gpu_texture_destroy(dev, img.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

static Mel_Gpu_Device* test_make_device_classic(Mel_Gpu_Instance** out_inst)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-d3d12-test", .debug = { .enabled = true });
    if (!inst)
        return NULL;
    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(inst, adapters, 8);
    if (n == 0)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    Mel_Gpu_Device_Create_Result dr = mel_gpu_device_create(inst, adapters[0], .debug = { .enabled = true });
    if (!dr.value)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    *out_inst = inst;
    return dr.value;
}

MEL_TEST(d3d12_bind_group, classic_descriptor_set)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_classic(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(!mel_gpu_bindless_available(dev));

    const u32 TW = 4, TH = 4;
    u8        texel[4 * 4 * 4];
    for (u32 i = 0; i < TW * TH; i++)
    {
        texel[i * 4 + 0] = 70;
        texel[i * 4 + 1] = 140;
        texel[i * 4 + 2] = 210;
        texel[i * 4 + 3] = 255;
    }
    Mel_Gpu_Texture_Create_Result tex = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { TW, TH, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_DST, .name = "srctex");
    MEL_REQUIRE(!mel_gpu_failed(tex.status));
    Mel_Gpu_Texture_Region region = { .subresource = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 }, .offset = { 0, 0, 0 }, .extent = { TW, TH, 1 } };
    mel_gpu_texture_write(dev, tex.value, region, texel, sizeof texel);
    Mel_Gpu_Texture_View_Create_Result view = mel_gpu_texture_default_view(dev, tex.value);
    MEL_REQUIRE(!mel_gpu_failed(view.status));
    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST, .mip_filter = MEL_GPU_MIPMAP_NEAREST, .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_w = MEL_GPU_WRAP_CLAMP_EDGE, .name = "point");
    MEL_REQUIRE(!mel_gpu_failed(smp.status));

    const u32                     W = 64, H = 64;
    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "rt");
    MEL_REQUIRE(!mel_gpu_failed(rt.status));
    Mel_Gpu_Texture_View_Create_Result rtv = mel_gpu_texture_default_view(dev, rt.value);
    MEL_REQUIRE(!mel_gpu_failed(rtv.status));
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));

    void *vs = NULL, *ps = NULL;
    usize vss = 0, pss = 0;
    MEL_REQUIRE(dxc_compile(VS_HLSL, "vs_6_0", &vs, &vss));
    MEL_REQUIRE(dxc_compile(CLASSIC_PS_HLSL, "ps_6_0", &ps, &pss));
    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev, .spirv_vertex = vs, .spirv_vertex_size = vss, .spirv_fragment = ps, .spirv_fragment_size = pss, .name = "classic");
    free(vs);
    free(ps);
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Bind_Group_Layout_Entry entries[] = {
        { .binding = 0, .kind = MEL_GPU_DESCRIPTOR_SAMPLED_IMAGE, .count = 1 },
        { .binding = 0, .kind = MEL_GPU_DESCRIPTOR_SAMPLER, .count = 1 },
    };
    Mel_Gpu_Bind_Group_Layout bgl = mel_gpu_bind_group_layout_create(dev, entries, 2);
    MEL_REQUIRE(mel_gpu_bind_group_layout_alive(dev, bgl));

    Mel_Gpu_Pipeline_Create_Result pso = mel_gpu_pipeline_create(dev, .shader = sh.value, .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .set_layouts = &bgl, .set_layout_count = 1, .name = "classic-pso");
    MEL_REQUIRE(!mel_gpu_failed(pso.status));

    Mel_Gpu_Bind_Group bg = mel_gpu_bind_group_create(dev, bgl);
    MEL_REQUIRE(mel_gpu_bind_group_alive(dev, bg));
    mel_gpu_bind_group_write_texture(dev, bg, 0, 0, view.value);
    mel_gpu_bind_group_write_sampler(dev, bg, 0, 0, smp.value);

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment color = { .view = rtv.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.0f, 0.0f, 0.0f, 1.0f) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = W, .height = H);
    mel_gpu_cmd_bind_pipeline(cmd, pso.value);
    mel_gpu_cmd_bind_descriptor_set(cmd, 0, bg);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, rt.value, range, rb.value);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_REQUIRE_NOT_NULL(px);
    const u8* c = px + (usize)32 * 256 + 32 * 4;
    MEL_EXPECT(c[0] >= 68 && c[0] <= 72);
    MEL_EXPECT(c[1] >= 138 && c[1] <= 142);
    MEL_EXPECT(c[2] >= 208 && c[2] <= 212);
    MEL_EXPECT_EQ(c[3], 255u);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_bind_group_destroy(dev, bg);
    mel_gpu_pipeline_destroy(dev, pso.value);
    mel_gpu_bind_group_layout_destroy(dev, bgl);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_sampler_destroy(dev, smp.value);
    mel_gpu_texture_view_destroy(dev, rtv.value);
    mel_gpu_texture_view_destroy(dev, view.value);
    mel_gpu_texture_destroy(dev, rt.value);
    mel_gpu_texture_destroy(dev, tex.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(d3d12_bind_group, classic_slot_reclaim)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_classic(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(!mel_gpu_bindless_available(dev));

    u32 res_base = mel_gpu__classic_res_in_use(dev);
    u32 smp_base = mel_gpu__classic_smp_in_use(dev);

    Mel_Gpu_Bind_Group_Layout_Entry entries[] = {
        { .binding = 0, .kind = MEL_GPU_DESCRIPTOR_SAMPLED_IMAGE, .count = 2 },
        { .binding = 0, .kind = MEL_GPU_DESCRIPTOR_SAMPLER, .count = 1 },
    };
    Mel_Gpu_Bind_Group_Layout bgl = mel_gpu_bind_group_layout_create(dev, entries, 2);
    MEL_REQUIRE(mel_gpu_bind_group_layout_alive(dev, bgl));

    Mel_Gpu_Bind_Group a = mel_gpu_bind_group_create(dev, bgl);
    MEL_REQUIRE(mel_gpu_bind_group_alive(dev, a));
    MEL_EXPECT_EQ(mel_gpu__classic_res_in_use(dev), res_base + 2u);
    MEL_EXPECT_EQ(mel_gpu__classic_smp_in_use(dev), smp_base + 1u);

    mel_gpu_bind_group_destroy(dev, a);
    MEL_EXPECT_EQ(mel_gpu__classic_res_in_use(dev), res_base);
    MEL_EXPECT_EQ(mel_gpu__classic_smp_in_use(dev), smp_base);

    Mel_Gpu_Bind_Group b = mel_gpu_bind_group_create(dev, bgl);
    MEL_REQUIRE(mel_gpu_bind_group_alive(dev, b));
    MEL_EXPECT_EQ(mel_gpu__classic_res_in_use(dev), res_base + 2u);
    MEL_EXPECT_EQ(mel_gpu__classic_smp_in_use(dev), smp_base + 1u);

    mel_gpu_bind_group_destroy(dev, b);
    MEL_EXPECT_EQ(mel_gpu__classic_res_in_use(dev), res_base);
    MEL_EXPECT_EQ(mel_gpu__classic_smp_in_use(dev), smp_base);

    mel_gpu_bind_group_layout_destroy(dev, bgl);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(d3d12_bind_group, classic_churn_under_submission)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_classic(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(!mel_gpu_bindless_available(dev));

    const u32 TW = 4, TH = 4;
    u8        texel[4 * 4 * 4];
    for (u32 i = 0; i < TW * TH; i++)
    {
        texel[i * 4 + 0] = 70;
        texel[i * 4 + 1] = 140;
        texel[i * 4 + 2] = 210;
        texel[i * 4 + 3] = 255;
    }
    Mel_Gpu_Texture_Create_Result tex = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { TW, TH, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_DST, .name = "srctex");
    MEL_REQUIRE(!mel_gpu_failed(tex.status));
    Mel_Gpu_Texture_Region region = { .subresource = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 }, .offset = { 0, 0, 0 }, .extent = { TW, TH, 1 } };
    mel_gpu_texture_write(dev, tex.value, region, texel, sizeof texel);
    Mel_Gpu_Texture_View_Create_Result view = mel_gpu_texture_default_view(dev, tex.value);
    MEL_REQUIRE(!mel_gpu_failed(view.status));
    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST, .mip_filter = MEL_GPU_MIPMAP_NEAREST, .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_w = MEL_GPU_WRAP_CLAMP_EDGE, .name = "point");
    MEL_REQUIRE(!mel_gpu_failed(smp.status));

    const u32                     W = 64, H = 64;
    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "rt");
    MEL_REQUIRE(!mel_gpu_failed(rt.status));
    Mel_Gpu_Texture_View_Create_Result rtv = mel_gpu_texture_default_view(dev, rt.value);
    MEL_REQUIRE(!mel_gpu_failed(rtv.status));
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));

    void *vs = NULL, *ps = NULL;
    usize vss = 0, pss = 0;
    MEL_REQUIRE(dxc_compile(VS_HLSL, "vs_6_0", &vs, &vss));
    MEL_REQUIRE(dxc_compile(CLASSIC_PS_HLSL, "ps_6_0", &ps, &pss));
    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev, .spirv_vertex = vs, .spirv_vertex_size = vss, .spirv_fragment = ps, .spirv_fragment_size = pss, .name = "classic");
    free(vs);
    free(ps);
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Bind_Group_Layout_Entry entries[] = {
        { .binding = 0, .kind = MEL_GPU_DESCRIPTOR_SAMPLED_IMAGE, .count = 1 },
        { .binding = 0, .kind = MEL_GPU_DESCRIPTOR_SAMPLER, .count = 1 },
    };
    Mel_Gpu_Bind_Group_Layout bgl = mel_gpu_bind_group_layout_create(dev, entries, 2);
    MEL_REQUIRE(mel_gpu_bind_group_layout_alive(dev, bgl));

    Mel_Gpu_Pipeline_Create_Result pso = mel_gpu_pipeline_create(dev, .shader = sh.value, .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .set_layouts = &bgl, .set_layout_count = 1, .name = "classic-pso");
    MEL_REQUIRE(!mel_gpu_failed(pso.status));

    u32 res_base = mel_gpu__classic_res_in_use(dev);
    u32 smp_base = mel_gpu__classic_smp_in_use(dev);

    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    MEL_REQUIRE_NOT_NULL(q);

    const u32 CYCLES = 8192;
    u8        last0 = 0, last1 = 0, last2 = 0, last3 = 0;
    bool      ok = true;
    for (u32 cycle = 0; cycle < CYCLES; cycle++)
    {
        Mel_Gpu_Bind_Group bg = mel_gpu_bind_group_create(dev, bgl);
        if (!mel_gpu_bind_group_alive(dev, bg))
        {
            ok = false;
            break;
        }
        mel_gpu_bind_group_write_texture(dev, bg, 0, 0, view.value);
        mel_gpu_bind_group_write_sampler(dev, bg, 0, 0, smp.value);

        Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
        mel_gpu_command_list_begin(cmd);
        Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
        mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
        Mel_Gpu_Color_Attachment color = { .view = rtv.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.0f, 0.0f, 0.0f, 1.0f) };
        mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = W, .height = H);
        mel_gpu_cmd_bind_pipeline(cmd, pso.value);
        mel_gpu_cmd_bind_descriptor_set(cmd, 0, bg);
        mel_gpu_cmd_draw(cmd, 3, 1);
        mel_gpu_cmd_end_rendering(cmd);
        mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
        mel_gpu_cmd_copy_texture_to_buffer(cmd, rt.value, range, rb.value);
        mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_COPY_SOURCE, MEL_GPU_STATE_COMMON);
        mel_gpu_command_list_end(cmd);
        Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
        if (!mel_gpu_ok(mel_gpu_future_status(f)))
            ok = false;
        mel_gpu_future_destroy(f);

        const u8* px = mel_gpu_buffer_mapped(dev, rb.value);
        const u8* c = px + (usize)32 * 256 + 32 * 4;
        last0 = c[0];
        last1 = c[1];
        last2 = c[2];
        last3 = c[3];

        mel_gpu_bind_group_destroy(dev, bg);
        mel_gpu_command_list_destroy(cmd);

        if (mel_gpu__classic_res_in_use(dev) > res_base || mel_gpu__classic_smp_in_use(dev) > smp_base)
        {
            ok = false;
            break;
        }
    }

    MEL_EXPECT(ok);
    MEL_EXPECT_EQ(mel_gpu__classic_res_in_use(dev), res_base);
    MEL_EXPECT_EQ(mel_gpu__classic_smp_in_use(dev), smp_base);
    MEL_EXPECT(last0 >= 68 && last0 <= 72);
    MEL_EXPECT(last1 >= 138 && last1 <= 142);
    MEL_EXPECT(last2 >= 208 && last2 <= 212);
    MEL_EXPECT_EQ(last3, 255u);

    mel_gpu_queue_release(q);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_pipeline_destroy(dev, pso.value);
    mel_gpu_bind_group_layout_destroy(dev, bgl);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_sampler_destroy(dev, smp.value);
    mel_gpu_texture_view_destroy(dev, rtv.value);
    mel_gpu_texture_view_destroy(dev, view.value);
    mel_gpu_texture_destroy(dev, rt.value);
    mel_gpu_texture_destroy(dev, tex.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(d3d12_bind_group, classic_fragmentation_coalesce)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_classic(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(!mel_gpu_bindless_available(dev));

    u32 res_base = mel_gpu__classic_res_in_use(dev);

    Mel_Gpu_Bind_Group_Layout_Entry e1[] = { { .binding = 0, .kind = MEL_GPU_DESCRIPTOR_SAMPLED_IMAGE, .count = 1 } };
    Mel_Gpu_Bind_Group_Layout_Entry e4[] = { { .binding = 0, .kind = MEL_GPU_DESCRIPTOR_SAMPLED_IMAGE, .count = 4 } };
    Mel_Gpu_Bind_Group_Layout       small = mel_gpu_bind_group_layout_create(dev, e1, 1);
    Mel_Gpu_Bind_Group_Layout       big = mel_gpu_bind_group_layout_create(dev, e4, 1);

    Mel_Gpu_Bind_Group a = mel_gpu_bind_group_create(dev, small);
    Mel_Gpu_Bind_Group b = mel_gpu_bind_group_create(dev, small);
    Mel_Gpu_Bind_Group c = mel_gpu_bind_group_create(dev, small);
    Mel_Gpu_Bind_Group d = mel_gpu_bind_group_create(dev, small);
    MEL_REQUIRE(mel_gpu_bind_group_alive(dev, a) && mel_gpu_bind_group_alive(dev, b) && mel_gpu_bind_group_alive(dev, c) && mel_gpu_bind_group_alive(dev, d));
    MEL_EXPECT_EQ(mel_gpu__classic_res_in_use(dev), res_base + 4u);

    mel_gpu_bind_group_destroy(dev, b);
    mel_gpu_bind_group_destroy(dev, c);
    MEL_EXPECT_EQ(mel_gpu__classic_res_in_use(dev), res_base + 2u);

    Mel_Gpu_Bind_Group big_one = mel_gpu_bind_group_create(dev, big);
    MEL_REQUIRE(mel_gpu_bind_group_alive(dev, big_one));
    MEL_EXPECT_EQ(mel_gpu__classic_res_in_use(dev), res_base + 6u);

    mel_gpu_bind_group_destroy(dev, a);
    mel_gpu_bind_group_destroy(dev, d);
    mel_gpu_bind_group_destroy(dev, big_one);
    MEL_EXPECT_EQ(mel_gpu__classic_res_in_use(dev), res_base);

    mel_gpu_bind_group_layout_destroy(dev, small);
    mel_gpu_bind_group_layout_destroy(dev, big);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

static const char* CLASSIC_CB_PS_HLSL =
    "Texture2D<float4> g_tex : register(t0, space0);\n"
    "SamplerState g_smp : register(s0, space0);\n"
    "cbuffer Tint : register(b0, space0) { float4 g_tint; }\n"
    "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "float4 main(PSIn i) : SV_Target { return g_tex.Sample(g_smp, i.uv) * g_tint; }\n";

MEL_TEST(d3d12_bind_group, classic_uniform_buffer)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_classic(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(!mel_gpu_bindless_available(dev));

    const u32 TW = 4, TH = 4;
    u8        texel[4 * 4 * 4];
    for (u32 i = 0; i < TW * TH; i++)
    {
        texel[i * 4 + 0] = 200;
        texel[i * 4 + 1] = 200;
        texel[i * 4 + 2] = 200;
        texel[i * 4 + 3] = 255;
    }
    Mel_Gpu_Texture_Create_Result tex = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { TW, TH, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_DST, .name = "srctex");
    MEL_REQUIRE(!mel_gpu_failed(tex.status));
    Mel_Gpu_Texture_Region region = { .subresource = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 }, .offset = { 0, 0, 0 }, .extent = { TW, TH, 1 } };
    mel_gpu_texture_write(dev, tex.value, region, texel, sizeof texel);
    Mel_Gpu_Texture_View_Create_Result view = mel_gpu_texture_default_view(dev, tex.value);
    MEL_REQUIRE(!mel_gpu_failed(view.status));
    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST, .mip_filter = MEL_GPU_MIPMAP_NEAREST, .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_w = MEL_GPU_WRAP_CLAMP_EDGE, .name = "point");
    MEL_REQUIRE(!mel_gpu_failed(smp.status));

    const f32                    tint[4] = { 0.5f, 0.25f, 1.0f, 1.0f };
    Mel_Gpu_Buffer_Create_Result cb = mel_gpu_buffer_create(dev, .size = sizeof tint, .usage = MEL_GPU_BUFFER_UNIFORM, .memory = MEL_GPU_MEMORY_UPLOAD, .data = tint, .name = "tint");
    MEL_REQUIRE(!mel_gpu_failed(cb.status));

    const u32                     W = 64, H = 64;
    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "rt");
    MEL_REQUIRE(!mel_gpu_failed(rt.status));
    Mel_Gpu_Texture_View_Create_Result rtv = mel_gpu_texture_default_view(dev, rt.value);
    MEL_REQUIRE(!mel_gpu_failed(rtv.status));
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));

    void *vs = NULL, *ps = NULL;
    usize vss = 0, pss = 0;
    MEL_REQUIRE(dxc_compile(VS_HLSL, "vs_6_0", &vs, &vss));
    MEL_REQUIRE(dxc_compile(CLASSIC_CB_PS_HLSL, "ps_6_0", &ps, &pss));
    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev, .spirv_vertex = vs, .spirv_vertex_size = vss, .spirv_fragment = ps, .spirv_fragment_size = pss, .name = "classic-cb");
    free(vs);
    free(ps);
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Bind_Group_Layout_Entry entries[] = {
        { .binding = 0, .kind = MEL_GPU_DESCRIPTOR_SAMPLED_IMAGE, .count = 1 },
        { .binding = 0, .kind = MEL_GPU_DESCRIPTOR_UNIFORM_BUFFER, .count = 1 },
        { .binding = 0, .kind = MEL_GPU_DESCRIPTOR_SAMPLER, .count = 1 },
    };
    Mel_Gpu_Bind_Group_Layout bgl = mel_gpu_bind_group_layout_create(dev, entries, 3);
    MEL_REQUIRE(mel_gpu_bind_group_layout_alive(dev, bgl));

    Mel_Gpu_Pipeline_Create_Result pso = mel_gpu_pipeline_create(dev, .shader = sh.value, .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .set_layouts = &bgl, .set_layout_count = 1, .name = "classic-cb-pso");
    MEL_REQUIRE(!mel_gpu_failed(pso.status));

    Mel_Gpu_Bind_Group bg = mel_gpu_bind_group_create(dev, bgl);
    MEL_REQUIRE(mel_gpu_bind_group_alive(dev, bg));
    mel_gpu_bind_group_write_texture(dev, bg, 0, 0, view.value);
    mel_gpu_bind_group_write_buffer(dev, bg, 0, 0, cb.value);
    mel_gpu_bind_group_write_sampler(dev, bg, 0, 0, smp.value);

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment color = { .view = rtv.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.0f, 0.0f, 0.0f, 1.0f) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = W, .height = H);
    mel_gpu_cmd_bind_pipeline(cmd, pso.value);
    mel_gpu_cmd_bind_descriptor_set(cmd, 0, bg);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, rt.value, range, rb.value);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_REQUIRE_NOT_NULL(px);
    const u8* c = px + (usize)32 * 256 + 32 * 4;
    MEL_EXPECT(c[0] >= 98 && c[0] <= 102);
    MEL_EXPECT(c[1] >= 48 && c[1] <= 52);
    MEL_EXPECT(c[2] >= 198 && c[2] <= 202);
    MEL_EXPECT_EQ(c[3], 255u);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_bind_group_destroy(dev, bg);
    mel_gpu_pipeline_destroy(dev, pso.value);
    mel_gpu_bind_group_layout_destroy(dev, bgl);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_buffer_destroy(dev, cb.value);
    mel_gpu_sampler_destroy(dev, smp.value);
    mel_gpu_texture_view_destroy(dev, rtv.value);
    mel_gpu_texture_view_destroy(dev, view.value);
    mel_gpu_texture_destroy(dev, rt.value);
    mel_gpu_texture_destroy(dev, tex.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

static const char* REFLECT_INSTANCED_VS_HLSL =
    "struct VSIn { float2 pos : POSITION; float4 inst : TEXCOORD3; };\n"
    "float4 main(VSIn i) : SV_Position { return float4(i.pos, 0, 1) + i.inst; }\n";

MEL_TEST(d3d12_reflect, input_signature_indexed_semantic)
{
    void* vs = NULL;
    usize vss = 0;
    MEL_REQUIRE(dxc_compile(REFLECT_INSTANCED_VS_HLSL, "vs_6_0", &vs, &vss));

    char semantics[8][32];
    u32  sem_indices[8];
    i32  formats[8];
    u32  offsets[8];
    u32  stride = 0;
    u32  count = mel_gpu__dxil_reflect_test(vs, vss, mel_alloc_heap(), semantics, sem_indices, formats, offsets, 8, &stride);
    free(vs);

    MEL_REQUIRE_EQ(count, 2u);

    MEL_EXPECT(strcmp(semantics[0], "POSITION") == 0);
    MEL_EXPECT_EQ(sem_indices[0], 0u);
    MEL_EXPECT_EQ(formats[0], (i32)MEL_GPU_FORMAT_RG32_FLOAT);
    MEL_EXPECT_EQ(offsets[0], 0u);

    MEL_EXPECT(strcmp(semantics[1], "TEXCOORD") == 0);
    MEL_EXPECT_EQ(sem_indices[1], 3u);
    MEL_EXPECT_EQ(formats[1], (i32)MEL_GPU_FORMAT_RGBA32_FLOAT);
    MEL_EXPECT_EQ(offsets[1], 8u);

    MEL_EXPECT_EQ(stride, 24u);
}

MEL_TEST(d3d12_reflect, input_signature)
{
    void* vs = NULL;
    usize vss = 0;
    MEL_REQUIRE(dxc_compile(REFLECT_VS_HLSL, "vs_6_0", &vs, &vss));

    char semantics[8][32];
    u32  sem_indices[8];
    i32  formats[8];
    u32  offsets[8];
    u32  stride = 0;
    u32  count = mel_gpu__dxil_reflect_test(vs, vss, mel_alloc_heap(), semantics, sem_indices, formats, offsets, 8, &stride);
    free(vs);

    MEL_REQUIRE_EQ(count, 3u);

    MEL_EXPECT(strcmp(semantics[0], "POSITION") == 0);
    MEL_EXPECT_EQ(sem_indices[0], 0u);
    MEL_EXPECT_EQ(formats[0], (i32)MEL_GPU_FORMAT_RGB32_FLOAT);
    MEL_EXPECT_EQ(offsets[0], 0u);

    MEL_EXPECT(strcmp(semantics[1], "COLOR") == 0);
    MEL_EXPECT_EQ(sem_indices[1], 0u);
    MEL_EXPECT_EQ(formats[1], (i32)MEL_GPU_FORMAT_RGBA32_FLOAT);
    MEL_EXPECT_EQ(offsets[1], 12u);

    MEL_EXPECT(strcmp(semantics[2], "TEXCOORD") == 0);
    MEL_EXPECT_EQ(sem_indices[2], 0u);
    MEL_EXPECT_EQ(formats[2], (i32)MEL_GPU_FORMAT_RG32_FLOAT);
    MEL_EXPECT_EQ(offsets[2], 28u);

    MEL_EXPECT_EQ(stride, 36u);
}

static HWND test_make_hidden_window(u32 w, u32 h)
{
    HINSTANCE   hinst = GetModuleHandleW(NULL);
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = DefWindowProcW, .hInstance = hinst, .lpszClassName = L"MelD3D12SwapchainTest" };
    RegisterClassExW(&wc);
    return CreateWindowExW(0, L"MelD3D12SwapchainTest", L"mel-d3d12-swapchain", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, (int)w, (int)h, NULL, NULL, hinst, NULL);
}

static void test_present_clear(Mel_Gpu_Swapchain* sc, Mel_Gpu_Color clear)
{
    mel_gpu_frame_begin(sc);
    Mel_Gpu_Command_List* cmd = mel_gpu_frame_commands(sc);
    mel_gpu_cmd_begin_pass(cmd, clear);
    mel_gpu_cmd_end_pass(cmd);
    mel_gpu_frame_end(sc);
}

MEL_TEST(d3d12_swapchain, present_clear_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 W = 64, H = 64;

    bool               interactive = test_interactive_session();
    HWND               hwnd = NULL;
    Mel_Gpu_Surface*   surf = NULL;
    Mel_Gpu_Swapchain* sc = NULL;
    if (interactive)
    {
        hwnd = test_make_hidden_window(W, H);
        MEL_REQUIRE_NOT_NULL(hwnd);
        surf = mel_gpu_surface_create(dev, hwnd);
        MEL_REQUIRE_NOT_NULL(surf);
        sc = mel_gpu_swapchain_create(dev, .surface = surf, .width = (i32)W, .height = (i32)H, .format = MEL_GPU_FORMAT_BGRA8_UNORM, .vsync = false);
    }
    else
    {
        surf = mel_gpu_surface_create(dev, (void*)dev);
        sc = mel_gpu__swapchain_create_headless(dev, (Mel_Gpu_Swapchain_Opt){ .surface = surf, .width = (i32)W, .height = (i32)H, .format = MEL_GPU_FORMAT_BGRA8_UNORM, .vsync = false });
    }
    if (!sc)
    {
        mel_gpu_surface_destroy(surf);
        if (hwnd)
            DestroyWindow(hwnd);
        mel_gpu_device_destroy(dev);
        mel_gpu_instance_destroy(inst);
        MEL_SKIP("DXGI swapchain unavailable: non-interactive (service) window station has no DWM/desktop; "
                 "the flip-model present path is verifiable only on an interactive session");
    }
    MEL_EXPECT_EQ(mel_gpu_swapchain_format(sc), MEL_GPU_FORMAT_BGRA8_UNORM);

    Mel_Gpu_Color clear = mel_gpu_rgba(0.25f, 0.5f, 0.75f, 1.0f);
    test_present_clear(sc, clear);
    test_present_clear(sc, clear);

    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "sc-readback");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));
    MEL_REQUIRE(mel_gpu__swapchain_readback_back(sc, rb.value));

    const u8* px = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_REQUIRE_NOT_NULL(px);
    MEL_EXPECT(px[0] >= 189 && px[0] <= 193);
    MEL_EXPECT(px[1] >= 126 && px[1] <= 130);
    MEL_EXPECT(px[2] >= 62 && px[2] <= 66);
    MEL_EXPECT_EQ(px[3], 255u);

    mel_gpu_swapchain_resize(sc, 96, 48);
    test_present_clear(sc, mel_gpu_rgba(0.0f, 1.0f, 0.0f, 1.0f));

    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_swapchain_destroy(sc);
    mel_gpu_surface_destroy(surf);
    if (hwnd)
        DestroyWindow(hwnd);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

#else

MEL_TEST(d3d12_gating, backend_not_selected)
{
    MEL_SKIP("gpu-d3d12 was built without the d3d12 backend; the entire suite compiled to no tests. "
             "Re-run with the d3d12 backend selected: nob test gpu-d3d12 --gpu=d3d12");
}

#endif
