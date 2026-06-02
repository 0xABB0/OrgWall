#include <test/test.h>

#if MEL_GPU_VULKAN

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
#include <gpu/memory.h>
#include <gpu/format_props.h>
#include <gpu/vulkan/interop.h>

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

// U14: a device with the bindless heap enabled. Returns NULL when the descriptor-indexing floor is absent.
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

// U14 ceiling: heap + buffer-device-address, so the root record can carry real GPU pointers (mixed payload).
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

// U10 + U15 + U16 + U17 end-to-end: render a clear into an offscreen texture, copy it to a readback
// buffer, and verify the cleared pixel on the CPU — the machine-checkable proof M1 could not produce.
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
    MEL_EXPECT(px[0] >= 62 && px[0] <= 66);    // r = 0.25
    MEL_EXPECT(px[1] >= 126 && px[1] <= 130);  // g = 0.5
    MEL_EXPECT(px[2] >= 189 && px[2] <= 193);  // b = 0.75
    MEL_EXPECT_EQ(px[3], 255u);                // a = 1.0

    mel_gpu_command_list_destroy(cmd);
    mel_gpu_queue_release(q);
    mel_gpu_buffer_destroy(dev, rb.value);
    mel_gpu_texture_view_destroy(dev, v.value);
    mel_gpu_texture_destroy(dev, t.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

// U10 texture_write: upload a known pattern, copy it back, verify it survived the round-trip.
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

// U17 buffer-state barrier records a validation-clean transition end-to-end.
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

// U11 sampler auto-dedup (gpu-rhi.md §6.3): identical descriptors collapse to one shared handle; a
// different descriptor gets its own.
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
    // Same descriptor -> same interned handle.
    MEL_EXPECT(mel_gpu_handle_eq(a.value.slot, b.value.slot));

    Mel_Gpu_Sampler_Create_Result c = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST, .wrap_u = MEL_GPU_WRAP_REPEAT, .name = "s2");
    MEL_REQUIRE(!mel_gpu_failed(c.status));
    MEL_EXPECT(!mel_gpu_handle_eq(a.value.slot, c.value.slot));

    // Two claims on the shared sampler: first destroy keeps it alive, second frees it.
    mel_gpu_sampler_destroy(dev, a.value);
    MEL_EXPECT(mel_gpu_sampler_alive(dev, b.value));
    mel_gpu_sampler_destroy(dev, b.value);
    MEL_EXPECT(!mel_gpu_sampler_alive(dev, b.value));
    mel_gpu_sampler_destroy(dev, c.value);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

// U14 end-to-end: a fullscreen triangle samples a heap-resident texture through a heap-resident sampler,
// indexed by a push-constant root record, into an offscreen target — the machine-checkable proof that a
// shader can read a bindless resource (gpu-rhi.md §6.7).
MEL_TEST(vk_bindless, sample_texture_readback)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    // Source: a 1x1 RGBA8 texture holding a known colour, registered as a sampled image in the heap.
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

    // Offscreen target + readback.
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

    // No explicit .bindless / .push_constant_size: U12 reflection-lite derives both from the SPIR-V (set 0
    // usage + the 8-byte root record). The shader is the source of truth for its layout (gpu-rhi.md §6.4).
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
    // Every fragment sampled the single source texel: the whole target equals the source colour.
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

// U11 static/immutable samplers (gpu-rhi.md §6.3): a pipeline layout admits a baked sampler from day one.
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

    // A bindless pipeline (set 0 = heap) with a static sampler baked into set 1.
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

// A pipeline that requests bindless on a device without the heap fails with MissingFeature, not a crash.
MEL_TEST(vk_bindless, missing_feature_without_heap)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device(&inst); // no descriptor_indexing -> no heap
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(!mel_gpu_bindless_available(dev));

    // A plain shader (no descriptor arrays) so the device's lack of the heap is the only thing under test.
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

// Records a bindless sample of (tex_slot, smp_slot) into rt and copies it to rb. Submits synchronously.
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

// U14 future-gated slot reclamation (gpu-rhi.md §3.3 / §6.7): after a heap-registered view is destroyed and
// a new one is created at the reclaimed slot, sampling that slot must return the NEW resource's contents —
// the heap descriptor is rewritten, never left stale.
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

    // Resource A at some slot, sampled -> A's colour.
    Mel_Gpu_Texture     texA;
    const u8            ca[4] = { 30, 60, 90, 255 };
    Mel_Gpu_Texture_View viewA = test_make_color_texture(dev, ca, &texA);
    u32                 slotA = mel_gpu_texture_view_bindless_slot(dev, viewA);
    test_bindless_render(dev, q, pipe.value, W, H, rt.value, rt_view.value, slotA, mel_gpu_sampler_bindless_slot(dev, smp.value), rb.value);
    const u8* px = mel_gpu_buffer_mapped(dev, rb.value);
    MEL_EXPECT(px[0] >= 29 && px[0] <= 31 && px[1] >= 59 && px[1] <= 61 && px[2] >= 89 && px[2] <= 91);

    // Destroy A; the prior submit has retired (synchronous), so the slot index is reclaimed.
    mel_gpu_texture_view_destroy(dev, viewA);
    mel_gpu_texture_destroy(dev, texA);

    // Resource B reuses the slot. Sampling it must return B's colour, not A's stale descriptor.
    Mel_Gpu_Texture     texB;
    const u8            cb[4] = { 200, 150, 100, 255 };
    Mel_Gpu_Texture_View viewB = test_make_color_texture(dev, cb, &texB);
    u32                 slotB = mel_gpu_texture_view_bindless_slot(dev, viewB);
    MEL_EXPECT_EQ(slotB, slotA); // the reclaimed index is reused
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

// U14 pointer-bearing root-record ceiling (gpu-rhi.md §6.7): the fragment dereferences a buffer purely by
// its device address carried in the push-constant root record — no descriptor set at all. Proves the mixed
// payload (buffers as real pointers) the caps now report.
MEL_TEST(vk_bindless, bda_pointer_root_record)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = test_make_device_bda(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dev);
    MEL_EXPECT_EQ((int)caps->memory.bindless.binding_model, (int)MEL_GPU_BINDING_MODEL_ROOT_RECORD);
    MEL_EXPECT_EQ((int)caps->memory.bindless.root_record_payload, (int)MEL_GPU_ROOT_RECORD_PAYLOAD_MIXED);

    // A device-address buffer holding one vec4 colour; the shader reads it through its GPU pointer.
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
    // Reflection derives an 8-byte push constant and no descriptor-set-0 usage: a pure-pointer pipeline.
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
    MEL_EXPECT(px[0] >= 50 && px[0] <= 52);    // 0.2
    MEL_EXPECT(px[1] >= 101 && px[1] <= 103);  // 0.4
    MEL_EXPECT(px[2] >= 152 && px[2] <= 154);  // 0.6
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

#else

MEL_TEST(vk_device, skipped_without_vulkan) { MEL_SKIP("vulkan backend not selected (build with --gpu=vulkan)"); }

#endif
