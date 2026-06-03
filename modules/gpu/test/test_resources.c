#include <test/test.h>

#if MEL_GPU_VULKAN || MEL_GPU_METAL

#include <gpu/device.h>
#include <gpu/buffer.h>
#include <gpu/texture.h>
#include <gpu/sampler.h>
#include <gpu/status.h>

#include <allocator/heap.h>

#include <string.h>

static Mel_Gpu_Device* res_make_device(Mel_Gpu_Instance** out_inst)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-resources", .debug = { .enabled = true });
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

MEL_TEST(res_buffer, write_readback_roundtrip)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = res_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    enum { N = 64 };
    u32 pattern[N];
    for (u32 i = 0; i < N; i++)
        pattern[i] = 0xC0DE0000u + i;

    Mel_Gpu_Buffer_Create_Result b =
        mel_gpu_buffer_create(dev, .size = sizeof pattern, .usage = MEL_GPU_BUFFER_UNIFORM | MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_UPLOAD, .name = "rt");
    MEL_REQUIRE(!mel_gpu_failed(b.status));
    MEL_REQUIRE(mel_gpu_buffer_alive(dev, b.value));

    mel_gpu_buffer_write(dev, b.value, pattern, sizeof pattern);

    const u32* mapped = (const u32*)mel_gpu_buffer_mapped(dev, b.value);
    MEL_REQUIRE(mapped != NULL);
    bool ok = true;
    for (u32 i = 0; i < N; i++)
        if (mapped[i] != pattern[i])
        {
            ok = false;
            break;
        }
    MEL_EXPECT(ok);

    mel_gpu_buffer_destroy(dev, b.value);
    MEL_EXPECT(!mel_gpu_buffer_alive(dev, b.value));

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(res_buffer, create_use_destroy_churn)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = res_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    for (u32 i = 0; i < 256; i++)
    {
        Mel_Gpu_Memory_Role mem = (i & 1) ? MEL_GPU_MEMORY_UPLOAD : MEL_GPU_MEMORY_DEVICE;
        Mel_Gpu_Buffer_Create_Result b =
            mel_gpu_buffer_create(dev, .size = 256 + i, .usage = MEL_GPU_BUFFER_STORAGE | MEL_GPU_BUFFER_TRANSFER_DST, .memory = mem, .name = "churn");
        MEL_REQUIRE(!mel_gpu_failed(b.status));
        if (mem == MEL_GPU_MEMORY_UPLOAD)
        {
            u32 v = 0xABCD0000u + i;
            mel_gpu_buffer_write(dev, b.value, &v, sizeof v);
            const u32* m = (const u32*)mel_gpu_buffer_mapped(dev, b.value);
            MEL_REQUIRE(m != NULL);
            MEL_EXPECT_EQ(m[0], v);
        }
        mel_gpu_buffer_destroy(dev, b.value);
        MEL_EXPECT(!mel_gpu_buffer_alive(dev, b.value));
    }

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(res_texture, create_view_destroy_churn)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = res_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    for (u32 i = 0; i < 128; i++)
    {
        Mel_Gpu_Texture_Create_Result t =
            mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { 8 + (i % 16), 8, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                   .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_DST, .name = "tex");
        MEL_REQUIRE(!mel_gpu_failed(t.status));
        MEL_REQUIRE(mel_gpu_texture_alive(dev, t.value));

        Mel_Gpu_Texture_View_Create_Result v = mel_gpu_texture_default_view(dev, t.value);
        MEL_REQUIRE(!mel_gpu_failed(v.status));

        mel_gpu_texture_view_destroy(dev, v.value);
        mel_gpu_texture_destroy(dev, t.value);
        MEL_EXPECT(!mel_gpu_texture_alive(dev, t.value));
    }

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(res_sampler, create_destroy_churn)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = res_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    for (u32 i = 0; i < 128; i++)
    {
        Mel_Gpu_Sampler_Create_Result s =
            mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_LINEAR, .mag_filter = MEL_GPU_FILTER_NEAREST, .wrap_u = MEL_GPU_WRAP_REPEAT,
                                   .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE, .lod_max = (f32)(i + 1), .name = "smp");
        MEL_REQUIRE(!mel_gpu_failed(s.status));
        MEL_REQUIRE(mel_gpu_sampler_alive(dev, s.value));
        mel_gpu_sampler_destroy(dev, s.value);
        MEL_EXPECT(!mel_gpu_sampler_alive(dev, s.value));
    }

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

#endif
