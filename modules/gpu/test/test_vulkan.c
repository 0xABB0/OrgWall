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
#include <gpu/query.h>
#include <gpu/swapchain.h>
#include <gpu/state.h>
#include <gpu/memory.h>
#include <gpu/format_props.h>
#include <gpu/vulkan/interop.h>
#include <gpu/threading.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <thread/thread.h>
#include <thread/barrier.h>

#include <stdatomic.h>

#include "bindless_spv.h"

static Mel_Gpu_Device* test_make_device(Mel_Gpu_Instance** out_inst)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-vulkan-test", .debug = { .enabled = true });
    if (!inst)
        return NULL;
    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(inst, adapters, 8);
    if (n == 0)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    Mel_Gpu_Device_Create_Result dr = mel_gpu_device_create(inst, adapters[0], .features = { .timeline_semaphores = true });
    if (!dr.value)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    *out_inst = inst;
    return dr.value;
}

static Mel_Gpu_Device* test_make_device_bindless(Mel_Gpu_Instance** out_inst)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-vulkan-test", .debug = { .enabled = true });
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

static Mel_Gpu_Device* test_make_device_bda(Mel_Gpu_Instance** out_inst)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-vulkan-test", .debug = { .enabled = true });
    if (!inst)
        return NULL;
    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(inst, adapters, 8);
    if (n == 0)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    Mel_Gpu_Device_Create_Result dr = mel_gpu_device_create(inst, adapters[0], .features = { .timeline_semaphores = true, .descriptor_indexing = true, .buffer_device_address = true });
    if (!dr.value)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    *out_inst = inst;
    return dr.value;
}

MEL_TEST(vk_device, instance_adapters_caps)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-vulkan-test", .debug = { .enabled = true });
    MEL_REQUIRE_NOT_NULL(inst);

    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(inst, adapters, 8);
    MEL_REQUIRE(n > 0);

    Mel_Gpu_Caps caps = mel_gpu_adapter_caps(adapters[0]);
    MEL_EXPECT(caps.adapter.name[0] != 0);
    MEL_EXPECT(caps.queries.timestamp_period_ns >= 0.0);

    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_device, create_and_destroy_headless)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-vulkan-test", .debug = { .enabled = true, .thread_safety_tracker = true });
    MEL_REQUIRE_NOT_NULL(inst);

    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(inst, adapters, 8);
    MEL_REQUIRE(n > 0);

    Mel_Gpu_Device_Create_Result dr = mel_gpu_device_create(inst, adapters[0], .features = { .timeline_semaphores = true });
    MEL_REQUIRE(!mel_gpu_failed(dr.status));
    MEL_REQUIRE_NOT_NULL(dr.value);

    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dr.value);
    MEL_REQUIRE_NOT_NULL(caps);
    MEL_EXPECT(caps->adapter.name[0] != 0);
    MEL_EXPECT(caps->sampler.max_anisotropy >= 1.0f);

    mel_gpu_device_destroy(dr.value);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_alloc, buffer_upload_and_device)
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

MEL_TEST(vk_queue, request_info_submit)
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

    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_list_count = 0 });
    MEL_REQUIRE_NOT_NULL(f);
    MEL_EXPECT(mel_gpu_future_resolved(f));
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    mel_gpu_queue_release(q);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_format, color_attachment_supported)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Format_Properties fp = mel_gpu_format_properties(dev, MEL_GPU_FORMAT_BGRA8_UNORM, MEL_GPU_TILING_OPTIMAL);
    MEL_EXPECT(fp.tiling_features & MEL_GPU_FMT_COLOR_ATTACHMENT);
    MEL_EXPECT(fp.tiling_features & MEL_GPU_FMT_SAMPLED);
    MEL_EXPECT(fp.sample_counts != 0);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_residency, budget_and_caps)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dev);
    MEL_EXPECT(caps->memory.residency_control == MEL_GPU_RESIDENCY_NONE || caps->memory.residency_control == MEL_GPU_RESIDENCY_BUDGET_ONLY);

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

MEL_TEST(vk_interop, buffer_import_borrowed)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    MEL_REQUIRE(mel_gpu_vk_device(dev) != VK_NULL_HANDLE);
    MEL_REQUIRE(mel_gpu_vk_physical_device(dev) != VK_NULL_HANDLE);

    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 256, .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VkBuffer           native = VK_NULL_HANDLE;
    MEL_REQUIRE(vkCreateBuffer(mel_gpu_vk_device(dev), &bci, NULL, &native) == VK_SUCCESS);

    Mel_Gpu_Buffer imported = mel_gpu_buffer_import(dev, native, 256, "borrowed");
    MEL_REQUIRE(mel_gpu_buffer_alive(dev, imported));
    MEL_EXPECT(mel_gpu_vk_buffer(dev, imported) == native);

    mel_gpu_buffer_destroy(dev, imported);
    MEL_EXPECT(!mel_gpu_buffer_alive(dev, imported));

    vkDestroyBuffer(mel_gpu_vk_device(dev), native, NULL);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_power, caps_populated)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dev);
    MEL_EXPECT(caps->power.power_source <= MEL_GPU_POWER_SOURCE_BATTERY);
    MEL_EXPECT(caps->power.thermal_pressure <= MEL_GPU_THERMAL_CRITICAL);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_texture, create_view_and_alive)
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

MEL_TEST(vk_render, offscreen_clear_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_device_caps(dev) != NULL);

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

MEL_TEST(vk_texture, write_and_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 W = 4, H = 4;
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

MEL_TEST(vk_render, buffer_barrier_submits_clean)
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

MEL_TEST(vk_bindless, sampler_dedup)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dev);
    MEL_REQUIRE(caps->memory.bindless.tier == MEL_GPU_TIER_FULL);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    Mel_Gpu_Sampler_Create_Result a = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_LINEAR, .mag_filter = MEL_GPU_FILTER_LINEAR, .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .name = "s");
    Mel_Gpu_Sampler_Create_Result b = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_LINEAR, .mag_filter = MEL_GPU_FILTER_LINEAR, .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .name = "s");
    MEL_REQUIRE(!mel_gpu_failed(a.status) && !mel_gpu_failed(b.status));
    MEL_EXPECT(mel_gpu_handle_eq(a.value.slot, b.value.slot));

    Mel_Gpu_Sampler_Create_Result c = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST, .wrap_u = MEL_GPU_WRAP_REPEAT, .name = "s2");
    MEL_REQUIRE(!mel_gpu_failed(c.status));
    MEL_EXPECT(!mel_gpu_handle_eq(a.value.slot, c.value.slot));

    mel_gpu_sampler_destroy(dev, a.value);
    MEL_EXPECT(mel_gpu_sampler_alive(dev, b.value));
    mel_gpu_sampler_destroy(dev, b.value);
    MEL_EXPECT(!mel_gpu_sampler_alive(dev, b.value));
    mel_gpu_sampler_destroy(dev, c.value);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_bindless, sample_texture_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    const u8 src_px[4] = { 40, 80, 120, 255 };
    Mel_Gpu_Texture_Create_Result src = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { 1, 1, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                               .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_DST, .name = "src");
    MEL_REQUIRE(!mel_gpu_failed(src.status));
    Mel_Gpu_Texture_Region region = { .subresource = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 }, .offset = { 0, 0, 0 }, .extent = { 1, 1, 1 } };
    mel_gpu_texture_write(dev, src.value, region, src_px, sizeof src_px);
    Mel_Gpu_Texture_View_Create_Result src_view = mel_gpu_texture_default_view(dev, src.value);
    MEL_REQUIRE(!mel_gpu_failed(src_view.status));

    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST,
                                                               .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE, .name = "nearest");
    MEL_REQUIRE(!mel_gpu_failed(smp.status));

    const u32 W = 8, H = 8;
    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                              .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "rt");
    MEL_REQUIRE(!mel_gpu_failed(rt.status));
    Mel_Gpu_Texture_View_Create_Result rt_view = mel_gpu_texture_default_view(dev, rt.value);
    MEL_REQUIRE(!mel_gpu_failed(rt_view.status));
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = BINDLESS_VERT_SPV, .spirv_vertex_size = sizeof BINDLESS_VERT_SPV,
                                                                          .spirv_fragment = BINDLESS_FRAG_SPV, .spirv_fragment_size = sizeof BINDLESS_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "bindless");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .name = "bindless-sample");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    struct
    {
        u32 tex;
        u32 smp;
    } root = { mel_gpu_texture_view_bindless_slot(dev, src_view.value), mel_gpu_sampler_bindless_slot(dev, smp.value) };

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);

    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);

    Mel_Gpu_Color_Attachment color = { .view = rt_view.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = W, .height = H);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
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
    MEL_EXPECT(px[0] >= 39 && px[0] <= 41);
    MEL_EXPECT(px[1] >= 79 && px[1] <= 81);
    MEL_EXPECT(px[2] >= 119 && px[2] <= 121);
    MEL_EXPECT_EQ(px[3], 255u);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_sampler_destroy(dev, smp.value);
    mel_gpu_texture_view_destroy(dev, src_view.value);
    mel_gpu_texture_view_destroy(dev, rt_view.value);
    mel_gpu_texture_destroy(dev, src.value);
    mel_gpu_texture_destroy(dev, rt.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_bindless, static_sampler_pipeline_create)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_LINEAR, .mag_filter = MEL_GPU_FILTER_LINEAR, .name = "static");
    MEL_REQUIRE(!mel_gpu_failed(smp.status));

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = BINDLESS_VERT_SPV, .spirv_vertex_size = sizeof BINDLESS_VERT_SPV,
                                                                          .spirv_fragment = BINDLESS_FRAG_SPV, .spirv_fragment_size = sizeof BINDLESS_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "bindless");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Static_Sampler statics[] = { { .sampler = smp.value, .binding = 0 } };
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .push_constant_size = 8,
                                                                  .bindless = true, .static_samplers = statics, .static_sampler_count = 1, .name = "static-sampler");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));
    MEL_EXPECT(mel_gpu_pipeline_alive(dev, pipe.value));

    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_sampler_destroy(dev, smp.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_bindless, missing_feature_without_heap)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(!mel_gpu_bindless_available(dev));

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = BINDLESS_VERT_SPV, .spirv_vertex_size = sizeof BINDLESS_VERT_SPV,
                                                                          .spirv_fragment = BINDLESS_SOLID_FRAG_SPV, .spirv_fragment_size = sizeof BINDLESS_SOLID_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "solid");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .push_constant_size = 8, .bindless = true, .name = "no-heap");
    MEL_EXPECT_EQ((u32)pipe.status, (u32)MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE);

    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_bindless, sampler_over_cap_fails_loudly)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dev);
    u32                 cap = caps->memory.bindless.max_sampler_slots;
    MEL_REQUIRE(cap > 0);

    const Mel_Alloc*   alloc = mel_alloc_heap();
    Mel_Gpu_Sampler*   live = mel_alloc_array(alloc, Mel_Gpu_Sampler, cap);
    u32                live_count = 0;
    bool               hit_exhausted = false;
    for (u32 i = 0; i <= cap; i++)
    {
        Mel_Gpu_Sampler_Create_Result s = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST,
                                                                  .wrap_u = MEL_GPU_WRAP_REPEAT, .lod_min = (f32)i * 0.01f, .lod_max = 64.0f, .name = "ovc");
        if ((u32)s.status == (u32)MEL_GPU_SAMPLER_CREATE_BINDLESS_SLOT_EXHAUSTED)
        {
            hit_exhausted = true;
            break;
        }
        MEL_REQUIRE(!mel_gpu_failed(s.status));
        MEL_REQUIRE(live_count < cap);
        live[live_count++] = s.value;
    }
    MEL_EXPECT(hit_exhausted);
    MEL_EXPECT_EQ(live_count, cap);

    for (u32 i = 0; i < live_count; i++)
        mel_gpu_sampler_destroy(dev, live[i]);
    mel_dealloc(alloc, live);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

static void test_bindless_render(Mel_Gpu_Device* dev, Mel_Gpu_Queue* q, Mel_Gpu_Pipeline pipe, u32 W, u32 H,
                                 Mel_Gpu_Texture rt, Mel_Gpu_Texture_View rt_view, u32 tex_slot, u32 smp_slot, Mel_Gpu_Buffer rb)
{
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, rt, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment color = { .view = rt_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = W, .height = H);
    mel_gpu_cmd_bind_pipeline(cmd, pipe);
    u32 root[2] = { tex_slot, smp_slot };
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, root);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, rt, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, rt, range, rb);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    mel_gpu_future_destroy(f);
    mel_gpu_command_list_destroy(cmd);
}

static Mel_Gpu_Texture_View test_make_color_texture(Mel_Gpu_Device* dev, const u8 rgba[4], Mel_Gpu_Texture* out_tex)
{
    Mel_Gpu_Texture_Create_Result t = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { 1, 1, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                             .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_DST, .name = "src");
    Mel_Gpu_Texture_Region region = { .subresource = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 }, .offset = { 0, 0, 0 }, .extent = { 1, 1, 1 } };
    mel_gpu_texture_write(dev, t.value, region, rgba, 4);
    *out_tex = t.value;
    return mel_gpu_texture_default_view(dev, t.value).value;
}

MEL_TEST(vk_bindless, slot_reuse_samples_correct)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST, .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE, .name = "n");
    MEL_REQUIRE(!mel_gpu_failed(smp.status));

    const u32 W = 4, H = 4;
    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                              .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "rt");
    Mel_Gpu_Texture_View_Create_Result rt_view = mel_gpu_texture_default_view(dev, rt.value);
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb");

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = BINDLESS_VERT_SPV, .spirv_vertex_size = sizeof BINDLESS_VERT_SPV,
                                                                          .spirv_fragment = BINDLESS_FRAG_SPV, .spirv_fragment_size = sizeof BINDLESS_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "bindless");
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .name = "p");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));
    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);

    Mel_Gpu_Texture     texA;
    const u8            ca[4] = { 30, 60, 90, 255 };
    Mel_Gpu_Texture_View viewA = test_make_color_texture(dev, ca, &texA);
    u32                 slotA = mel_gpu_texture_view_bindless_slot(dev, viewA);
    test_bindless_render(dev, q, pipe.value, W, H, rt.value, rt_view.value, slotA, mel_gpu_sampler_bindless_slot(dev, smp.value), rb.value);
    const u8* px = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_EXPECT(px[0] >= 29 && px[0] <= 31 && px[1] >= 59 && px[1] <= 61 && px[2] >= 89 && px[2] <= 91);

    mel_gpu_texture_view_destroy(dev, viewA);
    mel_gpu_texture_destroy(dev, texA);

    Mel_Gpu_Texture     texB;
    const u8            cb[4] = { 200, 150, 100, 255 };
    Mel_Gpu_Texture_View viewB = test_make_color_texture(dev, cb, &texB);
    u32                 slotB = mel_gpu_texture_view_bindless_slot(dev, viewB);
    MEL_EXPECT_EQ(slotB, slotA);
    test_bindless_render(dev, q, pipe.value, W, H, rt.value, rt_view.value, slotB, mel_gpu_sampler_bindless_slot(dev, smp.value), rb.value);
    px = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_EXPECT(px[0] >= 199 && px[0] <= 201 && px[1] >= 149 && px[1] <= 151 && px[2] >= 99 && px[2] <= 101);

    mel_gpu_texture_view_destroy(dev, viewB);
    mel_gpu_texture_destroy(dev, texB);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_sampler_destroy(dev, smp.value);
    mel_gpu_texture_view_destroy(dev, rt_view.value);
    mel_gpu_texture_destroy(dev, rt.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_bindless, bda_pointer_root_record)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bda(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dev);
    MEL_EXPECT_EQ((int)caps->memory.bindless.binding_model, (int)MEL_GPU_BINDING_MODEL_ROOT_RECORD);
    MEL_EXPECT_EQ((int)caps->memory.bindless.root_record_payload, (int)MEL_GPU_ROOT_RECORD_PAYLOAD_MIXED);

    const float colf[4] = { 0.2f, 0.4f, 0.6f, 1.0f };
    Mel_Gpu_Buffer_Create_Result colbuf = mel_gpu_buffer_create(dev, .size = sizeof colf, .usage = MEL_GPU_BUFFER_DEVICE_ADDRESS, .memory = MEL_GPU_MEMORY_DEVICE, .data = colf, .name = "color");
    MEL_REQUIRE(!mel_gpu_failed(colbuf.status));
    u64 addr = mel_gpu_buffer_device_address(dev, colbuf.value);
    MEL_REQUIRE(addr != 0);

    const u32 W = 4, H = 4;
    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                              .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "rt");
    Mel_Gpu_Texture_View_Create_Result rt_view = mel_gpu_texture_default_view(dev, rt.value);
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb");

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = BINDLESS_VERT_SPV, .spirv_vertex_size = sizeof BINDLESS_VERT_SPV,
                                                                          .spirv_fragment = BDA_FRAG_SPV, .spirv_fragment_size = sizeof BDA_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "bda");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .name = "bda");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment color = { .view = rt_view.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = W, .height = H);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof addr, &addr);
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
    MEL_EXPECT(px[0] >= 50 && px[0] <= 52);
    MEL_EXPECT(px[1] >= 101 && px[1] <= 103);
    MEL_EXPECT(px[2] >= 152 && px[2] <= 154);
    MEL_EXPECT_EQ(px[3], 255u);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_buffer_destroy(dev, colbuf.value);
    mel_gpu_texture_view_destroy(dev, rt_view.value);
    mel_gpu_texture_destroy(dev, rt.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_bindless, missing_bindless_slot)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));
    MEL_REQUIRE(mel_gpu_device_caps(dev)->memory.bindless.max_texture_view_slots < 20000u);

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = BINDLESS_VERT_SPV, .spirv_vertex_size = sizeof BINDLESS_VERT_SPV,
                                                                          .spirv_fragment = BINDLESS_OVERSIZE_FRAG_SPV, .spirv_fragment_size = sizeof BINDLESS_OVERSIZE_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "oversize");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .name = "oversize");
    MEL_EXPECT_EQ((u32)pipe.status, (u32)MEL_GPU_PIPELINE_CREATE_MISSING_BINDLESS_SLOT);
    MEL_EXPECT(!mel_gpu_pipeline_alive(dev, pipe.value));

    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_pipeline, spec_constants_bake)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 W = 4, H = 4;
    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                              .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "rt");
    Mel_Gpu_Texture_View_Create_Result rt_view = mel_gpu_texture_default_view(dev, rt.value);
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb");

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = BINDLESS_VERT_SPV, .spirv_vertex_size = sizeof BINDLESS_VERT_SPV,
                                                                          .spirv_fragment = SPEC_FRAG_SPV, .spirv_fragment_size = sizeof SPEC_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "spec");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    union { f32 f; u32 u; } r = { .f = 0.5f }, g = { .f = 0.25f };
    Mel_Gpu_Spec_Constant specs[] = { { .id = 0, .value = r.u }, { .id = 1, .value = g.u } };
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                  .spec_constants = specs, .spec_constant_count = 2, .name = "spec");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment color = { .view = rt_view.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = W, .height = H);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
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
    MEL_EXPECT(px[0] >= 126 && px[0] <= 129);
    MEL_EXPECT(px[1] >= 62 && px[1] <= 65);
    MEL_EXPECT_EQ(px[2], 0u);
    MEL_EXPECT_EQ(px[3], 255u);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_texture_view_destroy(dev, rt_view.value);
    mel_gpu_texture_destroy(dev, rt.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_pipeline, reflection_vertex_input)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const f32 verts[] = {
        -1.0f, -1.0f, 0.2f, 0.4f, 0.6f,
        3.0f, -1.0f, 0.2f, 0.4f, 0.6f,
        -1.0f, 3.0f, 0.2f, 0.4f, 0.6f,
    };
    Mel_Gpu_Buffer_Create_Result vbo = mel_gpu_buffer_create(dev, .size = sizeof verts, .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_DEVICE, .data = verts, .name = "vbo");
    MEL_REQUIRE(!mel_gpu_failed(vbo.status));

    const u32 W = 4, H = 4;
    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                              .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "rt");
    Mel_Gpu_Texture_View_Create_Result rt_view = mel_gpu_texture_default_view(dev, rt.value);
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb");

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = VTXREFL_VERT_SPV, .spirv_vertex_size = sizeof VTXREFL_VERT_SPV,
                                                                          .spirv_fragment = VTXREFL_FRAG_SPV, .spirv_fragment_size = sizeof VTXREFL_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "vtxrefl");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .name = "vtxrefl");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment color = { .view = rt_view.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = W, .height = H);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 0, vbo.value);
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
    MEL_EXPECT(px[0] >= 50 && px[0] <= 52);
    MEL_EXPECT(px[1] >= 101 && px[1] <= 103);
    MEL_EXPECT(px[2] >= 152 && px[2] <= 154);
    MEL_EXPECT_EQ(px[3], 255u);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_buffer_destroy(dev, vbo.value);
    mel_gpu_texture_view_destroy(dev, rt_view.value);
    mel_gpu_texture_destroy(dev, rt.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_bind_group, classic_descriptor_set)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(!mel_gpu_bindless_available(dev));

    const u8             src_px[4] = { 70, 140, 210, 255 };
    Mel_Gpu_Texture      src_tex;
    Mel_Gpu_Texture_View src_view = test_make_color_texture(dev, src_px, &src_tex);
    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST,
                                                               .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE, .name = "n");
    MEL_REQUIRE(!mel_gpu_failed(smp.status));

    const u32 W = 4, H = 4;
    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                              .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "rt");
    Mel_Gpu_Texture_View_Create_Result rt_view = mel_gpu_texture_default_view(dev, rt.value);
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb");

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = BINDLESS_VERT_SPV, .spirv_vertex_size = sizeof BINDLESS_VERT_SPV,
                                                                          .spirv_fragment = CLASSIC_FRAG_SPV, .spirv_fragment_size = sizeof CLASSIC_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "classic");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Bind_Group_Layout_Entry entries[] = { { .binding = 0, .kind = MEL_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER, .count = 1 } };
    Mel_Gpu_Bind_Group_Layout       bgl = mel_gpu_bind_group_layout_create(dev, entries, 1);
    MEL_REQUIRE(mel_gpu_bind_group_layout_alive(dev, bgl));

    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                  .set_layouts = &bgl, .set_layout_count = 1, .name = "classic");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Mel_Gpu_Bind_Group bg = mel_gpu_bind_group_create(dev, bgl);
    MEL_REQUIRE(mel_gpu_bind_group_alive(dev, bg));
    mel_gpu_bind_group_write_combined(dev, bg, 0, 0, src_view, smp.value);

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment color = { .view = rt_view.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = W, .height = H);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
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
    MEL_EXPECT(px[0] >= 69 && px[0] <= 71);
    MEL_EXPECT(px[1] >= 139 && px[1] <= 141);
    MEL_EXPECT(px[2] >= 209 && px[2] <= 211);
    MEL_EXPECT_EQ(px[3], 255u);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_bind_group_destroy(dev, bg);
    mel_gpu_bind_group_layout_destroy(dev, bgl);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_sampler_destroy(dev, smp.value);
    mel_gpu_texture_view_destroy(dev, src_view);
    mel_gpu_texture_view_destroy(dev, rt_view.value);
    mel_gpu_texture_destroy(dev, src_tex);
    mel_gpu_texture_destroy(dev, rt.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_bindless, static_sampler_lifetime)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_LINEAR, .mag_filter = MEL_GPU_FILTER_LINEAR, .name = "static-life");
    MEL_REQUIRE(!mel_gpu_failed(smp.status));

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = BINDLESS_VERT_SPV, .spirv_vertex_size = sizeof BINDLESS_VERT_SPV,
                                                                          .spirv_fragment = BINDLESS_FRAG_SPV, .spirv_fragment_size = sizeof BINDLESS_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "bindless");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Static_Sampler         statics[] = { { .sampler = smp.value, .binding = 0 } };
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .push_constant_size = 8,
                                                                  .bindless = true, .static_samplers = statics, .static_sampler_count = 1, .name = "static-life");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    mel_gpu_sampler_destroy(dev, smp.value);
    MEL_EXPECT(mel_gpu_sampler_alive(dev, smp.value));

    mel_gpu_pipeline_destroy(dev, pipe.value);
    MEL_EXPECT(!mel_gpu_sampler_alive(dev, smp.value));

    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_compute, storage_buffer_bindless)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    const u32 N = 64;
    u32       in_data[N];
    for (u32 i = 0; i < N; i++)
        in_data[i] = i;

    Mel_Gpu_Buffer_Create_Result in_buf = mel_gpu_buffer_create(dev, .size = sizeof in_data, .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_UPLOAD, .data = in_data, .name = "in");
    MEL_REQUIRE(!mel_gpu_failed(in_buf.status));
    Mel_Gpu_Buffer_Create_Result out_buf = mel_gpu_buffer_create(dev, .size = sizeof in_data, .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_READBACK, .name = "out");
    MEL_REQUIRE(!mel_gpu_failed(out_buf.status));

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_compute_from_bytecode(dev, .spirv = ADD_COMP_SPV, .spirv_size = sizeof ADD_COMP_SPV, .entry = "main", .name = "add");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_compute_create(dev, .shader = sh.value, .name = "add");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));
    MEL_REQUIRE(mel_gpu_pipeline_alive(dev, pipe.value));

    struct
    {
        u32 in_slot, out_slot, n;
    } root = {
        mel_gpu_buffer_bindless_slot(dev, in_buf.value),
        mel_gpu_buffer_bindless_slot(dev, out_buf.value),
        N,
    };

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_dispatch(cmd, (N + 63) / 64, 1, 1);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u32* out = mel_gpu_buffer_mapped(dev, out_buf.value);
    MEL_REQUIRE_NOT_NULL(out);
    bool ok = true;
    for (u32 i = 0; i < N; i++)
        if (out[i] != i + 1)
            ok = false;
    MEL_EXPECT(ok);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_buffer_destroy(dev, out_buf.value);
    mel_gpu_buffer_destroy(dev, in_buf.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_compute, storage_image_bindless)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    const u32 W = 8, H = 8;
    Mel_Gpu_Texture_Create_Result img = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                               .usage = MEL_GPU_TEXTURE_STORAGE | MEL_GPU_TEXTURE_COPY_SRC, .name = "img");
    MEL_REQUIRE(!mel_gpu_failed(img.status));
    Mel_Gpu_Texture_View_Create_Result view = mel_gpu_texture_default_view(dev, img.value);
    MEL_REQUIRE(!mel_gpu_failed(view.status));
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_compute_from_bytecode(dev, .spirv = IMGWRITE_COMP_SPV, .spirv_size = sizeof IMGWRITE_COMP_SPV, .entry = "main", .name = "imgwrite");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_compute_create(dev, .shader = sh.value, .name = "imgwrite");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));
    MEL_REQUIRE(mel_gpu_pipeline_alive(dev, pipe.value));

    struct { u32 img_slot, width, height; } root = { mel_gpu_texture_view_bindless_slot(dev, view.value), W, H };

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
    const u8* p00 = px;
    const u8* p42 = px + (2u * W + 4u) * 4u;
    const u8* p77 = px + (7u * W + 7u) * 4u;
    MEL_EXPECT_EQ(p00[0], 0u);
    MEL_EXPECT_EQ(p00[1], 0u);
    MEL_EXPECT(p00[2] >= 126 && p00[2] <= 130);
    MEL_EXPECT_EQ(p00[3], 255u);
    MEL_EXPECT(p42[0] >= 126 && p42[0] <= 130);
    MEL_EXPECT(p42[1] >= 62 && p42[1] <= 66);
    MEL_EXPECT(p42[2] >= 126 && p42[2] <= 130);
    MEL_EXPECT(p77[0] >= 221 && p77[0] <= 225);
    MEL_EXPECT(p77[1] >= 221 && p77[1] <= 225);

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

MEL_TEST(vk_compute, dispatch_indirect)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    const u32 N = 128;
    u32       in_data[N];
    for (u32 i = 0; i < N; i++)
        in_data[i] = i;

    Mel_Gpu_Buffer_Create_Result in_buf = mel_gpu_buffer_create(dev, .size = sizeof in_data, .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_UPLOAD, .data = in_data, .name = "in");
    MEL_REQUIRE(!mel_gpu_failed(in_buf.status));
    Mel_Gpu_Buffer_Create_Result out_buf = mel_gpu_buffer_create(dev, .size = sizeof in_data, .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_READBACK, .name = "out");
    MEL_REQUIRE(!mel_gpu_failed(out_buf.status));
    Mel_Gpu_Buffer_Create_Result args_buf = mel_gpu_buffer_create(dev, .size = 3 * sizeof(u32), .usage = MEL_GPU_BUFFER_STORAGE | MEL_GPU_BUFFER_INDIRECT, .memory = MEL_GPU_MEMORY_DEVICE, .name = "args");
    MEL_REQUIRE(!mel_gpu_failed(args_buf.status));

    Mel_Gpu_Shader_Create_Result fill_sh = mel_gpu_shader_create_compute_from_bytecode(dev, .spirv = FILLARGS_COMP_SPV, .spirv_size = sizeof FILLARGS_COMP_SPV, .entry = "main", .name = "fillargs");
    MEL_REQUIRE(!mel_gpu_failed(fill_sh.status));
    Mel_Gpu_Shader_Create_Result add_sh = mel_gpu_shader_create_compute_from_bytecode(dev, .spirv = ADD_COMP_SPV, .spirv_size = sizeof ADD_COMP_SPV, .entry = "main", .name = "add");
    MEL_REQUIRE(!mel_gpu_failed(add_sh.status));

    Mel_Gpu_Pipeline_Create_Result fill_pipe = mel_gpu_pipeline_compute_create(dev, .shader = fill_sh.value, .name = "fillargs");
    MEL_REQUIRE(!mel_gpu_failed(fill_pipe.status));
    Mel_Gpu_Pipeline_Create_Result add_pipe = mel_gpu_pipeline_compute_create(dev, .shader = add_sh.value, .name = "add");
    MEL_REQUIRE(!mel_gpu_failed(add_pipe.status));

    struct { u32 args_buf, groups_x; } fill_root = { mel_gpu_buffer_bindless_slot(dev, args_buf.value), (N + 63) / 64 };
    struct { u32 in_slot, out_slot, n; } add_root = { mel_gpu_buffer_bindless_slot(dev, in_buf.value), mel_gpu_buffer_bindless_slot(dev, out_buf.value), N };

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    mel_gpu_cmd_bind_pipeline(cmd, fill_pipe.value);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof fill_root, &fill_root);
    mel_gpu_cmd_dispatch(cmd, 1, 1, 1);
    mel_gpu_cmd_buffer_barrier(cmd, args_buf.value, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_INDIRECT_ARGUMENT);
    mel_gpu_cmd_bind_pipeline(cmd, add_pipe.value);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof add_root, &add_root);
    mel_gpu_cmd_dispatch_indirect(cmd, args_buf.value, 0);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u32* out = mel_gpu_buffer_mapped(dev, out_buf.value);
    MEL_REQUIRE_NOT_NULL(out);
    bool ok = true;
    for (u32 i = 0; i < N; i++)
        if (out[i] != i + 1)
            ok = false;
    MEL_EXPECT(ok);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, add_pipe.value);
    mel_gpu_pipeline_destroy(dev, fill_pipe.value);
    mel_gpu_shader_destroy(dev, add_sh.value);
    mel_gpu_shader_destroy(dev, fill_sh.value);
    mel_gpu_buffer_destroy(dev, args_buf.value);
    mel_gpu_buffer_destroy(dev, out_buf.value);
    mel_gpu_buffer_destroy(dev, in_buf.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_pipeline, alpha_blend)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 W = 4, H = 4;
    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                              .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "rt");
    Mel_Gpu_Texture_View_Create_Result rt_view = mel_gpu_texture_default_view(dev, rt.value);
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb");

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = BINDLESS_VERT_SPV, .spirv_vertex_size = sizeof BINDLESS_VERT_SPV,
                                                                          .spirv_fragment = SOLID_PC_FRAG_SPV, .spirv_fragment_size = sizeof SOLID_PC_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "blend");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Color_Target target = { .format = MEL_GPU_FORMAT_RGBA8_UNORM, .blend = MEL_GPU_BLEND_ALPHA };
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_targets = &target, .color_target_count = 1,
                                                                  .push_constant_size = 16, .name = "blend");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    f32 src[4] = { 1.0f, 0.0f, 0.0f, 0.5f };
    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment color = { .view = rt_view.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.2f, 0.4f, 0.6f, 1.0f) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = W, .height = H);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof src, src);
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
    MEL_EXPECT(px[0] >= 151 && px[0] <= 155);
    MEL_EXPECT(px[1] >= 49 && px[1] <= 53);
    MEL_EXPECT(px[2] >= 74 && px[2] <= 78);
    MEL_EXPECT_EQ(px[3], 255u);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_texture_view_destroy(dev, rt_view.value);
    mel_gpu_texture_destroy(dev, rt.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_pipeline, mrt_two_targets)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 W = 4, H = 4;
    Mel_Gpu_Texture_Create_Result t0 = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                              .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "t0");
    Mel_Gpu_Texture_Create_Result t1 = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                              .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "t1");
    Mel_Gpu_Texture_View_Create_Result v0 = mel_gpu_texture_default_view(dev, t0.value);
    Mel_Gpu_Texture_View_Create_Result v1 = mel_gpu_texture_default_view(dev, t1.value);
    Mel_Gpu_Buffer_Create_Result rb0 = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb0");
    Mel_Gpu_Buffer_Create_Result rb1 = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb1");

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = BINDLESS_VERT_SPV, .spirv_vertex_size = sizeof BINDLESS_VERT_SPV,
                                                                          .spirv_fragment = MRT_FRAG_SPV, .spirv_fragment_size = sizeof MRT_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "mrt");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Color_Target targets[2] = {
        { .format = MEL_GPU_FORMAT_RGBA8_UNORM, .blend = MEL_GPU_BLEND_OPAQUE },
        { .format = MEL_GPU_FORMAT_RGBA8_UNORM, .blend = MEL_GPU_BLEND_OPAQUE },
    };
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_targets = targets, .color_target_count = 2, .name = "mrt");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, t0.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    mel_gpu_cmd_texture_barrier(cmd, t1.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment colors[2] = {
        { .view = v0.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) },
        { .view = v1.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) },
    };
    mel_gpu_cmd_begin_rendering(cmd, .colors = colors, .color_count = 2, .width = W, .height = H);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, t0.value, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_texture_barrier(cmd, t1.value, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, t0.value, range, rb0.value);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, t1.value, range, rb1.value);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* p0 = mel_gpu_buffer_mapped(dev, rb0.value);
    const u8* p1 = mel_gpu_buffer_mapped(dev, rb1.value);
    MEL_REQUIRE_NOT_NULL(p0);
    MEL_REQUIRE_NOT_NULL(p1);
    MEL_EXPECT(p0[0] >= 62 && p0[0] <= 66);
    MEL_EXPECT(p0[1] >= 126 && p0[1] <= 130);
    MEL_EXPECT(p0[2] >= 189 && p0[2] <= 193);
    MEL_EXPECT_EQ(p1[0], 255u);
    MEL_EXPECT_EQ(p1[1], 0u);
    MEL_EXPECT(p1[2] >= 126 && p1[2] <= 130);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_buffer_destroy(dev, rb0.value);
    mel_gpu_buffer_destroy(dev, rb1.value);
    mel_gpu_texture_view_destroy(dev, v0.value);
    mel_gpu_texture_view_destroy(dev, v1.value);
    mel_gpu_texture_destroy(dev, t0.value);
    mel_gpu_texture_destroy(dev, t1.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_pipeline, depth_compare)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 W = 4, H = 4;
    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                              .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "rt");
    Mel_Gpu_Texture_View_Create_Result rt_view = mel_gpu_texture_default_view(dev, rt.value);
    Mel_Gpu_Texture_Create_Result depth = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_D32_FLOAT,
                                                                 .usage = MEL_GPU_TEXTURE_ATTACHMENT, .name = "depth");
    Mel_Gpu_Texture_View_Create_Result depth_view = mel_gpu_texture_default_view(dev, depth.value);
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb");

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = DEPTHPC_VERT_SPV, .spirv_vertex_size = sizeof DEPTHPC_VERT_SPV,
                                                                          .spirv_fragment = DEPTHPC_FRAG_SPV, .spirv_fragment_size = sizeof DEPTHPC_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "depth");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Depth_Stencil ds = { .depth_test = true, .depth_write = true, .depth_compare = MEL_GPU_COMPARE_LESS };
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                  .depth_format = MEL_GPU_FORMAT_D32_FLOAT, .depth_stencil = &ds, .push_constant_size = 20, .name = "depth");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    struct { f32 color[4]; f32 depth; } near_red = { { 1, 0, 0, 1 }, 0.5f }, far_green = { { 0, 1, 0, 1 }, 0.7f };
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
    mel_gpu_cmd_push_constants(cmd, 0, sizeof near_red, &near_red);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof far_green, &far_green);
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
    MEL_EXPECT_EQ(px[0], 255u);
    MEL_EXPECT_EQ(px[1], 0u);
    MEL_EXPECT_EQ(px[2], 0u);

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

MEL_TEST(vk_pipeline, msaa_renders_clean)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 W = 8, H = 8;
    Mel_Gpu_Texture_Create_Result rt = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                              .sample_count = 4, .usage = MEL_GPU_TEXTURE_ATTACHMENT, .name = "msaa-rt");
    Mel_Gpu_Texture_View_Create_Result rt_view = mel_gpu_texture_default_view(dev, rt.value);

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = BINDLESS_VERT_SPV, .spirv_vertex_size = sizeof BINDLESS_VERT_SPV,
                                                                          .spirv_fragment = SOLID_PC_FRAG_SPV, .spirv_fragment_size = sizeof SOLID_PC_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "msaa");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                  .samples = 4, .push_constant_size = 16, .name = "msaa");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    f32 white[4] = { 1, 1, 1, 1 };
    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, rt.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment color = { .view = rt_view.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = W, .height = H);
    mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof white, white);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_texture_view_destroy(dev, rt_view.value);
    mel_gpu_texture_destroy(dev, rt.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_render, msaa_resolve_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 W = 8, H = 8;
    Mel_Gpu_Texture_Create_Result msaa = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                .sample_count = 4, .usage = MEL_GPU_TEXTURE_ATTACHMENT, .name = "msaa");
    MEL_REQUIRE(!mel_gpu_failed(msaa.status));
    Mel_Gpu_Texture_View_Create_Result msaa_view = mel_gpu_texture_default_view(dev, msaa.value);
    MEL_REQUIRE(!mel_gpu_failed(msaa_view.status));
    Mel_Gpu_Texture_Create_Result resolve = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                   .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "resolve");
    MEL_REQUIRE(!mel_gpu_failed(resolve.status));
    Mel_Gpu_Texture_View_Create_Result resolve_view = mel_gpu_texture_default_view(dev, resolve.value);
    MEL_REQUIRE(!mel_gpu_failed(resolve_view.status));
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb");

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = BINDLESS_VERT_SPV, .spirv_vertex_size = sizeof BINDLESS_VERT_SPV,
                                                                          .spirv_fragment = SOLID_PC_FRAG_SPV, .spirv_fragment_size = sizeof SOLID_PC_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "msaa");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                  .samples = 4, .push_constant_size = 16, .name = "msaa");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    f32 white[4] = { 1, 1, 1, 1 };
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
    const u8* c = px + (4u * W + 4u) * 4u;
    MEL_EXPECT_EQ(c[0], 255u);
    MEL_EXPECT_EQ(c[1], 255u);
    MEL_EXPECT_EQ(c[2], 255u);
    MEL_EXPECT_EQ(c[3], 255u);

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

MEL_TEST(vk_queue, submit_many_command_lists)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 N = 16, W = 8, H = 8;
    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    MEL_REQUIRE_NOT_NULL(q);

    const Mel_Alloc*      alloc = mel_alloc_heap();
    Mel_Gpu_Texture*      tex = mel_alloc_array(alloc, Mel_Gpu_Texture, N);
    Mel_Gpu_Texture_View* view = mel_alloc_array(alloc, Mel_Gpu_Texture_View, N);
    Mel_Gpu_Buffer*       rb = mel_alloc_array(alloc, Mel_Gpu_Buffer, N);
    Mel_Gpu_Command_List** cls = mel_alloc_array(alloc, Mel_Gpu_Command_List*, N);

    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    for (u32 i = 0; i < N; i++)
    {
        Mel_Gpu_Texture_Create_Result t = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                 .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "rt-many");
        MEL_REQUIRE(!mel_gpu_failed(t.status));
        tex[i] = t.value;
        view[i] = mel_gpu_texture_default_view(dev, t.value).value;
        rb[i] = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb-many").value;

        Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
        mel_gpu_command_list_begin(cmd);
        mel_gpu_cmd_texture_barrier(cmd, tex[i], range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
        f32 shade = (f32)i / (f32)(N - 1);
        Mel_Gpu_Color_Attachment color = { .view = view[i], .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(shade, shade, shade, 1.0f) };
        mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = W, .height = H);
        mel_gpu_cmd_end_rendering(cmd);
        mel_gpu_cmd_texture_barrier(cmd, tex[i], range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
        mel_gpu_cmd_copy_texture_to_buffer(cmd, tex[i], range, rb[i]);
        mel_gpu_command_list_end(cmd);
        cls[i] = cmd;
    }

    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = cls, .command_list_count = N });
    MEL_REQUIRE_NOT_NULL(f);
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    for (u32 i = 0; i < N; i++)
    {
        const u8* px = mel_gpu_buffer_mapped(dev, rb[i]);
        MEL_REQUIRE_NOT_NULL(px);
        u8 want = (u8)(((f32)i / (f32)(N - 1)) * 255.0f + 0.5f);
        MEL_EXPECT(px[0] >= (want > 2 ? want - 2 : 0) && px[0] <= (want < 253 ? want + 2 : 255));
    }

    for (u32 i = 0; i < N; i++)
    {
        mel_gpu_command_list_destroy(cls[i]);
        mel_gpu_buffer_destroy(dev, rb[i]);
        mel_gpu_texture_view_destroy(dev, view[i]);
        mel_gpu_texture_destroy(dev, tex[i]);
    }
    mel_dealloc(alloc, cls);
    mel_dealloc(alloc, rb);
    mel_dealloc(alloc, view);
    mel_dealloc(alloc, tex);
    mel_gpu_queue_release(q);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_render, command_list_state_reset_on_rerecord)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 W = 16, H = 16;
    Mel_Gpu_Texture_Create_Result t = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                             .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "rt-reset");
    MEL_REQUIRE(!mel_gpu_failed(t.status));
    Mel_Gpu_Texture_View_Create_Result v = mel_gpu_texture_default_view(dev, t.value);
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb-reset");
    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };

    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    MEL_REQUIRE_NOT_NULL(cmd);
    for (u32 frame = 0; frame < 2; frame++)
    {
        mel_gpu_command_list_begin(cmd);
        mel_gpu_cmd_texture_barrier(cmd, t.value, range, MEL_GPU_STATE_COMMON, MEL_GPU_STATE_RENDER_TARGET);
        Mel_Gpu_Color_Attachment color = { .view = v.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.5f, 0.5f, 0.5f, 1.0f) };
        mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = W, .height = H);
        mel_gpu_cmd_end_rendering(cmd);
        mel_gpu_cmd_texture_barrier(cmd, t.value, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
        mel_gpu_cmd_copy_texture_to_buffer(cmd, t.value, range, rb.value);
        mel_gpu_command_list_end(cmd);
        Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
        MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
        mel_gpu_future_destroy(f);
    }
    const u8* px = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_REQUIRE_NOT_NULL(px);
    MEL_EXPECT(px[0] >= 125 && px[0] <= 131);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_texture_view_destroy(dev, v.value);
    mel_gpu_texture_destroy(dev, t.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_alloc, repeated_uploads_round_trip)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 ROUNDS = 24;
    const usize N = 256;
    u8* src = mel_alloc_array(mel_alloc_heap(), u8, N);
    for (u32 r = 0; r < ROUNDS; r++)
    {
        for (usize i = 0; i < N; i++)
            src[i] = (u8)((i + r * 7u) & 0xFFu);
        Mel_Gpu_Buffer_Create_Result dst = mel_gpu_buffer_create(dev, .size = N, .usage = MEL_GPU_BUFFER_STORAGE | MEL_GPU_BUFFER_TRANSFER_SRC | MEL_GPU_BUFFER_TRANSFER_DST,
                                                                  .memory = MEL_GPU_MEMORY_DEVICE, .data = src, .name = "upload-churn");
        MEL_REQUIRE(!mel_gpu_failed(dst.status));
        Mel_Gpu_Buffer_Create_Result back = mel_gpu_buffer_create(dev, .size = N, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "upload-back");
        Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
        Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
        mel_gpu_command_list_begin(cmd);
        mel_gpu_cmd_copy_buffer(cmd, dst.value, back.value, N);
        mel_gpu_command_list_end(cmd);
        Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
        MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
        mel_gpu_future_destroy(f);
        const u8* got = mel_gpu_buffer_mapped(dev, back.value);
        MEL_REQUIRE_NOT_NULL(got);
        bool match = true;
        for (usize i = 0; i < N; i++)
            if (got[i] != src[i])
                match = false;
        MEL_EXPECT(match);
        mel_gpu_command_list_destroy(cmd);
        mel_gpu_queue_release(q);
        mel_gpu_buffer_destroy(dev, back.value);
        mel_gpu_buffer_destroy(dev, dst.value);
    }
    mel_dealloc(mel_alloc_heap(), src);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_swapchain, extent_accessor_null_contract)
{
    Mel_Gpu_Swapchain_Extent e = mel_gpu_swapchain_extent(NULL);
    MEL_EXPECT_EQ(e.width, 0u);
    MEL_EXPECT_EQ(e.height, 0u);
}

typedef struct
{
    Mel_Gpu_Thread_Tracker* tracker;
    Mel_Barrier*            both_in;
    const void*             shared;
    _Atomic(u32)*           done;
} Vk_Tracker_Misuse_Ctx;

static int vk_tracker_misuse_worker(void* user)
{
    Vk_Tracker_Misuse_Ctx* c = user;
    mel_gpu_thread_tracker_enter(c->tracker, c->shared, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
    mel_barrier_wait(c->both_in);
    mel_gpu_thread_tracker_exit(c->tracker, c->shared);
    atomic_fetch_add(c->done, 1);
    return 0;
}

MEL_TEST(vk_tracker, cross_thread_misuse_reports_without_aborting)
{
    Mel_Gpu_Thread_Tracker* tracker = mel_gpu_thread_tracker_create();
    MEL_REQUIRE_NOT_NULL(tracker);

    int          shared_object = 0;
    _Atomic(u32) done;
    atomic_store(&done, 0);

    Mel_Barrier both_in;
    MEL_REQUIRE(mel_barrier_init(&both_in, 2));

    Vk_Tracker_Misuse_Ctx ctx = { .tracker = tracker, .both_in = &both_in, .shared = &shared_object, .done = &done };
    Mel_Thread            t0, t1;
    MEL_REQUIRE(mel_thread_spawn(&t0, vk_tracker_misuse_worker, &ctx, .name = "trk-misuse-0"));
    MEL_REQUIRE(mel_thread_spawn(&t1, vk_tracker_misuse_worker, &ctx, .name = "trk-misuse-1"));
    mel_thread_join(&t0, NULL);
    mel_thread_join(&t1, NULL);

    MEL_EXPECT_EQ(atomic_load(&done), 2u);

    int after = 0;
    mel_gpu_thread_tracker_enter(tracker, &after, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
    mel_gpu_thread_tracker_exit(tracker, &after);

    mel_barrier_destroy(&both_in);
    mel_gpu_thread_tracker_destroy(tracker);
}

MEL_TEST(vk_raster, fill_mode_non_solid_cap_reflects_reality)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dev);
    MEL_REQUIRE_NOT_NULL(caps);
    MEL_EXPECT(caps->raster.fill_mode_non_solid == true);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_render, begin_rendering_auto_transition_no_manual_barrier)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 W = 32, H = 32;
    Mel_Gpu_Texture_Create_Result t = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                             .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_COPY_SRC, .name = "rt-auto");
    MEL_REQUIRE(!mel_gpu_failed(t.status));
    Mel_Gpu_Texture_View_Create_Result v = mel_gpu_texture_default_view(dev, t.value);
    MEL_REQUIRE(!mel_gpu_failed(v.status));
    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)W * H * 4, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "rb-auto");
    MEL_REQUIRE(!mel_gpu_failed(rb.status));

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);

    Mel_Gpu_Color_Attachment color = { .view = v.value, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.1f, 0.2f, 0.3f, 1.0f) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = W, .height = H);
    mel_gpu_cmd_end_rendering(cmd);

    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, t.value, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_COPY_SOURCE);
    mel_gpu_cmd_copy_texture_to_buffer(cmd, t.value, range, rb.value);
    mel_gpu_command_list_end(cmd);

    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_REQUIRE_NOT_NULL(f);
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    const u8* px = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_REQUIRE_NOT_NULL(px);
    MEL_EXPECT(px[0] >= 24 && px[0] <= 28);
    MEL_EXPECT(px[1] >= 49 && px[1] <= 53);
    MEL_EXPECT(px[2] >= 75 && px[2] <= 79);
    MEL_EXPECT_EQ(px[3], 255u);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_texture_view_destroy(dev, v.value);
    mel_gpu_texture_destroy(dev, t.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(vk_query, timestamp_delta_plausible)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dev);
    MEL_REQUIRE_NOT_NULL(caps);
    if (caps->queries.timestamp == MEL_GPU_TIMESTAMP_NONE || !caps->queries.timestamp_compute_and_graphics || caps->queries.timestamp_period_ns <= 0.0)
    {
        mel_gpu_device_destroy(dev);
        mel_gpu_instance_destroy(inst);
        MEL_SKIP("device does not grant timestamp queries (timestamp tier/compute-and-graphics/period absent)");
    }

    Mel_Gpu_Query_Pool_Create_Result qp = mel_gpu_query_pool_create(dev, .type = MEL_GPU_QUERY_TIMESTAMP, .count = 2, .name = "ts");
    MEL_REQUIRE(!mel_gpu_failed(qp.status));
    MEL_REQUIRE(mel_gpu_query_pool_alive(dev, qp.value));

    const usize BYTES = 4u * 1024u * 1024u;
    Mel_Gpu_Buffer_Create_Result src = mel_gpu_buffer_create(dev, .size = BYTES, .usage = MEL_GPU_BUFFER_TRANSFER_SRC, .memory = MEL_GPU_MEMORY_DEVICE, .name = "ts-src");
    MEL_REQUIRE(!mel_gpu_failed(src.status));
    Mel_Gpu_Buffer_Create_Result dst = mel_gpu_buffer_create(dev, .size = BYTES, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_DEVICE, .name = "ts-dst");
    MEL_REQUIRE(!mel_gpu_failed(dst.status));

    Mel_Gpu_Queue*        q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    mel_gpu_cmd_reset_query_pool(cmd, qp.value, 0, 2);
    mel_gpu_cmd_write_timestamp(cmd, qp.value, 0);
    mel_gpu_cmd_copy_buffer(cmd, src.value, dst.value, BYTES);
    mel_gpu_cmd_buffer_barrier(cmd, dst.value, MEL_GPU_STATE_COPY_DEST, MEL_GPU_STATE_COMMON);
    mel_gpu_cmd_write_timestamp(cmd, qp.value, 1);
    mel_gpu_command_list_end(cmd);

    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    MEL_REQUIRE_NOT_NULL(f);
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    mel_gpu_future_destroy(f);

    u64 ns[2] = { 0, 0 };
    MEL_REQUIRE(mel_gpu_query_pool_resolve(dev, qp.value, 0, 2, ns));
    MEL_EXPECT(ns[1] > ns[0]);
    u64 delta = ns[1] - ns[0];
    MEL_EXPECT(delta > 0);
    MEL_EXPECT(delta < 1000000000ull);

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_buffer_destroy(dev, dst.value);
    mel_gpu_buffer_destroy(dev, src.value);
    mel_gpu_query_pool_destroy(dev, qp.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

#else

MEL_TEST(vk_device, skipped_without_vulkan) { MEL_SKIP("vulkan backend not selected (build with --gpu=vulkan)"); }

#endif
