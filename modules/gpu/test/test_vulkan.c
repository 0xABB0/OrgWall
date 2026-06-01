#include <test/test.h>

#if MEL_GPU_VULKAN

#include <gpu/device.h>
#include <gpu/caps.h>
#include <gpu/buffer.h>
#include <gpu/texture.h>
#include <gpu/queue.h>
#include <gpu/command.h>
#include <gpu/rendering.h>
#include <gpu/state.h>
#include <gpu/memory.h>
#include <gpu/format_props.h>
#include <gpu/vulkan/interop.h>

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

#else

MEL_TEST(vk_device, skipped_without_vulkan) { MEL_SKIP("vulkan backend not selected (build with --gpu=vulkan)"); }

#endif
