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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Phase 3 shaders are authored as HLSL and compiled to DXIL at test time by spawning the in-box dxc.exe (on
// PATH under dev.cmd). This keeps the tests dependency-free at link time (no dxcompiler.dll / dxcapi.h on the
// in-box INCLUDE path) and exercises real signed SM 6.6 DXIL (dxil.dll co-located). The blob is malloc'd; the
// caller frees it after shader create (which copies it).
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

// D3D12 backend (gpu-rhi.md §12 M2 co-primary; design/gpu-d3d12.md). Phase 0 — device foundation: the
// toolchain (clang/MSVC ABI over the in-box d3d12.h in C), DXGI adapter enumeration, caps, and headless
// device create/destroy on the win-pilot RTX 2060. Phase 1 — queues, committed-resource buffers, the
// device timeline fence driving submit→future + deferred-free, and the QueryVideoMemoryInfo budget.
// Pixel/recording phases follow.

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
    // The RTX 2060 is a discrete, timestamp-capable, Tier-3 bindless device — confirm the device-level
    // refinement ran (these would all be zero/none if CheckFeatureSupport had not been consulted).
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

    // UPLOAD: host-visible, persistently mapped — the write lands at the mapped pointer.
    Mel_Gpu_Buffer_Create_Result up = mel_gpu_buffer_create(dev, .size = sizeof verts, .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD, .data = verts, .name = "upload-vbo");
    MEL_REQUIRE(!mel_gpu_failed(up.status));
    MEL_REQUIRE(mel_gpu_buffer_alive(dev, up.value));
    void* mapped = mel_gpu_buffer_mapped(dev, up.value);
    MEL_REQUIRE_NOT_NULL(mapped);
    MEL_EXPECT_FLOAT_EQ(((f32*)mapped)[3], 3.0f, 0.0001f);

    // DEVICE: device-local, populated through the transient staging copy + timeline-fence wait.
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

    // No reactor on this device ⇒ the synchronous submit path: signal the timeline fence and wait it out.
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

// U10 + U15 + U16 + U17 end-to-end: clear an offscreen render target, copy it to a readback buffer, and
// verify the cleared pixel on the CPU — first pixels on D3D12.
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
    MEL_EXPECT(px[0] >= 62 && px[0] <= 66);   // r = 0.25
    MEL_EXPECT(px[1] >= 126 && px[1] <= 130); // g = 0.5
    MEL_EXPECT(px[2] >= 189 && px[2] <= 193); // b = 0.75
    MEL_EXPECT_EQ(px[3], 255u);               // a = 1.0

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_texture_view_destroy(dev, v.value);
    mel_gpu_texture_destroy(dev, t.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

// U10 texture_write: upload a known pattern, copy it back, verify the round-trip. W=64 keeps the copy-to-
// buffer footprint row pitch 256-aligned and tight (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT).
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

// U17 buffer-state barrier records a debug-layer-clean transition end-to-end (UAV barriers on D3D12).
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

// ---- Phase 3: pipelines + bindless + reflection (SM 6.6 dynamic resources) ----

// A fullscreen triangle from SV_VertexID (no vertex input); passes a UV through.
static const char* VS_HLSL =
    "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "VSOut main(uint vid : SV_VertexID) {\n"
    "  VSOut o;\n"
    "  float2 p = float2((vid << 1) & 2, vid & 2);\n"
    "  o.uv = p;\n"
    "  o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "  return o;\n"
    "}\n";

// No-descriptor PS (the pipeline-create probe): paints UV.
static const char* SOLID_PS_HLSL =
    "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "float4 main(PSIn i) : SV_Target { return float4(i.uv, 0.0, 1.0); }\n";

// Bindless PS: sample a heap-resident texture through a heap-resident sampler, both addressed by index in the
// per-draw root record (the §6.7 D3D12 descriptor-indices payload).
static const char* SAMPLE_PS_HLSL =
    "cbuffer Root : register(b0) { uint g_tex; uint g_smp; }\n"
    "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "float4 main(PSIn i) : SV_Target {\n"
    "  Texture2D<float4> t = ResourceDescriptorHeap[g_tex];\n"
    "  SamplerState s = SamplerDescriptorHeap[g_smp];\n"
    "  return t.Sample(s, i.uv);\n"
    "}\n";

// Bindless compute: out[i] = in[i] + 1, both buffers addressed by heap slot in the root record.
static const char* ADD_CS_HLSL =
    "cbuffer Root : register(b0) { uint g_in; uint g_out; uint g_n; }\n"
    "[numthreads(64,1,1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) {\n"
    "  if (id.x >= g_n) return;\n"
    "  RWByteAddressBuffer ib = ResourceDescriptorHeap[g_in];\n"
    "  RWByteAddressBuffer ob = ResourceDescriptorHeap[g_out];\n"
    "  uint v = ib.Load(id.x * 4);\n"
    "  ob.Store(id.x * 4, v + 1);\n"
    "}\n";

// U14 caps: the device reports the D3D12 binding model — a root record carrying descriptor indices. This is
// the concrete co-primary contrast: Vulkan-BDA reports MIXED (textures/samplers as indices, buffers as
// pointers); D3D12 collapses to DESCRIPTOR_INDICES because buffers are descriptors even at the ceiling.
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

// U13: a graphics PSO + reflection-derived root signature compile and link from DXIL (no bindless, no input).
MEL_TEST(d3d12_pipeline, graphics_create)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    void *vs = NULL, *ps = NULL;
    usize vss = 0, pss = 0;
    MEL_REQUIRE(dxc_compile(VS_HLSL, "vs_6_6", &vs, &vss));
    MEL_REQUIRE(dxc_compile(SOLID_PS_HLSL, "ps_6_6", &ps, &pss));
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

// U14 graphics bindless end-to-end: a fullscreen triangle samples a heap-resident solid texture through a
// heap-resident sampler, the slots delivered in the root record; the cleared RT is read back and the sampled
// colour pixel-verified. The binding-model contrast made concrete on D3D12 (cf. the Vulkan sample test).
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
    MEL_REQUIRE(dxc_compile(VS_HLSL, "vs_6_6", &vs, &vss));
    MEL_REQUIRE(dxc_compile(SAMPLE_PS_HLSL, "ps_6_6", &ps, &pss));
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
    // The sampled texture is left in SHADER_RESOURCE by texture_write (regular textures don't decay), so it
    // needs no barrier here; only the RT transitions.
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
    const u8* c = px + (usize)32 * 256 + 32 * 4; // 64-wide RGBA8 readback => 256-byte rows; centre pixel
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

// U13/U14 compute bindless: out[i] = in[i] + 1, both storage buffers addressed purely by heap slot in the
// root record; the device-local result is copied to a readback buffer and verified (the heap class the
// graphics test cannot reach — the storage-buffer payload).
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
    MEL_REQUIRE(dxc_compile(ADD_CS_HLSL, "cs_6_6", &cs, &css));
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

    // Separate submission: out decays to COMMON, then the copy promotes it to COPY_SOURCE (D3D12 buffer
    // state model). A device-local UAV buffer cannot be host-mapped, so the result returns via the copy.
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

#endif
